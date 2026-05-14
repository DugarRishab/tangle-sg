#include "../headers/tangle.h"
#include "../headers/transaction.h"
#include "../headers/utils.h"
#include <iostream>
#include <sstream>
#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <queue>
#include <openssl/sha.h>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include "../headers/debug_lock.h"
using namespace std;

Transaction Tangle::getTransaction(const std::string &transaction_id)
{
    // lock_guard<mutex> lock(tangleMutex);
    // DebugScopedLock<std::mutex> lock(tangleMutex, "tangleMutex", 10);
    std::shared_lock lock(tangleMutex);

    auto it = transactions.find(transaction_id);
    if (it != transactions.end())
    {
        return it->second; // Return the found transaction
    }
    else
    {
        std::cerr << "[ERROR] Transaction with ID " << transaction_id << " not found in Tangle." << std::endl;
        return Transaction(); // Return an empty transaction if not found
    }
}

std::unordered_map<std::string, Transaction> Tangle::getAllTransactions()
{
    // lock_guard<mutex> lock(tangleMutex);
    // DebugScopedLock<std::mutex> lock(tangleMutex, "tangleMutex", 10);
    std::shared_lock lock(tangleMutex);

    // Return a copy of the transactions map
    std::unordered_map<std::string, Transaction> transactionsCopy = transactions;
    return transactionsCopy;
}

Transaction Tangle::addNewTransaction(Transaction &tx)
{
    // calculate checksum

    // assing uid to the tra
    tx.data.transaction_id = to_string(tx.data.timestamp) + "_" + tx.data.sender + "_" + tx.data.receiver;
    string txData = serializeTransactionData(tx);
    // Ignoring checksum because signatiures are used for integrity
    // string checksum = computeChecksum(txData);
    // Update metadata
    // tx.metadata.checksum = checksum;

    // cummulative weight is not updated here. it is done after double signing
    tx.metadata.cumulative_weight = 0; // Initialize cumulative weight
    tx.metadata.status = TransactionStatus::PROPOSED;

    // sign the transaction
    tx.metadata.signature1 = signTransaction(txData);

    // Lock the mutex to protect shared Tangle access
    // lock_guard<mutex> lock(tangleMutex);
    // DebugScopedLock<std::mutex> lock(tangleMutex, "tangleMutex", 10);
    std::unique_lock lock(tangleMutex);

    const char *env_uid = std::getenv("UID");
    std::string uid_str = env_uid ? env_uid : "";

    tx.metadata.hops.emplace_back(timeNow(), uid_str); // Add hop with current timestamp and UID

    transactions[tx.data.transaction_id] = tx;

    // Populate children index for each parent
    for (const auto& parent : tx.data.parents) {
        children[parent].insert(tx.data.transaction_id);
        auto pit = transactions.find(parent);
        if (pit != transactions.end()) {
            pit->second.metadata.reference_count = children[parent].size();
        }
        updateDescendantWeight(parent, 1);
    }

    return tx;
}

int Tangle::addTransaction(Transaction &tx, int update)
{
    // Lock the mutex to protect shared Tangle access
    // lock_guard<mutex> lock(tangleMutex);
    // DebugScopedLock<std::mutex> lock(tangleMutex, "tangleMutex", 10);
    std::unique_lock lock(tangleMutex);

    // TODO: check if tx already exists
    auto it = transactions.find(tx.data.transaction_id);
    if (it == transactions.end())
    {
        const char *env_uid = std::getenv("UID");
        std::string uid_str = env_uid ? env_uid : "";

        tx.metadata.hops.emplace_back(timeNow(), uid_str); // Add hop with current timestamp and UID
        transactions[tx.data.transaction_id] = tx;

        // Populate children index for each parent
        for (const auto& parent : tx.data.parents) {
            children[parent].insert(tx.data.transaction_id);
            auto pit = transactions.find(parent);
            if (pit != transactions.end()) {
                pit->second.metadata.reference_count = children[parent].size();
            }
            updateDescendantWeight(parent, 1);
        }

        std::cout << "[TANGLE] Transaction added to Tangle: " << tx.data.transaction_id << endl;
        return 2; // Transaction added
    }
    else if (update)
    {
        std::cout << "[TANGLE] Transaction already exists in Tangle. Updating it: " << tx.data.transaction_id << endl;
        int updated = updateTransaction(tx, 1);
        return updated; // Transaction updated
    }
    else
    {
        // dont even update. update = 0
        return 0; // Transaction already exists, no update
    }
}
// REMOVED: updateCumulativeWeightOfParents and updateCumulativeWeight
// Use updateDescendantWeight (BFS) instead.

// Serializes the Tangle's transactions into a string format
// each part is separated by a comma
// and each transaction is separated by a semicolon
string Tangle::serialize()
{
    stringstream ss;

    for (const auto &[txID, tx] : transactions)
    { // transactions is the Tangle's storage map
        ss << serializeTransaction(tx) << ";";
    }
    return ss.str();
}


// Returns the Tangle's transactions in a deserialized format
std::unordered_map<std::string, Transaction> Tangle::deserialize(const string &data)
{
    stringstream ss(data);
    string line;

    string txString;
    std::unordered_map<std::string, Transaction> transactions;

    // split by semicolon
    while (getline(ss, txString, ';'))
    {

        Transaction tx = deserializeTransaction(txString);
        transactions[tx.data.transaction_id] = tx;
    }

    return transactions;
}

static inline void trim_inplace(std::string &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch)
                                    { return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch)
                         { return !std::isspace(ch); })
                .base(),
            s.end());
}

std::string extract_between_brackets(std::stringstream &ss)
{
    std::string chunk;
    if (!std::getline(ss, chunk, ']'))
        return ""; // nothing or malformed
    auto pos = chunk.find('[');
    if (pos == std::string::npos)
        return ""; // no '[' found
    std::string inner = chunk.substr(pos + 1);
    trim_inplace(inner);
    return inner; // could be empty -> means []
}

// Updates the Tangle from a serialized string
void Tangle::updateFromSerialized(const string &data)
{
    stringstream ss(data);
    string line;
    Transaction lastTx;

    while (getline(ss, line))
    {
        stringstream linestream(line);
        Transaction newTx;

        // Read basic transaction details
        getline(linestream, newTx.data.transaction_id, ',');
        string timestamp;
        getline(linestream, timestamp, ',');
        // Assuming timestamp is a UNIX timestamp string
        newTx.data.timestamp = std::stoll(timestamp);

        getline(linestream, newTx.data.sender, ',');
        getline(linestream, newTx.data.receiver, ',');

        string amount, price;
        getline(linestream, amount, ',');
        newTx.data.amount = stod(amount);
        getline(linestream, newTx.data.unit, ',');
        getline(linestream, price, ',');
        newTx.data.price_per_unit = stod(price);
        getline(linestream, newTx.data.currency, ',');

        string weight;
        getline(linestream, weight, ',');
        newTx.metadata.cumulative_weight = stoi(weight);

        string lastUpdated;
        getline(linestream, lastUpdated, ',');
        newTx.metadata.lastUpdated = std::stoll(lastUpdated);

        getline(linestream, newTx.metadata.signature1, ',');
        getline(linestream, newTx.metadata.signature2, ',');

        getline(linestream, newTx.metadata.checksum, ',');

        // Deserialize previous transactions
        string prevTxStr;
        getline(linestream, prevTxStr, ',');
        prevTxStr = prevTxStr.substr(1, prevTxStr.size() - 2); // Remove brackets
        stringstream prevTxStream(prevTxStr);
        string prevTx;
        while (getline(prevTxStream, prevTx, ','))
        {
            newTx.data.parents.push_back(prevTx);
        }
        // DebugScopedLock<std::mutex> lock(tangleMutex, "tangleMutex", 10);
        std::unique_lock lock(tangleMutex);
        // Add the new transaction to the Tangle
        transactions[newTx.data.transaction_id] = newTx;
        lastTx = newTx; // Keep track of the last transaction for cumulative weight updates
    }

    cout << "[LOG] Tangle updated from received data." << endl;
    cout << "[LOG] Last transaction ID: " << lastTx.data.transaction_id << endl;
    cout << "[LOG] Last transaction timestamp: " << lastTx.data.timestamp << endl;
}

// function to check if tx is already present in the tangle
bool Tangle::transactionPresent(Transaction &tx)
{
    // DebugScopedLock<std::mutex> lock(tangleMutex, "tangleMutex", 10);
    std::shared_lock lock(tangleMutex);

    // Check if the transaction ID exists in the Tangle's transactions
    return transactions.find(tx.data.transaction_id) != transactions.end();
}

bool Tangle::transactionNeedsUpdate(Transaction &tx)
{
    // lock_guard<mutex> lock(tangleMutex);
    // DebugScopedLock<std::mutex> lock(tangleMutex, "tangleMutex", 10);
    std::shared_lock lock(tangleMutex);

    auto it = transactions.find(tx.data.transaction_id);
    if (it != transactions.end())
    {

        if (!tx.metadata.signature2.empty() && it->second.metadata.signature2.empty())
        {
            // If the transaction is double signed, it needs to be updated
            return true;
        }

        // Check if the new transaction is newer than the existing one
        std::cout << "[TANGLE][UPDATE] received last update time: "
                  << tx.metadata.lastUpdated << " existing last update time: "
                  << it->second.metadata.lastUpdated << std::endl;
        return tx.metadata.lastUpdated > it->second.metadata.lastUpdated;
    }
    // Transaction not found, so it needs to be added
    return true;
}

// Function to only update the metadata of a transaction in the Tangle
int Tangle::updateTransaction(Transaction &tx, int no_lock)
{
    // only update cumulative weight and last updated time
    // lock_guard<mutex> lock(tangleMutex);
    // DebugScopedLock<std::mutex> lock(tangleMutex, "tangleMutex", 10);
    if (no_lock == 0)
        std::unique_lock lock(tangleMutex);

    auto it = transactions.find(tx.data.transaction_id);
    if (it != transactions.end())
    {
        Transaction &existing = it->second;

        auto dataEquals = [](const tx_data &a, const tx_data &b) -> bool
        {
            if (a.transaction_id != b.transaction_id)
            {
                std::cerr << "[MISMATCH] transaction_id: " << a.transaction_id << " vs " << b.transaction_id << std::endl;
                return false;
            }
            if (a.sender != b.sender)
            {
                std::cerr << "[MISMATCH] sender: " << a.sender << " vs " << b.sender << std::endl;
                return false;
            }
            if (a.receiver != b.receiver)
            {
                std::cerr << "[MISMATCH] receiver: " << a.receiver << " vs " << b.receiver << std::endl;
                return false;
            }
            if (a.amount != b.amount)
            {
                std::cerr << "[MISMATCH] amount: " << a.amount << " vs " << b.amount << std::endl;
                return false;
            }
            if (a.unit != b.unit)
            {
                std::cerr << "[MISMATCH] unit: " << a.unit << " vs " << b.unit << std::endl;
                return false;
            }
            if (a.price_per_unit != b.price_per_unit)
            {
                std::cerr << "[MISMATCH] price_per_unit: " << a.price_per_unit << " vs " << b.price_per_unit << std::endl;
                return false;
            }
            if (a.currency != b.currency)
            {
                std::cerr << "[MISMATCH] currency: " << a.currency << " vs " << b.currency << std::endl;
                return false;
            }
            if (a.timestamp != b.timestamp)
            {
                std::cerr << "[MISMATCH] timestamp: " << a.timestamp << " vs " << b.timestamp << std::endl;
                return false;
            }

            if (a.parents.size() != b.parents.size())
            {
                std::cerr << "[MISMATCH] parents.size(): " << a.parents.size() << " vs " << b.parents.size() << std::endl;
                return false;
            }
            for (size_t i = 0; i < a.parents.size(); ++i)
            {
                if (a.parents[i] != b.parents[i])
                {
                    std::cerr << "[MISMATCH] parents[" << i << "]: " << a.parents[i] << " vs " << b.parents[i] << std::endl;
                    return false;
                }
            }
            return true;
        };

        if (!dataEquals(existing.data, tx.data))
        {
            // Data update not allowed.
            std::cerr << "[TANGLE][ERROR] Data mismatch for transaction: " << tx.data.transaction_id << std::endl;
            return 0;
        }

        bool metadataChanged = false;

        if (existing.metadata.signature2.empty() && !tx.metadata.signature2.empty())
        {
            existing.metadata.signature2 = tx.metadata.signature2;
            existing.metadata.status = TransactionStatus::APPROVED;
            // existing.metadata.lastUpdated = tx.metadata.lastUpdated;
            metadataChanged = true;
        }

        if (existing.metadata.lastUpdated < tx.metadata.lastUpdated)
        {
            existing.metadata.consensusTimestamp = tx.metadata.consensusTimestamp;
            existing.metadata.consensusDuration = tx.metadata.consensusDuration;
            existing.metadata.verificationTimestamp = tx.metadata.verificationTimestamp;
            existing.metadata.verificationDuration = tx.metadata.verificationDuration;
            existing.metadata.lastUpdated = tx.metadata.lastUpdated;
            metadataChanged = true;
        }

        if (metadataChanged)
        {

            std::cout << "[TANGLE] Transaction updated in Tangle: " << tx.data.transaction_id << endl;
            return 1; // Transaction metadata updated
        }
        else
        {
            std::cout << "[TANGLE][WARN] Transaction not updated in Tangle: " << tx.data.transaction_id
                      << ". Existing transaction is newer." << endl;

            return 0;
        }
    }
    else
    {
        cerr << "[TANGLE][ERROR] Transaction not found in Tangle for update: " << tx.data.transaction_id << endl;
        return -1;
    }
}

int Tangle::updateTransactionMetrics(Transaction &tx)
{

    std::unique_lock lock(tangleMutex);

    auto it = transactions.find(tx.data.transaction_id);
    if (it != transactions.end())
    {
        Transaction &existing = it->second;

        existing.metadata.tsaDuration = tx.metadata.tsaDuration;
        existing.metadata.completionDuration = tx.metadata.completionDuration;

        return 1;
    }
    else
    {
        cerr << "[TANGLE][ERROR] Transaction not found in Tangle for update: " << tx.data.transaction_id << endl;
        return -1;
    }
}

void Tangle::calculatePropagationMetrics(Transaction &tx)
{
    // Calculate propagation delay and average propagation delay from hops
    if (tx.metadata.hops.size() >= 2)
    {
        int64_t firstHopTime = tx.metadata.hops.front().first;
        int64_t lastHopTime = tx.metadata.hops.back().first;
        
        tx.metadata.propagationDelay = lastHopTime - firstHopTime;
        tx.metadata.avgPropagationDelay = tx.metadata.propagationDelay / (static_cast<int64_t>(tx.metadata.hops.size()) - 1);
        
        std::cout << "[TANGLE][PROPAGATION] Transaction " << tx.data.transaction_id
                  << " propagation delay: " << tx.metadata.propagationDelay
                  << " ms, avg per hop: " << tx.metadata.avgPropagationDelay << " ms (hops: " 
                  << tx.metadata.hops.size() << ")" << std::endl;
    }
    else if (tx.metadata.hops.size() == 1)
    {
        // Only one hop, no propagation yet
        tx.metadata.propagationDelay = 0;
        tx.metadata.avgPropagationDelay = 0;
    }
}

void Tangle::addChild(const std::string& parent_id, const std::string& child_id) {
    std::unique_lock lock(tangleMutex);
    children[parent_id].insert(child_id);
    auto it = transactions.find(parent_id);
    if (it != transactions.end()) {
        it->second.metadata.reference_count = static_cast<int>(children[parent_id].size());
    }
    updateDescendantWeight(parent_id, 1);
}

std::vector<std::string> Tangle::getChildren(const std::string& tx_id, TransactionStatus min_status) const {
    std::shared_lock lock(tangleMutex);
    std::vector<std::string> result;
    auto it = children.find(tx_id);
    if (it != children.end()) {
        for (const auto& child_id : it->second) {
            auto txIt = transactions.find(child_id);
            if (txIt != transactions.end() &&
                static_cast<int>(txIt->second.metadata.status) >= static_cast<int>(min_status)) {
                result.push_back(child_id);
            }
        }
    }
    return result;
}

bool Tangle::isTip(const std::string& tx_id) const {
    std::shared_lock lock(tangleMutex);
    auto it = children.find(tx_id);
    return (it == children.end()) || it->second.empty();
}

std::vector<std::string> Tangle::getTips(TransactionStatus min_status) const {
    std::shared_lock lock(tangleMutex);
    std::vector<std::string> tips;
    for (const auto& [tx_id, tx] : transactions) {
        if (tx_id == "genesis") continue;
        auto cit = children.find(tx_id);
        bool no_children = (cit == children.end() || cit->second.empty());
        bool status_ok = (static_cast<int>(tx.metadata.status) >= static_cast<int>(min_status));
        if (no_children && status_ok) {
            tips.push_back(tx_id);
        }
    }
    return tips;
}

bool Tangle::addVote(const std::string& tx_id, const std::string& voter_id) {
    std::unique_lock lock(tangleMutex);
    auto it = transactions.find(tx_id);
    if (it == transactions.end()) return false;
    if (it->second.metadata.voted_by.insert(voter_id).second) {
        it->second.metadata.votes++;
        it->second.metadata.lastUpdated = timeNow();
        if (it->second.metadata.status != TransactionStatus::FINAL &&
            it->second.metadata.votes >= consensusThreshold) {
            finalizeTransaction(tx_id);
        }
        return true; // new vote recorded
    }
    return false; // duplicate vote
}

void Tangle::finalizeTransaction(const std::string& tx_id) {
    auto it = transactions.find(tx_id);
    if (it == transactions.end()) return;
    it->second.metadata.status = TransactionStatus::FINAL;
    int64_t now = timeNow();
    it->second.metadata.consensusTimestamp = now;
    it->second.metadata.consensusDuration = now - it->second.data.timestamp;
    std::cout << "[TANGLE][CONSENSUS] Transaction " << tx_id
              << " reached FINAL status. Votes: " << it->second.metadata.votes
              << ", Duration: " << it->second.metadata.consensusDuration << " ms" << std::endl;
}

void Tangle::updateDescendantWeight(const std::string& tx_id, int increment) {
    auto txIt = transactions.find(tx_id);
    if (txIt == transactions.end()) return;

    std::queue<std::string> q;
    for (const auto& parent : txIt->second.data.parents) {
        q.push(parent);
    }
    while (!q.empty()) {
        std::string current = q.front(); q.pop();
        auto it = transactions.find(current);
        if (it != transactions.end()) {
            it->second.metadata.cumulative_weight += increment;
            it->second.metadata.lastUpdated = timeNow();
            for (const auto& grandparent : it->second.data.parents) {
                q.push(grandparent);
            }
        }
    }
}