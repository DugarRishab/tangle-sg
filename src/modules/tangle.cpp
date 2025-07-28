#include "../headers/tangle.h"
#include "../headers/transaction.h"
#include <iostream>
#include <sstream>
#include <ctime>
#include <openssl/sha.h>

using namespace std;

std::mutex tangleMutex;

string computeChecksum(const string &data)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char *)data.c_str(), data.size(), hash);

    stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        ss << hex << setw(2) << setfill('0') << (int)hash[i];
    }
    return ss.str();
}

// Verifies that the received data has a correct checksum
bool verifyChecksum(const string &data, const string &receivedChecksum)
{
    string calculatedChecksum = computeChecksum(data);
    return calculatedChecksum == receivedChecksum;
}

void Tangle::addNewTransaction(const Transaction &tx)
{
    // calculate checksum
    string txData = serializeTransactionData(tx);
    // assing uid to the tra
    tx.data.transaction_id = to_string(tx.data.timestamp) + "_" + tx.data.sender + "_" + tx.data.receiver;

    // Ignoring checksum because signatiures are used for integrity
    // string checksum = computeChecksum(txData);
    // Update metadata
    // tx.metadata.checksum = checksum;

    // cummulative weight is not updated here. it is done after double signing
    tx.metadata.cumulative_weight = 0; // Initialize cumulative weight

    //sign the transaction
    tx.metadata.signature1 = signTransaction(txData);

    // Lock the mutex to protect shared Tangle access
    lock_guard<mutex> lock(tangleMutex);
    transactions[tx.data.transaction_id] = tx;

}
void Tangle::addTransaction(const Transaction &tx)
{
    // Lock the mutex to protect shared Tangle access
    lock_guard<mutex> lock(tangleMutex);
    // TODO: check if tx already exists


    transactions[tx.data.transaction_id] = tx;
}
void Tangle::updateCumulativeWeight(const std::string &transaction_id)
{
    transactions[transaction_id].metadata.cumulative_weight++;
    transactions[transaction_id].metadata.lastUpdated = time(nullptr);
    // Update cumulative weight for all parents
    // TODO: make it recursive for each parent until genesis
    for (const auto &parent : transactions[transaction_id].data.parents)
    {
        if (transactions.find(parent) != transactions.end())
        {
            transactions[parent].metadata.cumulative_weight++;
            transactions[parent].metadata.lastUpdated = time(nullptr);
        }
    }
}

string Tangle::serializeTransactionData(const Transaction &tx) const
{
    stringstream ss;
    ss << tx.data.transaction_id << ","
       << tx.data.timestamp << ","
       << tx.data.sender << ","
       << tx.data.receiver << ","
       << tx.data.amount << ","
       << tx.data.unit << ","
       << tx.data.price_per_unit << ","
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
string Tangle::serialize() const
{
    stringstream ss;

    for (const auto &[txID, tx] : transactions)
    { // transactions is the Tangle's storage map
        ss << serializeTransaction(tx) << ";";
    }
    return ss.str();
}

string Tangle::serializeTransaction(const Transaction &tx) const
{
    stringstream ss;
    ss << tx.data.transaction_id << ","
       << tx.data.timestamp << ","
       << tx.data.sender << ","
       << tx.data.receiver << ","
       << tx.data.amount << ","
       << tx.data.unit << ","
       << tx.data.price_per_unit << ","
       << tx.data.currency << ","
       << tx.metadata.cumulative_weight << ","
       << tx.metadata.lastUpdated << ","
       << tx.metadata.signature1 << ","
       << tx.metadata.signature2 << ","
       << tx.metadata.checksum;

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

// Returns the Tangle's transactions in a deserialized format
std::unordered_map<std::string, Transaction> Tangle::deserialze(const string &data)
{
    stringstream ss(data);
    string line;
    
    string txString;
    std::unordered_map<std::string, Transaction> transactions;

    // split by semicolon
    while (getline(ss, txString, ';')){

       
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

    // Deserialize previous transactions
    string prevTxStr;
    getline(ss, prevTxStr);
    
    // Remove brackets and split by semicolon
    prevTxStr = prevTxStr.substr(1, prevTxStr.size() - 2); // Remove brackets
    stringstream prevTxStream(prevTxStr);
    string prevTx;

    while (getline(prevTxStream, prevTx, ','))
        tx.data.parents.push_back(prevTx);

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

        // Add the new transaction to the Tangle
        transactions[newTx.data.transaction_id] = newTx;
        lastTx = newTx; // Keep track of the last transaction for cumulative weight updates
    }

    cout << "[LOG] Tangle updated from received data." << endl;
    cout << "[LOG] Last transaction ID: " << lastTx.data.transaction_id << endl;
    cout << "[LOG] Last transaction timestamp: " << lastTx.data.timestamp << endl;
}
