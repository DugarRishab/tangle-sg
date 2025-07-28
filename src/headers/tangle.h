#ifndef TANGLE_H
#define TANGLE_H
#include "transaction.h"
#include <unordered_map>
class Tangle {
public:
    void addNewTransaction(const Transaction& tx);
    void updateCumulativeWeight(const std::string& transaction_id);
    std::string serialize() const; // Converts the Tangle to a string format
    std::string serializeTransaction(const Transaction& tx) const; // Serializes a single transaction
    std::unordered_map<std::string, Transaction> deserialze(const std::string& data); // Converts a string format back to Tangle's transactions
    Transaction deserializeTransaction(const std::string& data); // Deserializes a single transaction
    void updateFromSerialized(const std::string& data); // Updates Tangle from serialized string
    std::unordered_map<std::string, Transaction> transactions;

private:
    std::mutex tangleMutex; // Mutex to protect shared Tangle access
};
#endif