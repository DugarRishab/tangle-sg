#include "../headers/network.h"
#include "../headers/tangle.h"
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
#include <string>
#include <unordered_map>
#include <arpa/inet.h>
#include <ctime>
#include "../headers/peers2.h"
#include "../headers/pow.h"
#include "../headers/transaction.h"
#include <json/json.h>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/client.hpp>

using namespace std;

using WsClient = websocketpp::client<websocketpp::config::asio_client>;
using ConnectionHdl = websocketpp::connection_hdl;
using MessagePtr = websocketpp::config::asio_client::message_type::ptr;
using WsServer = websocketpp::server<websocketpp::config::asio>;


Network::Network(uint16_t ws_port, Tangle &tangle) : ws_port(ws_port), tangle(tangle)
{
    initServer();
    initClient();
}

Network::~Network()
{
}

void Network::initClient()
{
    client = std::make_shared<WsClient>();
    client->init_asio();
    client->set_message_handler(
        [this](websocketpp::connection_hdl hdl, WsClient::message_ptr msg)
        {
            handleIncomingMessage(hdl, msg->get_payload(), ConnectionType::Client);
        });
    std::thread([this]
                { client->run(); })
        .detach();
}

void Network::initServer()
{
    server = std::make_shared<WsServer>();
    server->init_asio();
    server->set_reuse_addr(true);
    server->set_open_handler(
        [this](websocketpp::connection_hdl hdl)
        {
            // new connection opened
            // TODO: Save the hdl as server_hdl in the peer
        });
    server->set_message_handler(
        [this](websocketpp::connection_hdl hdl, WsServer::message_ptr msg)
        {
            handleIncomingMessage(hdl, msg->get_payload(), ConnectionType::Server);
        });
    server->listen(ws_port);
    server->start_accept();
    std::thread([this]
                { server->run(); })
        .detach();
}

// Computes SHA-256 checksum of the data
string Network::computeChecksum(const string &data)
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
bool Network::verifyChecksum(const string &data, const string &receivedChecksum)
{
    string calculatedChecksum = computeChecksum(data);
    return calculatedChecksum == receivedChecksum;
}

void Network::printLastTransaction()
{
    Transaction lastTx;
    time_t latestTimestamp = time(nullptr); // Initialize to current time

    for (const auto &pair : tangle.transactions)
    {
        if (pair.second.data.timestamp > latestTimestamp)
        {
            latestTimestamp = pair.second.data.timestamp;
            lastTx = pair.second;
        }
    }

    time_t txTime = latestTimestamp;
    time_t currentTime = time(nullptr);
    double elapsedSeconds = difftime(currentTime, txTime);

    // Convert seconds into human-readable format (e.g., minutes/seconds)
    int minutes = static_cast<int>(elapsedSeconds) / 60;
    int seconds = static_cast<int>(elapsedSeconds);

    // Print the last transaction details
    if (!lastTx.data.transaction_id.empty())
    {
        cout << "[LOG] Last transaction received: ID = " << lastTx.data.transaction_id
             << ", Sender = " << lastTx.data.sender << endl
             << ", Receiver = " << lastTx.data.receiver << endl
             << ", Amount = " << lastTx.data.amount << " " << lastTx.data.unit << endl
             << ", Price per unit = " << lastTx.data.price_per_unit << " " << lastTx.data.currency << endl
             << ", Time since creation = " << minutes << " min, " << seconds % 60 << " sec ago"
             << endl;
    }
    else
    {
        cout << "[LOG] No transactions found in updated Tangle." << endl;
    }
}

void Network::handleTangleUpdate(std::string receivedData)
{
    if (!receivedData.empty())
    {
        {

            tangle.updateFromSerialized(receivedData);
        }
        cout << "[LOG] Tangle update verified and applied." << endl;
        printLastTransaction();
    }
    else
    {
        cerr << "[ERROR] Received empty data from TCP client." << endl;
    }
}

void Network::handleIncomingMessage(ConnectionHdl hdl, const std::string &payload, ConnectionType connectionType)
{
    size_t pos = payload.find(":");
    if (pos != string::npos)
    {
        string messageType = payload.substr(0, pos);
        string message = payload.substr(pos + 1);
        // parse the message assuming it is in JSON format. seperate tangle, checksum, and timestamp
        Json::Value jsonData;
        Json::CharReaderBuilder reader;
        std::istringstream s(message);
        std::string errs;

        // Extract tangle data
        string data = jsonData["data"].asString();
        string checksum = jsonData["checksum"].asString();
        string timestamp = jsonData["timestamp"].asString();

        if (Json::parseFromStream(reader, s, &jsonData, &errs))
        {

            // Verify checksum
            if (verifyChecksum(data, checksum))
            {
                cout << "[LOG] Received valid Tangle update from peer." << endl;
                // handleTCPClient(data + " " + checksum, tangle);
            }
            else
            {
                cerr << "[ERROR] Checksum verification failed for received Tangle data." << endl;
                return;
            }
        }
        else
        {
            cerr << "[ERROR] Failed to parse JSON message: " << errs << endl;
            return;
        }

        if (messageType == "NEWTX")
        {
            cout << "[LOG] Received new transaction from peer: " << message << endl;
            // Handle new transaction
            Transaction newTx = Tangle::deserializeTransaction(data);
            // TODO: verify checksum of tx

            // TODO: check sign status of tx
            if (newTx.metadata.signature1.empty() && newTx.metadata.signature2.empty())
            {
                cerr << "[ERROR] Transaction is not signed. Cannot add to Tangle." << endl;
                return;
            }
            // single signed transaction
            else if (!newTx.metadata.signature1.empty() && newTx.metadata.signature2.empty())
            {
                cout << "[LOG] Transaction is only single signed. PoW not performed" << endl;

                // TODO: if the transaction is only signed by sender,

                string txSearialized = Tangle::serializeTransactionData(newTx);
                string sig_b64 = newTx.metadata.signature1;

                if (verifyTransaction(txSearialized, sig_b64, newTx.data.sender)) // Verify signature 1 is sender's signature
                {
                    cout << "[LOG] Transaction is signed by sender. Adding to Tangle." << endl;
                    // If the transaction is only signed by sender, we can add it to the Tangle
                    // but we need to perform PoW later
                }
                else
                {
                    cerr << "[ERROR] Transaction signature 1 verification failed for sender." << endl;
                    return;
                }
                // check if the receiver is same as the host node.
                const std::string uid = getenv("UID");
                // If yes, that means this node (the host node) is the receiver and must doubly sign the transaction
                if (newTx.data.receiver == uid) // If receiver is the host node
                {
                    // verify tx data against own meter data -> not possible here in docknet
                    cout << "[LOG] Transaction is for this node. Double signing it." << endl;
                    // Sign the transaction with the receiver's signature
                    newTx.metadata.signature2 = signTransaction(txSearialized);
                    newTx.metadata.lastUpdated = time(nullptr);
                    newTx.metadata.cumulative_weight = 0; // Initialize cumulative weight
                }
                else
                {
                    cout << "[LOG] Transaction is not for this node. Not signing." << endl;
                }

                tangle.addTransaction(newTx);
            }
            // double signed transaction
            else if (!newTx.metadata.signature1.empty() && !newTx.metadata.signature2.empty())
            {
                // TODO: verify each signature
                string txSearialized = Tangle::serializeTransactionData(newTx);
                string sig1_b64 = newTx.metadata.signature1;
                string sig2_b64 = newTx.metadata.signature2;

                if (verifyTransaction(txSearialized, sig1_b64, newTx.data.sender) &&
                    verifyTransaction(txSearialized, sig2_b64, newTx.data.receiver)) // Verify both signatures
                {
                    cout << "[LOG] Transaction is double signed by sender and receiver. Adding to Tangle." << endl;
                }
                else
                {
                    cerr << "[ERROR] Transaction signature verification failed." << endl;
                    return;
                }
                // perform PoW

                tangle.addTransaction(newTx);
                performPoW(newTx.data.transaction_id, 2);
                tangle.updateCumulativeWeight(newTx.data.transaction_id); // Increase cumulative weight for new transaction
            }

            cout << "[LOG] New transaction added to Tangle: " << newTx.data.transaction_id << endl;
            // TODO: broadcast this transaction to all peers
            broadcastTransaction(newTx);
        }
        if (messageType == "SYNC_REQ")
        {
            cout << "[LOG] Received SYNC_REQ from peer. Sending Tangle data." << endl;
            // Respond with Tangle data
            sendTangle(hdl, connectionType);
        }
        if (messageType == "SYNC_ACK")
        {
            cout << "[LOG] Received SYNC_ACK from peer. Tangle data synchronized." << endl;
            // TODO: accept tangle data from peer
            handleTangleUpdate(data);
        }
    }
    else
    {
        cerr << "[ERROR] Invalid message format received: " << payload << endl;
    }
}

void Network::broadcastTransaction(const Transaction &Tx)
{
    // Serialize the transaction
    string message = Tangle::serializeTransaction(Tx);

    string checksum = computeChecksum(message);

    // create a JSON object using JSON-CPP
    Json::Value jsonData;
    jsonData["data"] = message;
    jsonData["checksum"] = checksum;
    jsonData["timestamp"] = std::to_string(std::time(nullptr));

    Json::StreamWriterBuilder writer;
    string jsonString = Json::writeString(writer, jsonData);

    broadcastMessage(jsonString, "NEWTX");
    cout << "[LOG] Broadcasted new transaction to peers." << endl;
}

void Network::sendTangle(const ConnectionHdl &hdl, ConnectionType connectionType)
{
    // Serialize the Tangle
    string message = tangle.serialize();
    string checksum = computeChecksum(message);

    // create a JSON object using JSON-CPP
    Json::Value jsonData;
    jsonData["data"] = message;
    jsonData["checksum"] = checksum;
    jsonData["timestamp"] = std::to_string(std::time(nullptr));

    Json::StreamWriterBuilder writer;
    string jsonString = Json::writeString(writer, jsonData);

    sendMessage(jsonString, "SYNC_ACK", hdl, connectionType);

    cout << "[LOG][SYNC_ACK] Sent Tangle to peer ." << endl;
}
// TODO: sendSyncRequest() function

// Connect to a peer via WebSocket (client side)
void Network::connectWebSocket(Peer &peer)
{
    websocketpp::lib::error_code ec;
    auto uri = "ws://" + peer.address + ":" + std::to_string(ws_port);
    auto con = client->get_connection(uri, ec);
    if (ec){
        throw std::runtime_error(ec.message());
    }
        
    // Save outbound handle
    peer.client_hdl = con->get_handle();

    client->connect(con);

    std::cout << "Websocket connected to peer: " << peer.id << " at " << peer.address << ":" << peer.port << "\n";
}

// General function to send a message to all active peers. Input - Message and Message Type
void Network::broadcastMessage(const string &message, const string &messageType)
{
    for (auto &peer : activePeers)
    {
        // Construct the message with type prefix
        string fullMessage = messageType + ": " + message;

        if (!peer.client_hdl.expired())
        {
            sendMessage(message, messageType, peer.client_hdl, ConnectionType::Client);
        }
        // send via server if inbound connection exists
        else if (!peer.server_hdl.expired())
        {
            sendMessage(message, messageType, peer.server_hdl, ConnectionType::Server);
        }
    }
}
void Network::sendMessage(const string &message, const string &messageType, const ConnectionHdl &hdl, const ConnectionType connectionType)
{
    // Construct the message with type prefix
    string fullMessage = messageType + ": " + message;

    try
    {
        if (connectionType == ConnectionType::Client)
            client->send(hdl, fullMessage, websocketpp::frame::opcode::text);
        else if (connectionType == ConnectionType::Server)
            server->send(hdl, fullMessage, websocketpp::frame::opcode::text);
    }
    catch(const std::exception& e)
    {
        std::cerr << "[ERROR][SEND MESSAGE]" << e.what() << '\n';
    }
    

    cout << "[LOG] Sent message to peer: " << fullMessage << endl;
}
