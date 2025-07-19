#include "network.h"
#include "tangle.h"
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <arpa/inet.h>
#include <ctime>
#include "peers2.h"

#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

using namespace std;

using WsClient = websocketpp::client<websocketpp::config::asio_client>;
using ConnectionHdl = websocketpp::connection_hdl;
using MessagePtr = websocketpp::config::asio_client::message_type::ptr;


extern mutex tangleMutex;


// Computes SHA-256 checksum of the data
string computeChecksum(const string &data)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char *)data.c_str(), data.size(), hash);

    stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        ss << hex << setw(2) << setfill('0') << (int)hash[i];
    }
    return ss.str();
}

// Verifies that the received data has a correct checksum
bool verifyChecksum(const string &data, const string &receivedChecksum)
{
    string calculatedChecksum = computeChecksum(data);
    return calculatedChecksum == receivedChecksum;
}

void printLastTransaction(Tangle &tangle)
{
    Transaction lastTx;
    string latestTimestamp = "0";

    for (const auto &pair : tangle.transactions)
    {
        if (pair.second.timestamp > latestTimestamp)
        {
            latestTimestamp = pair.second.timestamp;
            lastTx = pair.second;
        }
    }

    time_t txTime = static_cast<time_t>(stoll(latestTimestamp));
    time_t currentTime = time(nullptr);
    double elapsedSeconds = difftime(currentTime, txTime);

    // Convert seconds into human-readable format (e.g., minutes/seconds)
    int minutes = static_cast<int>(elapsedSeconds) / 60;
    int seconds = static_cast<int>(elapsedSeconds);

    // Print the last transaction details
    if (!latestTimestamp.empty())
    {
        cout << "[LOG] Last transaction received: ID = " << lastTx.transaction_id
             << ", Sender = " << lastTx.sender << endl
             << ", Receiver = " << lastTx.receiver << endl
             << ", Amount = " << lastTx.amount << " " << lastTx.unit << endl
             << ", Price per unit = " << lastTx.price_per_unit << " " << lastTx.currency << endl
             << ", PoW = " << lastTx.proof_of_work << endl
             << ", Time since creation = " << seconds << " sec ago"
             << endl;
    }
    else
    {
        cout << "[LOG] No transactions found in updated Tangle." << endl;
    }
}

void handleTCPClient(std::string receivedData, Tangle &tangle)
{
    if (!receivedData.empty())
    {
        cout << "[LOG] Received Tangle update" << endl;
        string receivedChecksum = receivedData.substr(receivedData.find_last_of(" ") + 1);
        string actualData = receivedData.substr(0, receivedData.find_last_of(" "));

        if (verifyChecksum(actualData, receivedChecksum))
        {
            // tangle.updateFromSerialized(actualData);
            {
                lock_guard<mutex> lock(tangleMutex);
                tangle.updateFromSerialized(actualData);
            }
            cout << "[LOG] Tangle update verified and applied." << endl;
            printLastTransaction(tangle);
        }
        else
        {
            cerr << "[ERROR] Data corruption detected!" << endl;
        }
    }
    else
    {
        cerr << "[ERROR] Received empty data from TCP client." << endl;
    }
}

void setupMessageReceiver(WsClient &client, Tangle &tangle)
{
    client.set_message_handler(
        [&](ConnectionHdl hdl, MessagePtr msg)
        {
            // 1) Identify which peer sent it (if you’ve mapped hdl → peerId):
            // std::string peerId = peerMap[hdl];
            
            // 2) Grab payload and optionally its opcode:
            auto payload = msg->get_payload();
            auto opcode = msg->get_opcode(); // text or binary

            // 3) Process it:
            if (opcode == websocketpp::frame::opcode::text)
            {
                // std::cout << "[recv] From peer: " << payload << "\n";
                handleTCPClient(payload, tangle);
            }
            else
            {
                std::cout << "[recv] Received non‑text frame\n";
            }
        });
}

void broadcastTangle(const Tangle &tangle)
{
    string tangleData = tangle.serialize();
    string checksum = computeChecksum(tangleData);
    string message = tangleData + " " + checksum;

    for (auto peer : activePeers)
    {
        peer.client->send(peer.hdl, message, websocketpp::frame::opcode::text);
    }
}