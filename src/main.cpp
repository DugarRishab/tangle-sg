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
#include <fstream>
#include <cstdlib> // getenv, setenv, rand
#include <ctime>   // time_t, time()

using namespace std;
using namespace chrono;
namespace fs = std::filesystem;


const std::string KEYFILE_PRIV = "keys/node.key";
const std::string KEYFILE_PUB = "keys/node.pub";
const std::string HMAC_SECRET_FILE = "secret/hmac_secret.txt";



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
    while (i < 1000)
    {
        i++;
        
        Transaction newTx;

        auto peerList = peers.getPeerList();

        if (peerList.empty())
        {
            std::cout << "[WARN] Still no peers—waiting before generating transactions…\n";
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue; // skip this iteration until we have at least one
        }

        vector<string> parents = selectTips(tangle);

        // receiver is selected randomly from the list of active peers
        string receiver = peers.getPeerList()[rand() % peers.getPeerList().size()].id;                                                                                                                                    
        
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
        performPoW(newTx.data.transaction_id, 2);

        // Add the new transaction
        Transaction finalTx = tangle.addNewTransaction(newTx);

        cout << "[LOG] Generating new transaction: " << finalTx.data.transaction_id << " at:" << finalTx.data.timestamp << endl;

        auto end = chrono::high_resolution_clock::now();
        auto elapsed = duration<double, milli>(end - start).count();

        cout << "[LOG] Transaction " << finalTx.data.transaction_id << " added to Tangle." << endl;
        cout << "Time elapsed:" << elapsed << " ms" << endl;

        net.broadcastTransaction(finalTx);
        this_thread::sleep_for(chrono::seconds(10));
    }
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
    if (setenv("PK_b64",  pk_b64.c_str(), 1) != 0 ||
        setenv("SK_b64",  sk_b64.c_str(), 1) != 0 ||
        setenv("UID", uid.c_str(),    1) != 0) {
        std::cerr << "Failed to set environment variables\n";
    } else {
        std::cout << "Exported PK_b64, SK_b64, and UID to environment.\n";
    }

    // try
    // {
    //     std::string secret = loadOrCreateHMACSecret(HMAC_SECRET_FILE);
    //     // Now getenv("HMAC_SECRET") will return this hex string.
    // }
    // catch (const std::exception &ex)
    // {
    //     std::cerr << "[FATAL] " << ex.what() << "\n";
    //     return 1;
    // }

    // TODO FOR DOCKNET: using a standard Ed25519 tool
    // ed25519 - keygen
    // -- output - public node_X.pub -> 32bit public key
    // -- output - private node_X.key -> 64bit private key

    // then -> UID = Base58(PublicKey)

    Tangle tangle;

    // Create genesis transaction (without PoW initially)

    tx_data genesisData = {
        "0", // transaction_id
        "0",     // sender
        "0",     // receiver
        0,          // amount
        "kWh",        // unit
        0,         // price_per_unit
        "INR",        // currency
        time(nullptr),// timestamp
        static_cast<int>(time(nullptr)), // timestampInt
        {}            // parents (empty for genesis)
    };
    tx_metadata genesisMetadata = {
        time(nullptr), // lastUpdated
        0,             // cumulative_weight
        "",            // signature1
        "",            // signature2
        ""             // checksum
    };
    Transaction genesis = {genesisData, genesisMetadata};
    {
        
        tangle.addTransaction(genesis);
    }

    Network net(9000, tangle);

    Peers pd(9001, tangle, net); // 9000 is for WS, 9001 is for UDP
    // THREAD 1: WS server
    auto serverWrapper = [&]()
    {
        try
        {
            pd.findPeers(5); // Discover up to 5 peers
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
            simulateSmartMeter(tangle, pd, net); // your existing function
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
