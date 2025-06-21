#include "../headers/tangle.h"
#include "../headers/transaction.h"
#include <iostream>
#include <sstream>

using namespace std;

void Tangle::addTransaction(const Transaction& tx) {
    transactions[tx.transaction_id] = tx;
}
void Tangle::updateCumulativeWeight(const std::string& transaction_id) {
    transactions[transaction_id].cumulative_weight++;
}

// Serializes the Tangle's transactions into a string format
string Tangle::serialize() const {
    stringstream ss;

    for (const auto& [txID, tx] : transactions) { // transactions is the Tangle's storage map
        ss << tx.transaction_id << ","
           << tx.timestamp << ","
           << tx.sender << ","
           << tx.receiver << ","
           << tx.amount << ","
           << tx.unit << ","
           << tx.price_per_unit << ","
           << tx.currency << ","
           << tx.cumulative_weight << ","
           << tx.proof_of_work;

        // Serialize previous transactions
        ss << ",[";
        for (size_t i = 0; i < tx.previous_transactions.size(); i++) {
            ss << tx.previous_transactions[i];
            if (i < tx.previous_transactions.size() - 1) ss << ";";
        }
        ss << "]";

        // Serialize validating transactions
        ss << ",[";
        for (size_t i = 0; i < tx.validating_transactions.size(); i++) {
            ss << tx.validating_transactions[i];
            if (i < tx.validating_transactions.size() - 1) ss << ";";
        }
        ss << "]\n";
    }
    return ss.str();
}

// Updates the Tangle from a serialized string
void Tangle::updateFromSerialized(const string& data) {
    stringstream ss(data);
    string line;
    Transaction lastTx;

    while (getline(ss, line)) {
        stringstream linestream(line);
        Transaction newTx;

        // Read basic transaction details
        getline(linestream, newTx.transaction_id, ',');
        getline(linestream, newTx.timestamp, ',');
        getline(linestream, newTx.sender, ',');
        getline(linestream, newTx.receiver, ',');
        
        string amount, price;
        getline(linestream, amount, ',');
        newTx.amount = stod(amount);
        getline(linestream, newTx.unit, ',');
        getline(linestream, price, ',');
        newTx.price_per_unit = stod(price);
        getline(linestream, newTx.currency, ',');
        
        string weight;
        getline(linestream, weight, ',');
        newTx.cumulative_weight = stoi(weight);
        getline(linestream, newTx.proof_of_work, ',');

        // Deserialize previous transactions
        string prevTxStr, validTxStr;
        getline(linestream, prevTxStr, ',');
        prevTxStr = prevTxStr.substr(1, prevTxStr.size() - 2); // Remove brackets
        stringstream prevTxStream(prevTxStr);
        string prevTx;
        while (getline(prevTxStream, prevTx, ';')) {
            newTx.previous_transactions.push_back(prevTx);
        }

        // Deserialize validating transactions
        getline(linestream, validTxStr, ',');
        validTxStr = validTxStr.substr(1, validTxStr.size() - 2); // Remove brackets
        stringstream validTxStream(validTxStr);
        string validTx;
        while (getline(validTxStream, validTx, ';')) {
            newTx.validating_transactions.push_back(validTx);
        }

        // Add the new transaction to the Tangle
        transactions[newTx.transaction_id] = newTx;
        lastTx = newTx; // Keep track of the last transaction for cumulative weight updates
    }
    
    cout << "[LOG] Tangle updated from received data." << endl;
    cout << "[LOG] Last transaction ID: " << lastTx.transaction_id << endl;
    cout << "[LOG] Last transaction timestamp: " << lastTx.timestamp << endl;
}

