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

    tx.metadata.hops.emplace_back({timeNow(), uid_str}); // Add hop with current timestamp and UID

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

        tx.metadata.hops.emplace_back({timeNow(), uid_str}); // Add hop with current timestamp and UID
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

    // std::cout << "[LOG][WEIGHT] current cumulative weight for transaction: "
    //   << transaction_id << " is " << transactions[transaction_id].metadata.cumulative_weight << std::endl;
    transactions[transaction_id].metadata.cumulative_weight += weightIncrement;
    std::cout << "[TANGLE] Cumulative weight updated for transaction: "
              << ". New cumulative weight: " << transactions[transaction_id].metadata.cumulative_weight << std::endl;

    if (transactions.find(transaction_id) == transactions.end())
    {
        std::cerr << "[ERROR] Transaction " << transaction_id << " not found in Tangle." << std::endl;
        return;
    }
    transactions[transaction_id].metadata.lastUpdated = timeNow();
    // Update cumulative weight for all parents

    updateCumulativeWeightOfParents(transactions[transaction_id].data.parents, weightIncrement);
}

string Tangle::serializeTransactionData(const Transaction &tx)
{

    stringstream ss;
    ss << tx.data.transaction_id << ","
       << tx.data.timestamp << ","
       << tx.data.sender << ","
       << tx.data.receiver << ","
       << fixed << setprecision(17) << tx.data.amount << ","
       << tx.data.unit << ","
       << fixed << setprecision(17) << tx.data.price_per_unit << ","
       << tx.data.currency;

    // Serialize previous transactions
    ss << ",[";
    for (size_t i = 0; i < tx.data.parents.size(); i++)
    {
        ss << tx.data.parents[i];
        if (i < tx.data.parents.size() - 1)
            ss << ",";
    }
    ss << "]";

    return ss.str();
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

string Tangle::serializeTransaction(const Transaction &tx)
{

    stringstream ss;
    ss << tx.data.transaction_id << ","
       << tx.data.timestamp << ","
       << tx.data.sender << ","
       << tx.data.receiver << ","
       << fixed << setprecision(17) << tx.data.amount << ","
       << tx.data.unit << ","
       << fixed << setprecision(17) << tx.data.price_per_unit << ","
       << tx.data.currency << ","
       << tx.metadata.cumulative_weight << ","
       << tx.metadata.lastUpdated << ","
       << tx.metadata.signature1 << ","
       << tx.metadata.signature2 << ","
       << tx.metadata.checksum << ","
       << tx.metadata.consensusTimestamp << ","
       << tx.metadata.consensusDuration << ","
       << tx.metadata.verificationTimestamp << ","
       << tx.metadata.verificationDuration << ","
       << tx.metadata.powDuration << ","
       << tx.metadata.tsaDuration << ","
       << tx.metadata.completionDuration;

    // Serialize parents
    ss << ",[";
    for (size_t i = 0; i < tx.data.parents.size(); i++)
    {
        ss << tx.data.parents[i];
        if (i < tx.data.parents.size() - 1)
            ss << ",";
    }
    ss << "]";

    // serialize hops
    ss << ",[";
    for (size_t i = 0; i < tx.metadata.hops.size(); i++)
    {
        ss << "(" << tx.metadata.hops[i].first << "," << tx.metadata.hops[i].second << ")";
        if (i < tx.metadata.hops.size() - 1)
            ss << ",";
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

Transaction Tangle::deserializeTransaction(const string &data)
{
    Transaction tx;
    stringstream ss(data);
    string line;

    getline(ss, tx.data.transaction_id, ',');
    getline(ss, line, ',');
    tx.data.timestamp = std::stoll(line);
    getline(ss, tx.data.sender, ',');
    getline(ss, tx.data.receiver, ',');
    getline(ss, line, ',');
    tx.data.amount = std::stod(line);
    getline(ss, tx.data.unit, ',');
    getline(ss, line, ',');
    tx.data.price_per_unit = std::stod(line);
    getline(ss, tx.data.currency, ',');

    getline(ss, line, ',');
    tx.metadata.cumulative_weight = std::stoi(line);

    getline(ss, line, ',');
    tx.metadata.lastUpdated = std::stoll(line);

    getline(ss, tx.metadata.signature1, ',');
    getline(ss, tx.metadata.signature2, ',');

    getline(ss, tx.metadata.checksum, ',');

    getline(ss, line, ',');
    tx.metadata.consensusTimestamp = std::stoll(line);

    getline(ss, line, ',');
    tx.metadata.consensusDuration = std::stoll(line);

    getline(ss, line, ',');
    tx.metadata.verificationTimestamp = std::stoll(line);

    getline(ss, line, ',');
    tx.metadata.verificationDuration = std::stoll(line);

    getline(ss, line, ',');
    tx.metadata.powDuration = std::stoll(line);

    getline(ss, line, ',');
    tx.metadata.tsaDuration = std::stoll(line);

    getline(ss, line, ',');
    tx.metadata.completionDuration = std::stoll(line);

    // Deserialize previous transactions
    string prevTxStr;
    getline(ss, prevTxStr);

    // Remove brackets and split by semicolon
    prevTxStr = prevTxStr.substr(1, prevTxStr.size() - 2); // Remove brackets
    stringstream prevTxStream(prevTxStr);
    string prevTx;

    while (getline(prevTxStream, prevTx, ','))
        tx.data.parents.push_back(prevTx);

    // Deserialize hops
    string hopsStr;
    getline(ss, hopsStr);
    hopsStr = hopsStr.substr(1, hopsStr.size() - 2); // Remove brackets
    stringstream hopsStream(hopsStr);
    string hop;

    while (getline(hopsStream, hop, ','))
    {
        size_t pos = hop.find(',');
        if (pos != string::npos)
        {
            int64_t timestamp = std::stoll(hop.substr(0, pos));
            string peerId = hop.substr(pos + 1);
            tx.metadata.hops.emplace_back(timestamp, peerId);
        }
    }

    return tx;
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
        if (it->second.metadata.lastUpdated < tx.metadata.lastUpdated)
        {

            if (it->second.metadata.signature2.empty())
            {
                it->second.metadata.signature2 = tx.metadata.signature2;
                it->second.metadata.lastUpdated = tx.metadata.lastUpdated;
            }

            if (!it->second.metadata.signature2.empty() && !it->second.metadata.signature1.empty())
            {
                // allow weight updates only if both signatures are present
                int weightIncrement = tx.metadata.cumulative_weight - it->second.metadata.cumulative_weight;
                if (weightIncrement > 0)
                {
                    it->second.metadata.cumulative_weight += weightIncrement;
                    it->second.metadata.lastUpdated = tx.metadata.lastUpdated;
                    updateCumulativeWeightOfParents(it->second.data.parents, weightIncrement);
                }
            }

            std::cout << "[TANGLE] Transaction updated in Tangle: " << tx.data.transaction_id << endl;

            return 1; // Transaction updated
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
