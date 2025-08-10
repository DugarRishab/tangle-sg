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

Network::Network(uint16_t ws_port, Tangle &tangle, Peers &peers) : ws_port(ws_port), tangle(tangle), peers(peers)
{
    initServer();
    initClient();
    startPeerMonitor(std::chrono::seconds(30));
}

Network::~Network()
{
}

void Network::startPeerMonitor(std::chrono::milliseconds interval)
{
    monitorThread = std::thread(
        [this, interval]()
        {
            while (true)
            {
                std::cout << "[MONITOR] Checking active peers...\n";
                std::cout << "[MONITOR] Active peers count: " << peers.countPeers() << "\n";
                
                auto now = std::chrono::steady_clock::now();

                for (auto &[uri, peer] : peers.getPeerList())
                {
                    // if not already open or in the process of connecting
                    // log peer state
                    std::cout << "[MONITOR] Peer: " << peer.uri << " State: " << static_cast<int>(peer.state) << "\n";
                    if (peer.state != ConnectionState::OPEN &&
                        peer.state != ConnectionState::CONNECTING)
                    {
                        std::cout << "[MONITOR] Attempting reconnect to " << uri << "\n";
                        connectWebSocket(peer);
                        
                    }
                }

                std::this_thread::sleep_until(now + interval);
            }
        });

    monitorThread.detach();
}

void Network::initClient()
{
    client = std::make_shared<WsClient>();
    

    // client->clear_access_channels(websocketpp::log::alevel::all);
    // client->set_access_channels(websocketpp::log::alevel::connect |
    //                             websocketpp::log::alevel::debug_handshake |
    //                             websocketpp::log::alevel::fail |
    //                             websocketpp::log::alevel::debug_close);
    // client->clear_error_channels(websocketpp::log::elevel::none);
    // client->set_error_channels(websocketpp::log::elevel::all);

    client->init_asio();
    client->start_perpetual();

    client->set_open_handler(
        [this](ConnectionHdl hdl)
        {
            auto con = client->get_con_from_hdl(hdl);
            if (!con || !con->get_uri())
            {
                std::cout << "[WARN] Connection handle has no URI (early failure). Skipping.\n";
                return;
            }
            std::string uri = con->get_uri()->str();
            Peer p = peers.getPeer(uri);

            p.client_hdl = hdl;
            p.state = ConnectionState::OPEN;
            p.retryCount = 0;
            peers.updatePeer(p);

            std::cout << "[LOG] WebSocket connection OPENED with peer "
                      << p.id << " at " << p.address << " : " << p.port << "\n";

            // flush queued messages
            while (!p.outgoingQueue.empty())
            {
                auto &qm = p.outgoingQueue.front();
                client->send(hdl, qm.payload, qm.opcode);
                std::cout << "[LOG] Sent queued message to peer " << p.id << ": " << qm.payload << "\n";
                p.outgoingQueue.pop_front();
                peers.updatePeer(p);
            }
        });

    client->set_message_handler(
        [this](ConnectionHdl hdl, WsClient::message_ptr msg)
        {
            auto con = client->get_con_from_hdl(hdl);
            if (!con || !con->get_uri())
            {
                std::cout << "[WARN] Connection handle has no URI (early failure). Skipping.\n";
                return;
            }
            std::string uri = con->get_uri()->str();
            Peer p = peers.getPeer(uri);

            std::cout << "[LOG] PACKET FROM URI: " << con->get_uri()->str() << "\n";
            std::cout << "[LOG] Received message from peer " << p.id << " at " << p.address << " : " << p.port << "\n";
            handleIncomingMessage(p, msg->get_payload());
        });

    // on fail (handshake/transport error)
    client->set_fail_handler(
        [this](ConnectionHdl hdl)
        {
            auto con = client->get_con_from_hdl(hdl);
            if (!con || !con->get_uri())
            {
                std::cout << "[WARN] Connection handle has no URI (early failure). Skipping.\n";
                return;
            }
            std::string uri = con->get_uri()->str();
            Peer p = peers.getPeer(uri);
            std::cout << "[ERROR] WebSocket connection failed with peer " << p.id << " at " << p.address << " : " << p.port << "\n";
            p.state = ConnectionState::FAILED;
            scheduleReconnect(p);
        });

    // on close
    client->set_close_handler(
        [this](ConnectionHdl hdl)
        {
            auto con = client->get_con_from_hdl(hdl);
            if (!con || !con->get_uri())
            {
                std::cout << "[WARN] Connection handle has no URI (early failure). Skipping.\n";
                return;
            }
            std::string uri = con->get_uri()->str();
            Peer p = peers.getPeer(uri);
            std::cout << "[LOG] WebSocket connection closed with peer " << p.id << " at " << p.address << " : " << p.port << "\n";
            p.state = ConnectionState::CLOSED;
            scheduleReconnect(p);
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
        [this](ConnectionHdl hdl)
        {
            auto con = server->get_con_from_hdl(hdl);
            if (!con || !con->get_uri())
            {
                std::cout << "[WARN] Connection handle has no URI (early failure). Skipping.\n";
                return;
            }
            std::string uri = con->get_uri()->str();
            Peer p = peers.getPeer(uri);

            std::cout << "[LOG] New WebSocket connection from peer" << p.id << " at " << p.address << " : " << p.port << "\n";
            // new connection opened
            // TODO: Save the hdl as server_hdl in the peer
        });
    server->set_message_handler(
        [this](ConnectionHdl hdl, WsServer::message_ptr msg)
        {
            auto con = client->get_con_from_hdl(hdl);
            if (!con || !con->get_uri())
            {
                std::cout << "[WARN] Connection handle has no URI (early failure). Skipping.\n";
                return;
            }
            std::string uri = con->get_uri()->str();
            Peer p = peers.getPeer(uri);

            std::cout << "[LOG] Received message from peer " << p.id << " at " << p.address << " : " << p.port << "\n";

            handleIncomingMessage(p, msg->get_payload());
        });
    server->listen(ws_port);
    server->start_accept();
    std::thread([this]
                { server->run(); })
        .detach();
}

void Network::scheduleReconnect(Peer &peer)
{
    if (peer.retryCount < 3) // Limit retries to avoid infinite loop
    {
        peer.retryCount++;
        peer.nextRetry = std::chrono::steady_clock::now() + std::chrono::seconds(2 * peer.retryCount);
        std::cout << "[LOG] Scheduling reconnect for peer " << peer.id << " in " << 2 * peer.retryCount << " seconds.\n";
    }
    else
    {
        std::cout << "[ERROR] Max retry limit reached for peer " << peer.id << ". Giving up.\n";
        peer.state = ConnectionState::FAILED;
    }
    peers.updatePeer(peer);
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

void Network::handleIncomingMessage(Peer &peer, const std::string &payload)
{
    size_t pos = payload.find("::TYPE::");
    if (pos != string::npos)
    {
        string messageType = payload.substr(0, pos);
        string message = payload.substr(pos + 8);
        // parse the message assuming it is in JSON format. seperate tangle, checksum, and timestamp
        Json::Value jsonData;
        Json::CharReaderBuilder reader;
        std::istringstream s(message);
        std::string errs;

        const bool isValidJson = Json::parseFromStream(reader, s, &jsonData, &errs);

        if (isValidJson && jsonData.isObject())
        {

            // Extract tangle data
            string data = jsonData["data"].asString();
            string checksum = jsonData["checksum"].asString();
            string timestamp = jsonData["timestamp"].asString();

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
            string data = jsonData["data"].asString();
            string checksum = jsonData["checksum"].asString();
            string timestamp = jsonData["timestamp"].asString();

            cout << "[LOG] Received new transaction from peer: " << message << endl;
            // Handle new transaction
            Transaction newTx = Tangle::deserializeTransaction(data);

            

            // TODO: check sign status of tx
            if (newTx.metadata.signature1.empty() && newTx.metadata.signature2.empty())
            {
                cerr << "[ERROR] Transaction is not signed. Cannot add to Tangle." << endl;
                return;
            }
            // single signed transaction
            else if (!newTx.metadata.signature1.empty() && newTx.metadata.signature2.empty())
            {
                cout << "[LOG] Transaction is only single signed." << endl;

                // TODO: if the transaction is only signed by sender,
                // add transaction to Tangle but perform PoW later
                string txSearialized = Tangle::serializeTransactionData(newTx);
                string sig_b64 = newTx.metadata.signature1;

                if (verifyTransaction(txSearialized, sig_b64, newTx.data.sender)) // Verify signature 1 is sender's signature
                {
                    cout << "[LOG] Transaction is signed by sender." << endl;
                    // If the transaction is only signed by sender, we can add it to the Tangle
                    // but we need to perform PoW later
                }
                else
                {
                    cerr << "[ERROR] Transaction signature 1 verification failed for sender." << endl;
                    return;
                }
                int isTxPresent = tangle.addTransaction(newTx, 1);
                Transaction tx = tangle.transactions[newTx.data.transaction_id];
                // check if the receiver is same as the host node.
                if(isTxPresent == 0)
                {
                    cout << "[LOG] Transaction already exists in Tangle. No Updates. Not broadcasting." << endl;
                    return;
                }
                if(isTxPresent == 1)
                {
                    cout << "[LOG] Transaction already exists in Tangle. Updating it." << endl;

                    broadcastTransaction(tangle.transactions[tx.data.transaction_id]);
                }
                if(isTxPresent == 2)
                {
                    cout << "[LOG] Transaction added to Tangle. Broadcasting." << endl;

                    const std::string uid = getenv("UID");
                    // If yes, that means this node (the host node) is the receiver and must doubly sign the transaction
                    if (tx.data.receiver == uid) // If receiver is the host node
                    {
                        // verify tx data against own meter data -> not possible here in docknet
                        cout << "[LOG] Transaction is for this node. Double signing it." << endl;
                        // Sign the transaction with the receiver's signature
                        tx.metadata.signature2 = signTransaction(txSearialized);
                        // perform PoW on the transaction
                        performPoW(tx.data.transaction_id);
                        tx.metadata.lastUpdated = std::time(nullptr);
                        tangle.updateTransaction(tx);
                        tangle.updateCumulativeWeight(tx.data.transaction_id); // Increase cumulative weight for new transaction
                         // Update the transaction in Tangle
                    }
                    broadcastTransaction(tangle.transactions[newTx.data.transaction_id]);
                }
                
                
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

                int isTxPresent = tangle.addTransaction(newTx, 1);
                Transaction tx = tangle.transactions[newTx.data.transaction_id];
                // check if the receiver is same as the host node.
                if (isTxPresent == 0)
                {
                    cout << "[LOG] Transaction already exists in Tangle. No Updates. Not broadcasting." << endl;
                    return;
                }
                if (isTxPresent == 1)
                {
                    cout << "[LOG] Transaction already exists in Tangle. Updating it." << endl;
                    broadcastTransaction(tangle.transactions[newTx.data.transaction_id]);
                }
                else{
                    cout << "LOG] Transaction added to Tangle. Broadcasting." << endl;
                    // perform PoW on the transaction
                    performPoW(tx.data.transaction_id);
                    
                    tangle.updateCumulativeWeight(tx.data.transaction_id); // Increase cumulative weight for new transaction
                    
                    broadcastTransaction(tangle.transactions[newTx.data.transaction_id]);
                }
            }
            
        }
        if (messageType == "SYNC_REQ")
        {
            cout << "[LOG] Received SYNC_REQ from peer. Sending Tangle data." << endl;
            // Respond with Tangle data
            sendTangle(peer);
        }
        if (messageType == "SYNC_ACK")
        {
            cout << "[LOG] Received SYNC_ACK from peer. Tangle data synchronized." << endl;
            // TODO: accept tangle data from peer
            string data = jsonData["data"].asString();
            string checksum = jsonData["checksum"].asString();
            string timestamp = jsonData["timestamp"].asString();

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
    std::cout << "[LOG] Broadcasting new transaction: " << Tx.data.transaction_id << endl;
    // std::cout << "[LOG] Transaction data: " << message << endl;

    string checksum = computeChecksum(message);

    // create a JSON object using JSON-CPP
    Json::Value jsonData;
    jsonData["data"] = message;
    jsonData["checksum"] = checksum;
    jsonData["timestamp"] = std::to_string(std::time(nullptr));

    Json::StreamWriterBuilder writer;
    string jsonString = Json::writeString(writer, jsonData);
    // cout << "[LOG] Transaction JSON: " << jsonString << endl;
    broadcastMessage(jsonString, "NEWTX");
}

void Network::sendTangle(Peer &peer)
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

    sendMessage(jsonString, "SYNC_ACK", peer);

    cout << "[LOG][SYNC_ACK] Sent Tangle to peer ." << endl;
}
// TODO: sendSyncRequest() function

// Connect to a peer via WebSocket (client side)
void Network::connectWebSocket(Peer &peer)
{
    if (peer.state == ConnectionState::CONNECTING ||
        peer.state == ConnectionState::OPEN)
        return;

    websocketpp::lib::error_code ec;
    auto uri = "ws://" + peer.address + ":" + std::to_string(ws_port);
    
    auto con = client->get_connection(uri, ec);
    if (ec)
    {
        peers.updatePeerState(peer.uri, ConnectionState::FAILED);
        peer.state = ConnectionState::FAILED;

        std::cerr << "[ERROR][CONNECT_WS] Websocket connection NOT established with peer "
                  << peer.id << " at " << peer.address << " : " << peer.port << ". Reason: " << ec.message() << '\n';
        throw std::runtime_error(ec.message());
    }

    // Save outbound handle
    peer.client_hdl = con->get_handle();
    

    try
    {
        peer.state = ConnectionState::CONNECTING;
        peers.updatePeer(peer);
        client->connect(con);
        std::cout << "[LOG][CONNECT_WS] Websocket connected to peer: " << peer.id << " at " << peer.address << ":" << peer.port << "\n";
    }
    catch (const std::exception &e)
    {
        peer.state = ConnectionState::FAILED;
        peers.updatePeer(peer);
        std::cerr << "[ERROR][CONNECT_WS] Websocket connection NOT established with peer"
                  << peer.id << " at " << peer.address << " : " << peer.port << ". Reason: " << e.what() << '\n';
    }
}

// General function to send a message to all active peers. Input - Message and Message Type
void Network::broadcastMessage(const string &message, const string &messageType)
{
    for (auto &item : peers.getPeerList())
    {
        // Construct the message with type prefix
        string fullMessage = messageType + "::TYPE::" + message;

        sendMessage(message, messageType, item.second);
    }
}

void Network::sendMessage(const string &message, const string &messageType, Peer &peer)
{
    // Construct the message with type prefix
    string fullMessage = messageType + "::TYPE::" + message;
    ConnectionHdl hdl = peer.client_hdl;

    try
    {
        if (peer.state == ConnectionState::OPEN)
        {
            client->send(hdl, fullMessage, websocketpp::frame::opcode::text);
            cout << "[LOG][SEND] Sent message to peer: " << fullMessage << endl;
            return;
        }

        peer.outgoingQueue.push_back({fullMessage, websocketpp::frame::opcode::text});
        peers.updatePeer(peer); // Update peer state with queued message
        connectWebSocket(peer);

        std::cout << "[LOG][SEND] Message queued for peer: " << peer.id << " at " << peer.address << ":" << peer.port << endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[ERROR][SEND MESSAGE]" << e.what() << '\n';
    }
}
