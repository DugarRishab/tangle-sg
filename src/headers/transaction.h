#ifndef TRANSACTION_H
#define TRANSACTION_H
#include <string>
#include <vector>
struct tx_data
{
    std::string transaction_id;
    std::string sender;
    std::string receiver;
    double amount;
    std::string unit;
    double price_per_unit;
    std::string currency;
    time_t timestamp;
    int timestampInt;
    std::vector<std::string> parents;
};

struct tx_metadata
{
    time_t lastUpdated;
    int cumulative_weight;
    std::string signature1; // sender’s sig
    std::string signature2; // receiver’s sig

    std::string checksum; // hash of Tx->ata for integrity
};

struct Transaction
{
    tx_data data;
    tx_metadata metadata;
    
};

#endif