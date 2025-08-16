#ifndef TRANSACTION_H
#define TRANSACTION_H
#include <string>
#include <vector>

using namespace std;

struct tx_data
{
    std::string transaction_id;
    std::string sender;
    std::string receiver;
    double amount;
    std::string unit;
    double price_per_unit;
    std::string currency;
    int64_t timestamp;
    int timestampInt;
    std::vector<std::string> parents;
};


struct tx_metadata
{
    int64_t lastUpdated;
    int cumulative_weight;
    std::string signature1; // sender’s sig
    std::string signature2; // receiver’s sig
    std::string checksum; // hash of Tx->ata for integrity

    int64_t consensusTimestamp; // timestamp when consensus is reached
    int64_t consensusDuration;
    int64_t verificationTimestamp; // timestamp when transaction is verified
    int64_t verificationDuration; // time taken to verify the transaction
    int64_t powDuration; // time taken to perform PoW
    int64_t tsaDuration;
    int64_t completionDuration;

    std::vector <std::pair<int64_t, std::string>> hops;
};


struct Transaction
{
    tx_data data;
    tx_metadata metadata;
    
};

std::string signTransaction(const std::string &msg);
bool verifyTransaction(const std::string &msg, const std::string &sig_b64, const std::string &uid);

#endif