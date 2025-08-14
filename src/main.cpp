#include <iostream>
#include <vector>
#include <unordered_map>
#include <random>
#include <thread>
#include <chrono>
#include <filesystem>
#include <sodium.h>
#include "headers/pow.h"
#include "headers/tsa.h"
#include "headers/transaction.h"
#include "headers/tangle.h"
#include "headers/network.h"
#include "headers/peers2.h"
#include "headers/utils.h"
#include "headers/peerDiscovery.h"
#include <fstream>
#include <cstdlib> // getenv, setenv, rand
#include <ctime>   // time_t, time()
#include <iomanip> // for std::quoted

using namespace std;
using namespace chrono;
namespace fs = std::filesystem;

const std::string KEYFILE_PRIV = "keys/node.key";
const std::string KEYFILE_PUB = "keys/node.pub";
const std::string HMAC_SECRET_FILE = "secret/hmac_secret.txt";

// Join a vector of strings by a delimiter
static std::string join(const std::vector<std::string> &v, char delim = ';')
{
    std::ostringstream oss;
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (i)
            oss << delim;
        oss << v[i];
    }
    return oss.str();
}

void saveTangleToCSV(std::unordered_map<std::string, Transaction> transactions,
                     const std::string &filename)
{
    std::ofstream out(filename);
    if (!out.is_open())
    {
        std::cerr << "Failed to open CSV file for writing: "
                  << filename << std::endl;
        return;
    }

    // 1) Write header
    out << "transaction_id,sender,receiver,amount,unit,"
           "price_per_unit,currency,timestamp,parents,"
           "cumulative_weight,lastUpdated,signature1,signature2\n";

    // 2) Write each transaction
    for (auto const item : transactions)
    {
        const Transaction &tx = item.second;

        // Format parents as a semicolon-separated list
        std::string parents = join(tx.data.parents, ';');

        auto &d = tx.data;
        auto &m = tx.metadata;

        // CSV-safe quoting for any field that may contain commas
        out << std::quoted(d.transaction_id) << ','
            << std::quoted(d.sender) << ','
            << std::quoted(d.receiver) << ','
            << std::setprecision(17) // full precision
            << d.amount << ','
            << std::quoted(d.unit) << ','
            << std::setprecision(17)
            << d.price_per_unit << ','
            << std::quoted(d.currency) << ','
            << d.timestamp << ','
            << std::quoted(parents) << ','
            << m.cumulative_weight << ','
            << m.lastUpdated << ','
            << std::quoted(m.signature1) << ','
            << std::quoted(m.signature2) << '\n';
    }

    out.close();
    std::cout << "Tangle saved to CSV: " << filename << std::endl;
}

void compareTransactions(const Transaction &a, const Transaction &b)
{
    std::cout << std::boolalpha; // print bools as true/false

    // Compare the core data fields
    if (a.data.transaction_id != b.data.transaction_id)
        std::cout << "[MISMATCH] transaction_id: "
                  << a.data.transaction_id << " != " << b.data.transaction_id << "\n";

    if (a.data.sender != b.data.sender)
        std::cout << "[MISMATCH] sender: "
                  << a.data.sender << " != " << b.data.sender << "\n";

    if (a.data.receiver != b.data.receiver)
        std::cout << "[MISMATCH] receiver: "
                  << a.data.receiver << " != " << b.data.receiver << "\n";

    if (a.data.amount != b.data.amount)
        std::cout << "[MISMATCH] amount: "
                  << a.data.amount << " != " << b.data.amount << "\n";

    if (a.data.unit != b.data.unit)
        std::cout << "[MISMATCH] unit: "
                  << a.data.unit << " != " << b.data.unit << "\n";

    if (a.data.price_per_unit != b.data.price_per_unit)
        std::cout << "[MISMATCH] price_per_unit: "
                  << a.data.price_per_unit << " != " << b.data.price_per_unit << "\n";

    if (a.data.currency != b.data.currency)
        std::cout << "[MISMATCH] currency: "
                  << a.data.currency << " != " << b.data.currency << "\n";

    if (a.data.timestamp != b.data.timestamp)
        std::cout << "[MISMATCH] timestamp: "
                  << a.data.timestamp << " != " << b.data.timestamp << "\n";

    // If you track parent IDs as a vector, you can compare lengths or each element:
    if (a.data.parents.size() != b.data.parents.size())
    {
        std::cout << "[MISMATCH] number of parents: "
                  << a.data.parents.size() << " != " << b.data.parents.size() << "\n";
    }
    else
    {
        for (size_t i = 0; i < a.data.parents.size(); ++i)
        {
            if (a.data.parents[i] != b.data.parents[i])
            {
                std::cout << "[MISMATCH] parent[" << i << "]: "
                          << a.data.parents[i] << " != " << b.data.parents[i] << "\n";
            }
        }
    }

    // Compare metadata
    if (a.metadata.cumulative_weight != b.metadata.cumulative_weight)
        std::cout << "[MISMATCH] cumulative_weight: "
                  << a.metadata.cumulative_weight << " != " << b.metadata.cumulative_weight << "\n";

    if (a.metadata.lastUpdated != b.metadata.lastUpdated)
        std::cout << "[MISMATCH] lastUpdated: "
                  << a.metadata.lastUpdated << " != " << b.metadata.lastUpdated << "\n";

    // If you store signatures, you can compare them too:
    if (a.metadata.signature1 != b.metadata.signature1)
        std::cout << "[MISMATCH] signature1: "
                  << a.metadata.signature1 << " != " << b.metadata.signature1 << "\n";
    if (a.metadata.signature2 != b.metadata.signature2)
        std::cout << "[MISMATCH] signature2: "
                  << a.metadata.signature2 << " != " << b.metadata.signature2 << "\n";

    //
}

void testSignaturePipeline(const Transaction &originalTx)
{

    std::string fullSerialized = Tangle::serializeTransaction(originalTx);

    // Step 4: Deserialize the transaction
    Transaction deserializedTx = Tangle::deserializeTransaction(fullSerialized);

    compareTransactions(originalTx, deserializedTx);

    // Step 5: Serialize tx_data again after deserialization
    std::string deserializedSerializedData = Tangle::serializeTransactionData(deserializedTx);
    // std::cout << "Deserialized tx_data serialized: " << deserializedSerializedData << "\n";

    // Step 6: Verify the signature
    bool isValid = verifyTransaction(deserializedSerializedData, deserializedTx.metadata.signature1, deserializedTx.data.sender);

    std::cout << "Signature verification: " << (isValid ? "SUCCESS" : "FAILURE") << "\n";

    // Optional: Debug mismatch
    // if (!isValid)
    // {
    //     std::cout << "[Debug] Original:      " << originalTx << "\n";
    //     std::cout << "[Debug] Deserialized:  " << deserializedSerializedData << "\n";
    // }
}

void simulateSmartMeter(Tangle &tangle, Peers &peers, Network &net)
{
    // Simulate a smart meter generating transactions
    // This function will run in a separate thread to simulate real-time data generation
    cout << "[LOG] Starting smart meter simulation..." << endl;

    // Generate random transactions and broadcast them to the Tangle
    // This simulates a smart meter generating energy transactions

    random_device rd;
    mt19937 gen(rd());
    uniform_real_distribution<> energyDist(0.5, 5.0);
    uniform_real_distribution<> priceDist(0.1, 0.5);

    vector<int> timearray;
    int i = 0;
    while (i < 10)
    {
        std::cout << "[LOG][SIMULATOR] Generating transaction " << i + 1 << "..." << std::endl;
        i++;

        Transaction newTx;

        if (peers.countPeers() == 0)
        {
            std::cout << "[WARN] Still no peers—waiting before generating transactions…\n";
            std::this_thread::sleep_for(std::chrono::seconds(5));
            i--;
            continue; // skip this iteration until we have at least one
        }
        std::cout << "[SIMULATOR][TSA] Starting..." << std::endl;
        vector<string> parents = selectTips(tangle, 2);
        std::cout << "[SIMULATOR][TSA] Completed." << std::endl;
        // receiver is selected randomly from the list of active peers
        std::cout << "[SIMULATOR][RECEIVER] Completed." << std::endl;
        string receiver = peers.getRandomPeer().id;
        std::cout << "[SIMULATOR][RECEIVER] Completed." << std::endl;

        newTx.data.timestamp = time(nullptr);
        newTx.data.timestampInt = static_cast<int>(time(nullptr));
        newTx.data.sender = getenv("UID"); // Use UID from environment variable
        newTx.data.receiver = receiver;
        newTx.data.amount = energyDist(gen);
        newTx.data.unit = "kWh";
        newTx.data.price_per_unit = priceDist(gen);
        newTx.data.currency = "USD";
        newTx.data.parents = parents;
        newTx.metadata.lastUpdated = time(nullptr);
        newTx.metadata.cumulative_weight = 0; // Initialize cumulative weight

        auto start = chrono::high_resolution_clock::now();
        // Compute PoW for new transaction

        // Add the new transaction
        Transaction finalTx = tangle.addNewTransaction(newTx);

        std::cout << "[SIMULATOR][POW] starting...";
        performPoW(finalTx.data.transaction_id);
        std::cout << "[SIMULATOR][POW] over";

        cout << "[LOG][SIMULATOR] Generated new transaction: "
             << finalTx.data.transaction_id << " at:" << finalTx.data.timestamp << endl;

        auto end = chrono::high_resolution_clock::now();
        auto elapsed = duration<double, milli>(end - start).count();

        cout << "[LOG][SIMULATOR] Transaction " << finalTx.data.transaction_id << " added to Tangle." << endl;
        cout << "[LOG][SIMULATOR] Time elapsed:" << elapsed << " ms" << endl;

        // auto finalDataSerialized = Tangle::serializeTransaction(finalTx);
        // auto finalDataDeSerialized = Tangle::deserializeTransaction(finalDataSerialized);

        // check if finalDataDeSerialized matches with finalTx and print where the mismatch is

        // testSignaturePipeline(finalTx);

        net.broadcastTransaction(finalTx);
        cout << "[LOG][SIMULATOR] Transaction broadcasted completed." << endl;
        this_thread::sleep_for(chrono::seconds(30));
    }

    std::this_thread::sleep_for(std::chrono::seconds(300));
    saveTangleToCSV(tangle.getAllTransaction(), "tangle_state.csv");

    throw std::runtime_error("[LOG] Tangle state saved to tangle_state.txt. Exiting simulation.");
}

std::string loadOrCreateHMACSecret(const std::string &path)
{
    namespace fs = std::filesystem;
    std::string secret;

    // 1) Try to read existing file
    if (fs::exists(path))
    {
        std::ifstream in(path, std::ios::in | std::ios::binary);
        if (in && std::getline(in, secret) && !secret.empty())
        {
            // got it!
        }
        else
        {
            std::cerr << "[WARN] Could not read HMAC secret from " << path
                      << " (empty or unreadable). Generating new.\n";
            secret.clear();
        }
    }

    // 2) If missing or invalid, generate & save
    if (secret.empty())
    {
        // crypto_auth_KEYBYTES is the recommended length for HMAC-SHA256 keys
        std::vector<unsigned char> key(crypto_auth_KEYBYTES);
        randombytes_buf(key.data(), key.size());

        // hex-encode
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (auto byte : key)
        {
            oss << std::setw(2) << static_cast<int>(byte);
        }
        secret = oss.str();

        // ensure directory exists
        fs::path p(path);
        if (p.has_parent_path())
            fs::create_directories(p.parent_path());

        // write atomically
        std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out)
        {
            throw std::runtime_error("Failed to open HMAC secret file for writing: " + path);
        }
        out << secret << "\n";
        out.close();
        std::cout << "[INFO] Generated and saved new HMAC secret to " << path << "\n";
    }

    // 3) Export into environment
    if (setenv("HMAC_SECRET", secret.c_str(), /*overwrite=*/1) != 0)
    {
        throw std::runtime_error("Failed to set HMAC_SECRET environment variable");
    }

    return secret;
}

int main()
{
    std::cout.setf(std::ios::unitbuf);

    if (sodium_init() < 0)
    {
        std::cerr << "Failed to init libsodium\n";
        return 1;
    }

    fs::create_directories("keys");

    std::vector<unsigned char> pk(crypto_sign_PUBLICKEYBYTES);
    std::vector<unsigned char> sk(crypto_sign_SECRETKEYBYTES);

    // 1. Try to load existing keys
    auto sk_load = read_file(KEYFILE_PRIV);
    auto pk_load = read_file(KEYFILE_PUB);

    if (sk_load.size() == sk.size() && pk_load.size() == pk.size())
    {
        // Successful load
        sk = std::move(sk_load);
        pk = std::move(pk_load);
        std::cout << "Loaded existing keypair.\n";
    }
    else
    {
        // Generate and persist
        crypto_sign_keypair(pk.data(), sk.data());
        write_file(KEYFILE_PRIV, sk);
        write_file(KEYFILE_PUB, pk);
        // Restrict permissions on private key file (POSIX)
        fs::permissions(KEYFILE_PRIV,
                        fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::replace);
        std::cout << "Generated new keypair and saved to disk.\n";
    }

    // Derive UID as Base64 of public key
    // Derive Base64-encoded keys and UID
    std::string pk_b64 = to_base64(pk.data(), pk.size());
    std::string sk_b64 = to_base64(sk.data(), sk.size());
    std::string uid = pk_b64; // UID = Base64(pubkey)

    std::cout << "Node UID: " << uid << std::endl;

    // Export UID as environment variable for child processes at runtime
    // Export as environment variables for runtime
    if (setenv("PK_b64", pk_b64.c_str(), 1) != 0 ||
        setenv("SK_b64", sk_b64.c_str(), 1) != 0 ||
        setenv("UID", uid.c_str(), 1) != 0)
    {
        std::cerr << "Failed to set environment variables\n";
    }
    else
    {
        std::cout << "Exported PK_b64, SK_b64, and UID to environment.\n";
    }

    Tangle tangle;

    Peers peers;

    Network net(9000, tangle, peers); // 9000 is for WS, 9001 is for UDP

    PeerDiscovery pd(9001, net, peers); // 9001 is for UDP discovery

    // Create genesis transaction (without PoW initially)

    tx_data genesisData = {
        "genesis",                       // transaction_id
        "0",                             // sender
        "0",                             // receiver
        0,                               // amount
        "kWh",                           // unit
        0,                               // price_per_unit
        "INR",                           // currency
        time(nullptr),                   // timestamp
        static_cast<int>(time(nullptr)), // timestampInt
        {}                               // parents (empty for genesis)
    };
    tx_metadata genesisMetadata = {
        time(nullptr),   // lastUpdated
        0,               // cumulative_weight
        "genesis_sign1", // signature1
        "genesis_sign2", // signature2
        ""               // checksum
    };
    Transaction genesis = {genesisData, genesisMetadata};
    {
        tangle.addTransaction(genesis);
    }

    // THREAD 1: WS server
    auto serverWrapper = [&]()
    {
        try
        {
            pd.start();
        }
        catch (std::exception &ex)
        {
            std::cerr << "Fatal: " << ex.what() << "\n";
            // return 1;
        }
    };

    // THREAD 2: Simulation loop
    auto simWrapper = [&]()
    {
        try
        {
            std::cout << "[THREAD] simulateSmartMeter() beginning…\n";
            simulateSmartMeter(tangle, peers, net); // your existing function
            std::cout << "[THREAD] simulateSmartMeter() returned!\n";
        }
        catch (const std::exception &ex)
        {
            std::cerr << "[ERROR] simulateSmartMeter threw: " << ex.what() << "\n";
        }
        catch (...)
        {
            std::cerr << "[ERROR] simulateSmartMeter threw unknown exception\n";
        }
    };

    // Launch both
    std::thread serverThread(serverWrapper);
    std::thread simulationThread(simWrapper);

    // Join the threads to keep the main function active
    serverThread.join();
    simulationThread.join();

    return 0;
}
