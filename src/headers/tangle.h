#ifndef TANGLE_H
#define TANGLE_H
#include "transaction.h"
#include <unordered_map>
#include <mutex>
#include <string>
#include <shared_mutex>

using namespace std;

class Tangle
{
public:
    Transaction getTransaction(std::string &transaction_id); // Retrieves a transaction by its ID
    std::unordered_map<std::string, Transaction> getAllTransactions();
    Transaction addNewTransaction(Transaction &tx);
    int addTransaction( Transaction& tx, int update = 0);
    void updateCumulativeWeightOfParents(vector<std::string> &parents, int weightIncrement = 1);
    void updateCumulativeWeight(const std::string& transaction_id, int weightIncrement = 1);
    std::string serialize(); // Converts the Tangle to a string format
    // static std::string serializeTransaction(const Transaction& tx); // Serializes a single transaction
    // static string serializeTransactionData(const Transaction &tx); // Serializes transaction data
    std::unordered_map<std::string, Transaction> deserialize(const std::string &data); // Converts a string format back to Tangle's transactions
    // static Transaction deserializeTransaction(const std::string& data); // Deserializes a single transaction
    void updateFromSerialized(const std::string& data); // Updates Tangle from serialized string
    
    bool transactionPresent( Transaction& tx);
    bool transactionNeedsUpdate(Transaction &tx);
    int updateTransaction(Transaction &tx, int no_lock = 0); // Updates the metadata of a transaction in the Tangle
    int updateTransactionMetrics(Transaction &tx); // Updates the metrics of a transaction in the Tangle


private:
    std::unordered_map<std::string, Transaction> transactions;
    std::shared_mutex tangleMutex; // Mutex to protect shared Tangle access
};
#endif