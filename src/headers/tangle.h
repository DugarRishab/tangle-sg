#ifndef TANGLE_H
#define TANGLE_H
#include "transaction.h"
#include <unordered_map>
#include <mutex>
#include <string>

using namespace std;

class Tangle
{
public:
    Transaction addNewTransaction(Transaction& tx);
    void addTransaction(const Transaction& tx);
    void updateCumulativeWeight(const std::string& transaction_id);
    std::string serialize(); // Converts the Tangle to a string format
    static std::string serializeTransaction(const Transaction& tx); // Serializes a single transaction
    static string serializeTransactionData(const Transaction &tx); // Serializes transaction data
    std::unordered_map<std::string, Transaction> deserialize(const std::string &data); // Converts a string format back to Tangle's transactions
    static Transaction deserializeTransaction(const std::string& data); // Deserializes a single transaction
    void updateFromSerialized(const std::string& data); // Updates Tangle from serialized string
    std::unordered_map<std::string, Transaction> transactions;

private:
    std::mutex tangleMutex; // Mutex to protect shared Tangle access
};
#endif