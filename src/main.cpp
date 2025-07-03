#include <iostream>
#include <vector>
#include <unordered_map>
#include <random>
#include <thread>
#include <chrono>
#include "headers/pow.h"
#include "headers/tsa.h"
#include "headers/transaction.h"
#include "headers/tangle.h"
#include "headers/network.h"

#include <vector>

using namespace std;
using namespace chrono;

void simulateSmartMeter(Tangle &tangle)
{
    random_device rd;
    mt19937 gen(rd());
    uniform_real_distribution<> energyDist(0.5, 5.0);
    uniform_real_distribution<> priceDist(0.1, 0.5);

    vector<int> timearray;
    int i = 0;
    while (i < 1000)
    {
        i++;
        vector<string> parents = selectTips(tangle);

        Transaction newTx;
        newTx.transaction_id = "tx" + to_string(rand());
        newTx.timestamp = to_string(time(nullptr));
        newTx.timestampInt = static_cast<int>(time(nullptr));
        newTx.sender = "Meter_001";
        newTx.receiver = "Grid";
        newTx.amount = energyDist(gen);
        newTx.unit = "kWh";
        newTx.price_per_unit = priceDist(gen);
        newTx.currency = "USD";
        newTx.previous_transactions = parents;
        newTx.proof_of_work = "Pending";

        cout << "[LOG] Generating new transaction: " << newTx.transaction_id << " at:" << newTx.timestamp << endl;
        auto start = chrono::high_resolution_clock::now();
        // Compute PoW for new transaction
        newTx.proof_of_work = performPoW(newTx.transaction_id, 2);

        // Update cumulative weight for selected tips
        for (const string &parent : parents)
        {
            tangle.updateCumulativeWeight(parent);
        }

        // Add the new transaction
        tangle.addTransaction(newTx);
        auto end = chrono::high_resolution_clock::now();
        auto elapsed = duration<double, milli>(end - start).count();

        cout << "[LOG] Transaction " << newTx.transaction_id << " added to Tangle." << endl;
        cout << "Time elapsed:" << elapsed << " ms" << endl;

        broadcastTangle(tangle);
        this_thread::sleep_for(chrono::seconds(10));
    }
}


int main()
{
    
    Tangle tangle;

    // Create genesis transaction (without PoW initially)
    Transaction genesis = {"tx0", "2025-03-11T12:00:00Z", 00000000011, "node_A", "node_B", 5.0, "kWh", 0.12, "USD", {}, {}, 1, ""};

    // // Compute PoW separately
    // genesis.proof_of_work = performPoW(genesis.transaction_id, 2);
    // tangle.addTransaction(genesis);

    // thread serverThread(startServer, ref(tangle));
    // thread simulationThread(simulateSmartMeter, ref(tangle));

    // THREAD 1: HTTP & WS server
    auto serverWrapper = [&]()
    {
        try
        {
            std::cout << "[THREAD] startServer() beginning…\n";
            startServer(std::ref(tangle)); // your existing function
            std::cout << "[THREAD] startServer() returned!\n";
        }
        catch (const std::exception &ex)
        {
            std::cerr << "[ERROR] startServer threw: " << ex.what() << "\n";
        }
        catch (...)
        {
            std::cerr << "[ERROR] startServer threw unknown exception\n";
        }
    };

    // THREAD 2: Simulation loop
    auto simWrapper = [&]()
    {
        try
        {
            std::cout << "[THREAD] simulateSmartMeter() beginning…\n";
            simulateSmartMeter(std::ref(tangle)); // your existing function
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
