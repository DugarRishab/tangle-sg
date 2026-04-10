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
#include <curl/curl.h>
#include "headers/TeeBuf.h"

#include "headers/telemetry.h"

using namespace std;
using namespace chrono;
namespace fs = std::filesystem;

const std::string KEYFILE_PRIV = "keys/node.key";
const std::string KEYFILE_PUB = "keys/node.pub";
const std::string HMAC_SECRET_FILE = "secret/hmac_secret.txt";

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_simulation_complete{false};
static std::atomic<int64_t> g_last_activity{0};

void updateLastActivity() {
    g_last_activity.store(timeNow());
}

void signalHandler(int signo)
{
    // safe: set atomic flag to false
    g_running.store(false);
}

// Global reference to tangle for signal handler access
static Tangle* g_tangle_ptr = nullptr;
static std::string g_snapshot_filename = "tangle_state.csv";

void saveSnapshotOnShutdown()
{
    if (g_tangle_ptr != nullptr) {
        std::cout << "[SHUTDOWN] Saving final snapshot...\n";
        saveTangleToCSV(g_tangle_ptr->getAllTransactions(), g_snapshot_filename + ".final");
    }
}

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

bool saveTangleToCSVWithRetry(std::unordered_map<std::string, Transaction> transactions,
                     const std::string &filename, int maxRetries = 3)
{
    for (int attempt = 0; attempt < maxRetries; ++attempt)
    {
        std::ofstream out(filename);
        if (!out.is_open())
        {
            std::cerr << "[SAVE][ATTEMPT " << (attempt + 1) << "/" << maxRetries 
                      << "] Failed to open CSV file for writing: " << filename << std::endl;
            if (attempt < maxRetries - 1)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1)));
                continue;
            }
            return false;
        }

        // 1) Write header
        out << "transaction_id,sender,receiver,amount,unit,"
               "price_per_unit,currency,timestamp,parents,"
               "cumulative_weight,lastUpdated,signature1,signature2,"
               "consensusTimestamp,consensusDuration,propagationDelay,avgPropagationDelay\n";

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
                << std::quoted(m.signature2) << ','
                << m.consensusTimestamp << ','
                << m.consensusDuration << ','
                << m.propagationDelay << ','
                << m.avgPropagationDelay << '\n';
        }

        out.close();
        if (!out.fail())
        {
            std::cout << "[SAVE] Tangle saved to CSV: " << filename << std::endl;
            return true;
        }
        
        std::cerr << "[SAVE][ATTEMPT " << (attempt + 1) << "/" << maxRetries 
                  << "] Write failed, retrying..." << std::endl;
        if (attempt < maxRetries - 1)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1)));
        }
    }
    
    std::cerr << "[SAVE][ERROR] All " << maxRetries << " attempts failed to save CSV." << std::endl;
    return false;
}

void saveTangleToCSV(std::unordered_map<std::string, Transaction> transactions,
                     const std::string &filename)
{
    saveTangleToCSVWithRetry(transactions, filename, 3);
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

    std::string fullSerialized = serializeTransaction(originalTx);

    // Step 4: Deserialize the transaction
    Transaction deserializedTx = deserializeTransaction(fullSerialized);

    compareTransactions(originalTx, deserializedTx);

    // Step 5: Serialize tx_data again after deserialization
    std::string deserializedSerializedData = serializeTransactionData(deserializedTx);
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

    char *txCountEnv = getenv("TX_COUNT");
    int tx_count = txCountEnv ? atoi(txCountEnv) : 10;

    char *txDelayEnv = getenv("TX_DELAY");
    int tx_delay = txDelayEnv ? atoi(txDelayEnv) : 30;

    // Auto-save interval (default: 5 minutes = 300 seconds)
    const char *autosaveEnv = getenv("AUTOSAVE_INTERVAL");
    int autosaveInterval = autosaveEnv ? atoi(autosaveEnv) : 300;
    int64_t lastAutosave = timeNow();

    // Simulation timeout (default: 1 hour = 3600 seconds)
    const char *timeoutEnv = getenv("SIMULATION_TIMEOUT");
    int simulationTimeout = timeoutEnv ? atoi(timeoutEnv) : 3600;
    int64_t simulationStart = timeNow();

    vector<int> timearray;
    int i = 0;
    while (i < tx_count)
    {
        // Check simulation timeout
        int64_t elapsed = (timeNow() - simulationStart) / 1000; // convert to seconds
        if (elapsed > simulationTimeout) {
            std::cout << "[SIMULATOR][TIMEOUT] Simulation timeout reached after " 
                      << elapsed << " seconds. Stopping." << std::endl;
            break;
        }

        // Periodic auto-save
        if (autosaveInterval > 0) {
            int64_t timeSinceLastSave = (timeNow() - lastAutosave) / 1000;
            if (timeSinceLastSave >= autosaveInterval) {
                std::cout << "[SIMULATOR][AUTOSAVE] Auto-saving tangle state..." << std::endl;
                saveTangleToCSV(tangle.getAllTransactions(), "tangle_state_autosave.csv");
                lastAutosave = timeNow();
            }
        }

        std::cout << "[SIMULATOR] Generating transaction " << i + 1 << "..." << std::endl;
        i++;

        Transaction newTx;

        if (peers.countPeers() == 0)
        {
            std::cout << "[SIMULATOR][WARN] Still no peers—waiting before generating transactions…\n";
            std::this_thread::sleep_for(std::chrono::seconds(5));
            i--;
            continue; // skip this iteration until we have at least one
        }
        // std::cout << "[SIMULATOR][TSA] Starting..." << std::endl;
        int64_t tsaStartTime = timeNow();
        vector<string> parents = selectTips(tangle, 2);
        int64_t tsaEndTime = timeNow();
        // std::cout << "[SIMULATOR][TSA] Completed." << std::endl;
        // receiver is selected randomly from the list of active peers
        // std::cout << "[SIMULATOR][RECEIVER] Completed." << std::endl;
        // string receiver = peers.getRandomPeer().id;
        // std::cout << "[SIMULATOR][RECEIVER] Completed." << std::endl;

        // std::cout << "[SIMULATOR][" << std::this_thread::get_id() << "] before getRandomPeer" << std::endl
                //   << std::flush;
        string receiver = peers.getRandomPeer().id;
        // std::cout << "[SIMULATOR][" << std::this_thread::get_id() << "] after getRandomPeer" << std::endl
        //           << std::flush;

        newTx.data.timestamp = timeNow(); // Use current time in seconds
        
        newTx.data.sender = getenv("UID"); // Use UID from environment variable
        newTx.data.receiver = receiver;
        newTx.data.amount = energyDist(gen);
        newTx.data.unit = "kWh";
        newTx.data.price_per_unit = priceDist(gen);
        newTx.data.currency = "USD";
        newTx.data.parents = parents;
        newTx.metadata.lastUpdated = timeNow();
        newTx.metadata.cumulative_weight = 0; // Initialize cumulative weight
        newTx.metadata.tsaDuration = tsaEndTime - tsaStartTime;
        newTx.metadata.powDuration = 0; // Placeholder, will be set after PoW
        
        // Compute PoW for new transaction

        // Add the new transaction
        newTx = tangle.addNewTransaction(newTx);
        
        // Update last activity timestamp
        updateLastActivity();
        
        // std::cout << "[SIMULATOR][POW] starting..." << std::endl;
        int64_t powStartTime = timeNow();
        performPoW(newTx.data.transaction_id);
        int64_t powEndTime = timeNow();
        
        // std::ut << "[SIMULATOR][POW] over" << std::endl;

        cout << "[SIMULATOR] Generated new transaction: "
             << newTx.data.transaction_id << " at:" << newTx.data.timestamp << endl;

        auto end = timeNow();
        auto elapsed_tx = end - tsaStartTime;

        newTx.metadata.completionDuration = elapsed_tx;
        newTx.metadata.powDuration = powEndTime - powStartTime;

        tangle.updateTransactionMetrics(newTx); // Update the transaction in the Tangle

        newTx = tangle.getTransaction(newTx.data.transaction_id);

        cout << "[SIMULATOR] Transaction " << newTx.data.transaction_id << " added to Tangle." << endl;
        cout << "[SIMULATOR] TSA Duration: " << newTx.metadata.tsaDuration << " ms" << endl;
        cout << "[SIMULATOR] PoW Duration: " << newTx.metadata.powDuration << " ms" << endl;
        cout << "[SIMULATOR] Total Time elapsed:" << elapsed_tx << " ms" << endl;

        // auto finalDataSerialized = serializeTransaction(newTx);
        // auto finalDataDeSerialized = deserializeTransaction(finalDataSerialized);

        // check if finalDataDeSerialized matches with newTx and print where the mismatch is

        // testSignaturePipeline(newTx);

        net.broadcastTransaction(newTx);
        
        // Update last activity after broadcast
        updateLastActivity();
        
        cout << "[SIMULATOR] Transaction broadcasted completed." << endl;
        this_thread::sleep_for(chrono::seconds(tx_delay));
    }

    const char* waitPeriodEnv = getenv("WAIT_PERIOD");
    int waitPeriod = waitPeriodEnv ? atoi(waitPeriodEnv) : 300;
    std::this_thread::sleep_for(std::chrono::seconds(waitPeriod));

    // Wait for queues to drain, but with idle timeout check
    const char *idleTimeoutEnv = getenv("IDLE_TIMEOUT");
    int idleTimeout = idleTimeoutEnv ? atoi(idleTimeoutEnv) : 600; // default 10 minutes
    int64_t lastCheck = timeNow();
    
    while(!peers.allQueuesEmpty()){
        // Check if we've been idle too long
        int64_t idleTime = (timeNow() - g_last_activity.load()) / 1000;
        if (idleTime > idleTimeout) {
            std::cout << "[SIMULATOR][IDLE_TIMEOUT] No activity for " << idleTime 
                      << " seconds. Exiting queue wait." << std::endl;
            break;
        }
        
        // additional sleep in case the queueis still not empty after wait time.
        std::cout << "[SIMULATOR] QUEUES are not empty yet! Waiting for 1 more minute. (Idle: " 
                  << idleTime << "s)" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }

    saveTangleToCSV(tangle.getAllTransactions(), "tangle_state.csv");
    
    // Mark simulation as complete
    g_simulation_complete.store(true);

    // throw std::runtime_error("[SIMULATOR] Tangle state saved to tangle_state.txt. Exiting simulation.");
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

    // Open log file (append or truncate as you wish)
    std::ofstream logfile("program_log.txt", std::ios::out | std::ios::trunc);
    if (!logfile.is_open())
    {
        std::cerr << "Failed to open log file\n";
        return 1;
    }

    // Save the current cout buffer and set our tee buffer
    std::streambuf *oldCoutBuf = std::cout.rdbuf();
    TeeTimestampBuf tb(oldCoutBuf, logfile.rdbuf());
    std::cout.rdbuf(&tb);

    // install signal handlers early to handle graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

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
    
    // Set consensus threshold from environment variable (default: 3)
    const char *consensusThresholdEnv = getenv("CONSENSUS_THRESHOLD");
    int consensusThreshold = consensusThresholdEnv ? atoi(consensusThresholdEnv) : 3;
    tangle.setConsensusThreshold(consensusThreshold);
    std::cout << "[MAIN] Consensus threshold set to: " << consensusThreshold << std::endl;
    
    // Register tangle for graceful shutdown snapshot
    g_tangle_ptr = &tangle;

    Peers peers;

    Network net(9000, tangle, peers); // 9000 is for WS, 9001 is for UDP

    PeerDiscovery pd(9001, net, peers); // 9001 is for UDP discovery

    curl_global_init(CURL_GLOBAL_DEFAULT);

    startTelemetryCollector(1000);

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
        
        {}                               // parents (empty for genesis)
    };
    tx_metadata genesisMetadata = {
        time(nullptr),   // lastUpdated
        {},              // weightMap   
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
            // add delay of 15seconds to allow server to start
            std::this_thread::sleep_for(std::chrono::seconds(15));
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
    simulationThread.join();

    stopTelemetryCollector();
    auto metrics_snapshot = snapshotAndClearMetrics();

    std::string nodeId = uid;

    const char *endpoint_env = getenv("TELEMETRY_ENDPOINT");
    const std::string endpoint = endpoint_env ? endpoint_env : "http://172.25.0.10:8000/api/telemetry";

    const char *run_env = getenv("RUN_ID");
    int runId = run_env ? atoi(run_env) : 0;

    bool ok = sendTelemetry(endpoint, nodeId, tangle, peers, metrics_snapshot, runId);
    if (!ok)
    {
        std::cerr << "Telemetry upload failed\n";
    }
    else
    {
        std::cout << "Telemetry uploaded successfully\n";
    }

    std::cout << "[MAIN] Simulation finished. Network and peer discovery remain active.\n";
    std::cout << "[MAIN] Press Ctrl+C to stop and shutdown cleanly.\n";

    // Main loop with idle timeout check
    const char *mainIdleTimeoutEnv = getenv("IDLE_TIMEOUT");
    int mainIdleTimeout = mainIdleTimeoutEnv ? atoi(mainIdleTimeoutEnv) : 600;
    int64_t simulationEndTime = timeNow();
    
    while (g_running.load())
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Check idle timeout if simulation is complete
        if (g_simulation_complete.load()) {
            int64_t idleTime = (timeNow() - simulationEndTime) / 1000;
            if (idleTime > mainIdleTimeout) {
                std::cout << "[MAIN][IDLE_TIMEOUT] No activity for " << idleTime 
                          << " seconds after simulation. Shutting down.\n";
                g_running.store(false);
                break;
            }
        }
    }

    std::cout << "[MAIN] Shutdown requested — saving final snapshot and stopping modules...\n";
    
    // Save final snapshot on shutdown
    saveSnapshotOnShutdown();

    curl_global_cleanup();

    serverThread.join();

    // flush and restore
    std::cout.flush();
    std::cout.rdbuf(oldCoutBuf);
    logfile.close();

    return 0;
}
