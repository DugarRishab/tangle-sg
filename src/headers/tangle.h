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
    Transaction getTransaction(const std::string &transaction_id); // Retrieves a transaction by its ID
    std::unordered_map<std::string, Transaction> getAllTransactions();
    Transaction addNewTransaction(Transaction &tx);
    int addTransaction( Transaction& tx, int update = 0);
    // REMOVED: updateCumulativeWeightOfParents and updateCumulativeWeight
    // Use updateDescendantWeight (BFS) instead.
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

    // Consensus threshold configuration
    void setConsensusThreshold(int threshold) { consensusThreshold = threshold; }
    int getConsensusThreshold() const { return consensusThreshold; }

    // Propagation metrics calculation
    void calculatePropagationMetrics(Transaction &tx);

    // NEW: children index maintenance
    void addChild(const std::string& parent_id, const std::string& child_id);
    std::vector<std::string> getChildren(const std::string& tx_id, TransactionStatus min_status = TransactionStatus::PROPOSED) const;

    // NEW: tip detection using reference_count
    bool isTip(const std::string& tx_id) const;
    std::vector<std::string> getTips(TransactionStatus min_status = TransactionStatus::APPROVED) const;

    // NEW: BFT finality
    bool addVote(const std::string& tx_id, const std::string& voter_id);
    void finalizeTransaction(const std::string& tx_id);

    // NEW: update cumulative_weight (descendant count) when a child is added
    void updateDescendantWeight(const std::string& tx_id, int increment = 1);

private:
    std::unordered_map<std::string, Transaction> transactions;
    // NEW: children index — parent_id -> set of child tx_ids
    std::unordered_map<std::string, std::unordered_set<std::string>> children;
    mutable std::shared_mutex tangleMutex; // Mutex to protect shared Tangle access
    int consensusThreshold = 3; // Default: tx reaches consensus when votes >= threshold
};
#endif