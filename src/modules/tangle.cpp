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
#include <openssl/sha.h>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include "../headers/debug_lock.h"
using namespace std;

std::shared_mutex tangleMutex;

Transaction Tangle::getTransaction(std::string &transaction_id)
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
// TODO: make it recursive for each parent until genesis
void Tangle::updateCumulativeWeightOfParents(vector<std::string> &parents, int weightIncrement)
{
    for (const auto &parent : parents)
    {
        if (transactions.find(parent) != transactions.end())
        {
            transactions[parent].metadata.cumulative_weight += weightIncrement;
            transactions[parent].metadata.lastUpdated = timeNow();

            updateCumulativeWeightOfParents(transactions[parent].data.parents, weightIncrement);
        }
        else
        {
            std::cerr << "[TANGLE][ERROR] Parent transaction " << parent << " not found in Tangle." << std::endl;
        }
    }
}

void Tangle::updateCumulativeWeight(const std::string &transaction_id, int weightIncrement)
{

    // lock_guard<mutex> lock(tangleMutex);
    // DebugScopedLock<std::mutex> lock(tangleMutex, "tangleMutex", 10);

    std::unique_lock lock(tangleMutex);

    if (transactions.find(transaction_id) == transactions.end())
    {
        std::cerr << "[ERROR] Transaction " << transaction_id << " not found in Tangle." << std::endl;
        return;
    }

    const char *uidEnv = std::getenv("UID");
    std::string uid = uidEnv ? uidEnv : "";

    // Add the node to the weightMap if not already present
    if (transactions[transaction_id].metadata.weightMap.find(uid) == transactions[transaction_id].metadata.weightMap.end())
    {
        transactions[transaction_id].metadata.weightMap.insert(uid);
        transactions[transaction_id].metadata.cumulative_weight++;
        transactions[transaction_id].metadata.lastUpdated = timeNow();

        std::cout << "[TANGLE] Cumulative weight updated for transaction: "
                  << ". New cumulative weight: " << transactions[transaction_id].metadata.cumulative_weight << std::endl;

        // Update cumulative weight for all parents

        updateCumulativeWeightOfParents(transactions[transaction_id].data.parents, weightIncrement);
    }
    else
    {
        std::cout << "[TANGLE] Node " << uid << " has already added weight to transaction " << transaction_id << ". No update performed." << std::endl;
    }
}


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
            // existing.metadata.lastUpdated = tx.metadata.lastUpdated;
            metadataChanged = true;
        }

        if (!existing.metadata.signature1.empty() && !existing.metadata.signature2.empty())
        {
            int weightIncrement = 0;
            for (const auto &nodeId : tx.metadata.weightMap)
            {
                if (existing.metadata.weightMap.find(nodeId) == existing.metadata.weightMap.end())
                {
                    existing.metadata.weightMap.insert(nodeId);
                    existing.metadata.cumulative_weight++;
                    weightIncrement++;
                }
            }

            if (weightIncrement > 0)
            {

                // existing.metadata.lastUpdated = tx.metadata.lastUpdated;
                // update parents' cumulative weight
                updateCumulativeWeightOfParents(existing.data.parents, weightIncrement);
                metadataChanged = true;
            }
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

        existing.metadata.powDuration = tx.metadata.powDuration;
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