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
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include "../headers/peers2.h"
#include "../headers/transaction.h"
#include "../headers/utils.h"
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
    // Phase 7: Orphan pool config
    const char *orphanTTLEnv = getenv("ORPHAN_TTL_SEC");
    orphanTTL = orphanTTLEnv ? atoi(orphanTTLEnv) : 600;
    const char *orphanPoolMaxEnv = getenv("ORPHAN_POOL_MAX");
    orphanPoolMax = orphanPoolMaxEnv ? atoi(orphanPoolMaxEnv) : 1000;

    // Phase 8: Rate limit config
    const char *rateLimitBaseEnv = getenv("RATE_LIMIT_BASE");
    rateLimitBase = rateLimitBaseEnv ? atof(rateLimitBaseEnv) : 10.0;
    const char *rateLimitBurstEnv = getenv("RATE_LIMIT_BURST");
    rateLimitBurst = rateLimitBurstEnv ? atof(rateLimitBurstEnv) : 20.0;
    const char *rateLimitWindowEnv = getenv("RATE_LIMIT_WINDOW_SEC");
    int rateLimitWindowSec = rateLimitWindowEnv ? atoi(rateLimitWindowEnv) : 60;
    rateLimitWindowMs = rateLimitWindowSec * 1000;

    // Phase: Dynamic gossip fanout = ceil(ln(N)), min 3
    const char *totalNodesEnv = getenv("TOTAL_NODES");
    int totalNodes = totalNodesEnv ? atoi(totalNodesEnv) : 10;
    gossipFanout_ = std::max(3, static_cast<int>(std::ceil(std::log(totalNodes))));
    std::cout << "[INFO] Gossip fanout set to: " << gossipFanout_ << " (N=" << totalNodes << ")\n";

    initServer();
    initClient();
    const char *monitorDelayEnv = getenv("MONITOR_PERIOD");
    int monitorDelay = monitorDelayEnv ? atoi(monitorDelayEnv) : 5;
    startPeerMonitor(std::chrono::seconds(monitorDelay));
}

Network::~Network()
{
    stopPeerMonitor();
    if (monitorThread.joinable())
    {
        monitorThread.join();
    }
    std::cout << "[LOG] Network module destroyed." << std::endl;
}

void Network::startPeerMonitor(std::chrono::milliseconds interval)
{
    // stop existing monitor if running
    if (monitorRunning_.load())
        return; // if it's already true, exit immediately

    monitorRunning_.store(true);
    // mark it as running so nobody else starts it again

    monitorThread = std::thread(
        [this, interval]()
        {
            while (monitorRunning_.load())
            {
                std::cout << "[MONITOR] Checking active peers...\n";
                std::cout << "[MONITOR] Active peers count: " << peers.countPeers() << "\n";

                // auto now = std::chrono::steady_clock::now();

                for (auto &[uri, peer] : peers.getPeerList())
                {
                    // if not already open or in the process of connecting
                    // log peer state
                    std::cout << "[MONITOR] Peer: " << peer.uri << " State: " << static_cast<int>(peer.state) << "\n";

                    auto now = std::chrono::steady_clock::now();

                    // Skip peers that are already connected or connecting
                    if (peer.state == ConnectionState::OPEN ||
                        peer.state == ConnectionState::CONNECTING)
                    {
                        std::deque<Message> outgoingQueue;
                        if (peers.drainOutgoingQueue(peer.uri, outgoingQueue))
                        {
                            for (auto &qm : outgoingQueue)
                            {
                                try
                                {
                                    client->send(peer.client_hdl, qm.payload, qm.opcode);
                                    std::cout << "[MONITOR] Sent queued message to peer " << peer.id << ": " << qm.payload << "\n";
                                    outgoingQueue.pop_front();
                                }
                                catch (const websocketpp::exception &e)
                                {
                                    std::cerr << "[MONITOR][ERROR] Failed to send queued message to peer " << peer.id << ": " << e.what() << "\n";
                                    // Re-enqueue the message for future attempts
                                    peers.enqueueMessage(peer.uri, qm);
                                    // break; // exit the loop on failure
                                }
                            }
                        }

                        continue;
                    }

                    // If retryCount reached limit, evict dead peer
                    if (peer.retryCount >= 5)
                    {
                        std::cout << "[MONITOR] Evicting peer " << peer.id << " - max retries reached.\n";
                        peers.removePeer(uri);
                        continue;
                    }

                    // If it's not yet time to retry, skip
                    if (now < peer.nextRetry)
                    {
                        auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(peer.nextRetry - now).count();
                        std::cout << "[MONITOR] Waiting " << remaining_ms << " ms before retrying " << peer.id << "\n";
                        continue;
                    }

                    std::cout << "[MONITOR] Attempting reconnect to " << uri << "\n";
                    connectWebSocket(peer);
                }

                auto target = std::chrono::steady_clock::now() + interval;
                while (monitorRunning_.load() && std::chrono::steady_clock::now() < target)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            }
        });

    // monitorThread.detach();
}

void Network::stopPeerMonitor()
{
    if (monitorRunning_.load())
    {
        monitorRunning_.store(false);
        if (monitorThread.joinable())
        {
            monitorThread.join();
        }
        std::cout << "[MONITOR] Peer monitor stopped.\n";
    }
    else
    {
        std::cout << "[MONITOR] Peer monitor is not running.\n";
    }
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
                std::cout << "[CLIENT][WARN] Connection handle has no URI (early failure). Skipping.\n";
                return;
            }
            std::string uri = con->get_uri()->str();
            Peer p = peers.getPeer(uri);

            p.client_hdl = hdl;
            p.state = ConnectionState::OPEN;
            p.retryCount = 0;
            peers.updatePeer(p);

            std::cout << "[CLIENT] WebSocket connection OPENED with peer "
                      << p.id << " at " << p.address << " : " << p.port << "\n";

            // flush queued messages
            std::deque<Message> outgoingQueue;
            if (peers.drainOutgoingQueue(p.uri, outgoingQueue))
            {
                for (auto &qm : outgoingQueue)
                {
                    client->send(hdl, qm.payload, qm.opcode);
                    std::cout << "[CLIENT] Sent queued message to peer " << p.id << ": " << qm.payload << "\n";
                    outgoingQueue.pop_front();
                }
            }
        });

    client->set_message_handler(
        [this](ConnectionHdl hdl, WsClient::message_ptr msg)
        {
            auto con = client->get_con_from_hdl(hdl);
            if (!con || !con->get_uri())
            {
                std::cout << "[CLIENT][WARN] Connection handle has no URI (early failure). Skipping.\n";
                return;
            }
            std::string uri = con->get_uri()->str();
            Peer p = peers.getPeer(uri);

            // std::cout << "[CLIENT] PACKET FROM URI: " << con->get_uri()->str() << "\n";
            // std::cout << "[CLIENT] Received message from peer " << p.id << " at " << p.address << " : " << p.port << "\n";
            handleIncomingMessage(p, msg->get_payload());
        });

    // on fail (handshake/transport error)
    client->set_fail_handler(
        [this](ConnectionHdl hdl)
        {
            auto con = client->get_con_from_hdl(hdl);
            if (!con || !con->get_uri())
            {
                std::cout << "[CLIENT][WARN] Connection handle has no URI (early failure). Skipping.\n";
                return;
            }
            std::string uri = con->get_uri()->str();
            Peer p = peers.getPeer(uri);
            std::cout << "[CLIENT][ERROR] WebSocket connection failed with peer " << p.id << " at " << p.address << " : " << p.port << "\n";
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
                std::cout << "[CLIENT][WARN] Connection handle has no URI (early failure). Skipping.\n";
                return;
            }
            std::string uri = con->get_uri()->str();
            Peer p = peers.getPeer(uri);
            std::cout << "[CLIENT] WebSocket connection closed with peer " << p.id << " at " << p.address << " : " << p.port << "\n";
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
                std::cout << "[SERVER][WARN] Connection handle has no URI (early failure). Skipping.\n";
                return;
            }
            std::string uri = con->get_uri()->str();
            Peer p = peers.getPeer(uri);

            std::cout << "[SERVER] New WebSocket connection from peer" << p.id << " at " << p.address << " : " << p.port << "\n";
            // new connection opened
            // TODO: Save the hdl as server_hdl in the peer
        });
    server->set_message_handler(
        [this](ConnectionHdl hdl, WsServer::message_ptr msg)
        {
            auto con = server->get_con_from_hdl(hdl);
            if (!con || !con->get_uri())
            {
                std::cout << "[SERVER][WARN] Connection handle has no URI (early failure). Skipping.\n";
                return;
            }
            std::string uri = con->get_uri()->str();
            Peer p = peers.getPeer(uri);

            // std::cout << "[SERVER] Received message from peer " << p.id << " at " << p.address << " : " << p.port << "\n";

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
    if (peer.retryCount < 5) // Limit retries to avoid infinite loop
    {
        peer.retryCount++;
        peer.nextRetry = std::chrono::steady_clock::now() + std::chrono::seconds(2 * peer.retryCount);
        std::cout << "[SCHEDULE] Scheduling reconnect for peer " << peer.id << " in " << 2 * peer.retryCount << " seconds.\n";
    }
    else
    {
        std::cout << "[SCHEDULE][ERROR] Max retry limit reached for peer " << peer.id << ". Giving up.\n";
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

    for (auto &pair : tangle.getAllTransactions())
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
    try
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
                if (!verifyChecksum(data, checksum))
                {
                    cerr << "[HANDLER][ERROR] Checksum verification failed for received message data." << endl;
                    recordValidation(peer.uri, false);
                    return;
                }

                // Phase 8: Rate limit ALL messages
                if (!consumeToken(peer.uri))
                {
                    return; // silently drop, peer exceeded quota
                }
            }
            else
            {
                cerr << "[HANDLER][ERROR] Failed to parse JSON message: " << errs << endl;
                return;
            }

            if (messageType == "TX_PROPOSAL")
            {
                handleTxProposal(peer, jsonData);
            }
            else if (messageType == "TX_APPROVAL")
            {
                handleTxApproval(peer, jsonData);
            }
            else if (messageType == "VOTE")
            {
                handleVote(peer, jsonData);
            }
            else if (messageType == "NEWTX")
            {
                // Legacy handler: route to TX_PROPOSAL logic
                handleTxProposal(peer, jsonData);
            }
            else if (messageType == "TX_ACK")
            {
                handleTxAck(peer, jsonData);
            }
            else if (messageType == "SYNC_REQ")
            {
                cout << "[HANDLER] Received SYNC_REQ from peer. Sending Tangle data." << endl;
                sendTangle(peer);
                recordValidation(peer.uri, true);
            }
            else if (messageType == "SYNC_ACK")
            {
                cout << "[HANDLER] Received SYNC_ACK from peer. Tangle data synchronized." << endl;
                string data = jsonData["data"].asString();
                string checksum = jsonData["checksum"].asString();
                string timestamp = jsonData["timestamp"].asString();

                handleTangleUpdate(data);
                recordValidation(peer.uri, true);
            }
            else if (messageType == "TX_REQ")
            {
                try
                {
                    string txId = jsonData["data"].asString();
                    cout << "[HANDLER] Received TX_REQ for " << txId << ". Sending transaction." << endl;
                    std::string id = txId;
                    Transaction tx = tangle.getTransaction(id);
                    if (!tx.data.transaction_id.empty()) {
                        sendMessage(serializeTransaction(tx), "TX_ACK", peer);
                        recordValidation(peer.uri, true);
                    } else {
                        cerr << "[HANDLER][ERROR] Requested transaction " << txId << " not found." << endl;
                        recordValidation(peer.uri, false);
                    }
                }
                catch (const std::exception &e)
                {
                    std::cerr << "[HANDLER][ERROR] Exception handling TX_REQ: " << e.what() << std::endl;
                    recordValidation(peer.uri, false);
                }
            }
        }
        else
        {
            cerr << "[ERROR] Invalid message format received: " << payload << endl;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "[HANDLER][ERROR] Exception in handleIncomingMessage: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "[HANDLER][ERROR] Unknown exception in handleIncomingMessage" << std::endl;
    }
}

void Network::handleTxProposal(Peer &peer, const Json::Value &jsonData)
{
    try
    {
        string data = jsonData["data"].asString();
        string checksum = jsonData["checksum"].asString();

        cout << "[HANDLER] Received TX_PROPOSAL from peer." << endl;
        Transaction newTx = deserializeTransaction(data);

        // Verify checksum
        if (!verifyChecksum(data, checksum))
        {
            cerr << "[HANDLER][ERROR] Checksum verification failed for TX_PROPOSAL." << endl;
            recordValidation(peer.uri, false);
            return;
        }

        // Check for missing parents
        bool missingParent = false;
        for (const auto &parent : newTx.data.parents)
        {
            std::string parentId = parent;
            Transaction pTx = tangle.getTransaction(parentId);
            if (!tangle.transactionPresent(pTx) && parent != "genesis")
            {
                if (pTx.data.transaction_id.empty()) {
                    std::cout << "[ORPHAN] Transaction " << newTx.data.transaction_id << " missing parent " << parent << ". Queuing as orphan." << std::endl;
                    std::lock_guard<std::recursive_mutex> lock(orphansMutex);
                    expireOrphans();
                    orphans[parent].push_back({newTx, timeNow()});
                    missingParent = true;
                    requestTransaction(parent, peer);
                }
            }
        }
        if (missingParent)
        {
            recordValidation(peer.uri, false);
            return;
        }

        // Validate parent status: all parents must be FINAL (except genesis)
        for (const auto &parent : newTx.data.parents)
        {
            if (parent == "genesis") continue;
            Transaction pTx = tangle.getTransaction(parent);
            if (pTx.metadata.status != TransactionStatus::FINAL)
            {
                cerr << "[HANDLER][ERROR] TX_PROPOSAL parent " << parent
                     << " is not FINAL. Dropping." << endl;
                recordValidation(peer.uri, false);
                return;
            }
        }

        // Validate signature 1 (sender)
        if (newTx.metadata.signature1.empty())
        {
            cerr << "[HANDLER][ERROR] TX_PROPOSAL has no signature1. Dropping." << endl;
            recordValidation(peer.uri, false);
            return;
        }

        string txSerialized = serializeTransactionData(newTx);
        if (!verifyTransaction(txSerialized, newTx.metadata.signature1, newTx.data.sender))
        {
            cerr << "[HANDLER][ERROR] TX_PROPOSAL signature1 verification failed. Dropping." << endl;
            recordValidation(peer.uri, false);
            return;
        }

        int isTxPresent = tangle.addTransaction(newTx, 1);
        if (isTxPresent == 0)
        {
            cout << "[HANDLER] Transaction already exists. No updates." << endl;
            recordValidation(peer.uri, true);
            return;
        }
        if (isTxPresent == 1)
        {
            cout << "[HANDLER] Transaction updated." << endl;
            postProcessAddedTransaction(newTx.data.transaction_id, peer.uri);
            recordValidation(peer.uri, true);
            return;
        }

        // isTxPresent == 2: newly added
        cout << "[HANDLER] Transaction added to Tangle. Broadcasting TX_PROPOSAL." << endl;
        postProcessAddedTransaction(newTx.data.transaction_id, peer.uri);
        recordValidation(peer.uri, true);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[HANDLER][ERROR] Exception handling TX_PROPOSAL: " << e.what() << std::endl;
        recordValidation(peer.uri, false);
    }
}

void Network::handleTxApproval(Peer &peer, const Json::Value &jsonData)
{
    try
    {
        string data = jsonData["data"].asString();
        string checksum = jsonData["checksum"].asString();

        if (!verifyChecksum(data, checksum))
        {
            cerr << "[HANDLER][ERROR] Checksum verification failed for TX_APPROVAL." << endl;
            recordValidation(peer.uri, false);
            return;
        }

        Json::Value approvalData;
        Json::CharReaderBuilder reader;
        std::istringstream s(data);
        std::string errs;
        if (!Json::parseFromStream(reader, s, &approvalData, &errs))
        {
            cerr << "[HANDLER][ERROR] Failed to parse TX_APPROVAL payload: " << errs << endl;
            recordValidation(peer.uri, false);
            return;
        }

        string tx_id = approvalData["tx_id"].asString();
        string sig2 = approvalData["sig2"].asString();
        string receiver_id = approvalData["receiver_id"].asString();

        Transaction tx = tangle.getTransaction(tx_id);
        if (tx.data.transaction_id.empty())
        {
            cerr << "[HANDLER][ERROR] TX_APPROVAL references unknown tx " << tx_id << ". Requesting it." << endl;
            requestTransaction(tx_id, peer);
            recordValidation(peer.uri, false);
            return;
        }

        // Dedup: already approved — don't re-apply or re-gossip
        if (!tx.metadata.signature2.empty())
        {
            recordValidation(peer.uri, true);
            return;
        }

        // Verify sig2 against the transaction data
        string txSerialized = serializeTransactionData(tx);
        if (!verifyTransaction(txSerialized, sig2, receiver_id))
        {
            cerr << "[HANDLER][ERROR] TX_APPROVAL sig2 verification failed. Dropping." << endl;
            recordValidation(peer.uri, false);
            return;
        }

        // Apply approval
        tx.metadata.signature2 = sig2;
        tx.metadata.lastUpdated = timeNow();
        tangle.updateTransaction(tx);

        // Add sender and receiver as implicit voters (they signed the tx)
        tangle.addVote(tx_id, tx.data.sender);
        tangle.addVote(tx_id, receiver_id);

        // This node also votes explicitly if it hasn't already
        createAndBroadcastVote(tx_id);
        processOrphans(tx_id); // Re-trigger orphan resolution if tx became FINAL

        cout << "[HANDLER] Applied TX_APPROVAL for " << tx_id << ". Gossiping." << endl;

        // Gossip the TX_APPROVAL delta (validate-before-forward)
        Json::StreamWriterBuilder writer;
        string msg = Json::writeString(writer, jsonData);
        broadcastMessage(msg, "TX_APPROVAL", peer.uri);

        recordValidation(peer.uri, true);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[HANDLER][ERROR] Exception handling TX_APPROVAL: " << e.what() << std::endl;
        recordValidation(peer.uri, false);
    }
}

void Network::handleVote(Peer &peer, const Json::Value &jsonData)
{
    try
    {
        string data = jsonData["data"].asString();
        string checksum = jsonData["checksum"].asString();

        if (!verifyChecksum(data, checksum))
        {
            cerr << "[HANDLER][ERROR] Checksum verification failed for VOTE." << endl;
            recordValidation(peer.uri, false);
            return;
        }

        Json::Value voteData;
        Json::CharReaderBuilder reader;
        std::istringstream s(data);
        std::string errs;
        if (!Json::parseFromStream(reader, s, &voteData, &errs))
        {
            cerr << "[HANDLER][ERROR] Failed to parse VOTE payload: " << errs << endl;
            recordValidation(peer.uri, false);
            return;
        }

        string tx_id = voteData["tx_id"].asString();
        string voter_id = voteData["voter_id"].asString();
        string signature = voteData["signature"].asString();

        // Verify vote signature (sign(tx_id + voter_id))
        string votePayload = tx_id + voter_id;
        if (!verifyTransaction(votePayload, signature, voter_id))
        {
            cerr << "[HANDLER][ERROR] VOTE signature verification failed from " << voter_id << ". Dropping." << endl;
            recordValidation(peer.uri, false);
            return;
        }

        Transaction tx = tangle.getTransaction(tx_id);
        if (tx.data.transaction_id.empty())
        {
            cerr << "[HANDLER][ERROR] VOTE references unknown tx " << tx_id << ". Requesting it." << endl;
            requestTransaction(tx_id, peer);
            recordValidation(peer.uri, false);
            return;
        }

        // If already FINAL, accept silently but don't re-propagate
        if (tx.metadata.status == TransactionStatus::FINAL)
        {
            recordValidation(peer.uri, true);
            return;
        }

        // Check for duplicate vote before recording
        if (tx.metadata.voted_by.find(voter_id) != tx.metadata.voted_by.end())
        {
            recordValidation(peer.uri, true);
            return; // duplicate, drop silently
        }

        bool isNewVote = tangle.addVote(tx_id, voter_id);
        if (!isNewVote)
        {
            recordValidation(peer.uri, true);
            return; // duplicate (race condition), drop silently
        }
        processOrphans(tx_id); // Re-trigger orphan resolution if tx became FINAL

        tx = tangle.getTransaction(tx_id);
        if (tx.metadata.status == TransactionStatus::FINAL)
        {
            cout << "[HANDLER] VOTE from " << voter_id << " finalized tx " << tx_id << ". Gossiping VOTE." << endl;
        }
        else
        {
            cout << "[HANDLER] Accepted VOTE from " << voter_id << " for tx " << tx_id
                 << " (votes=" << tx.metadata.votes << ")." << endl;
        }

        // Always gossip valid new votes
        Json::StreamWriterBuilder writer;
        string msg = Json::writeString(writer, jsonData);
        broadcastMessage(msg, "VOTE", peer.uri);

        recordValidation(peer.uri, true);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[HANDLER][ERROR] Exception handling VOTE: " << e.what() << std::endl;
        recordValidation(peer.uri, false);
    }
}

void Network::broadcastTxProposal(const Transaction &Tx, const std::string &excludeUri)
{
    try
    {
        // Serialize the transaction
        string message = serializeTransaction(Tx);
        std::cout << "[SEND] Broadcasting TX_PROPOSAL: " << Tx.data.transaction_id << endl;

        string checksum = computeChecksum(message);

        // create a JSON object using JSON-CPP
        Json::Value jsonData;
        jsonData["data"] = message;
        jsonData["checksum"] = checksum;
        jsonData["timestamp"] = std::to_string(std::time(nullptr));

        Json::StreamWriterBuilder writer;
        string jsonString = Json::writeString(writer, jsonData);
        broadcastMessage(jsonString, "TX_PROPOSAL", excludeUri);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[BROADCAST][ERROR] Exception broadcasting TX_PROPOSAL: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "[BROADCAST][ERROR] Unknown exception broadcasting TX_PROPOSAL" << std::endl;
    }
}
void Network::createAndBroadcastVote(const std::string& tx_id)
{
    try
    {
        const char* uidEnv = getenv("UID");
        if (!uidEnv)
        {
            std::cerr << "[VOTE][ERROR] UID not set. Cannot create vote." << std::endl;
            return;
        }
        std::string uid = uidEnv;

        // Check if we already voted for this tx
        Transaction tx = tangle.getTransaction(tx_id);
        if (tx.metadata.voted_by.find(uid) != tx.metadata.voted_by.end())
        {
            return; // already voted
        }

        // Record vote locally first (so it's counted even if return path is dropped)
        tangle.addVote(tx_id, uid);

        // Sign vote payload: tx_id + voter_id
        std::string votePayload = tx_id + uid;
        std::string signature = signTransaction(votePayload);

        // Build vote JSON
        Json::Value voteJson;
        voteJson["tx_id"] = tx_id;
        voteJson["voter_id"] = uid;
        voteJson["signature"] = signature;

        // Wrap with checksum
        Json::StreamWriterBuilder writer;
        std::string voteMsg = Json::writeString(writer, voteJson);
        std::string checksum = computeChecksum(voteMsg);

        Json::Value wrap;
        wrap["data"] = voteMsg;
        wrap["checksum"] = checksum;
        wrap["timestamp"] = std::to_string(std::time(nullptr));

        std::string wrapStr = Json::writeString(writer, wrap);
        broadcastMessage(wrapStr, "VOTE");

        std::cout << "[VOTE] Created and broadcast vote for tx " << tx_id << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[VOTE][ERROR] Exception creating vote: " << e.what() << std::endl;
    }
}

// TODO: sendSyncRequest() function

// Connect to a peer via WebSocket (client side)
bool Network::connectWebSocket(Peer &peer)
{
    auto peerCopy = peers.getPeer(peer.uri);

    if (peerCopy.state == ConnectionState::CONNECTING ||
        peerCopy.state == ConnectionState::OPEN)
        return true;

    websocketpp::lib::error_code ec;
    auto uri = "ws://" + peer.address + ":" + std::to_string(ws_port);

    auto con = client->get_connection(uri, ec);
    if (ec)
    {
        peers.updatePeerState(peer.uri, ConnectionState::FAILED);
        peer.state = ConnectionState::FAILED;

        std::cerr << "[CONNECT_WS][ERROR] Websocket connection NOT established with peer "
                  << peer.id << " at " << peer.address << " : " << peer.port << ". Reason: " << ec.message() << '\n';
        return false;
    }

    // Save outbound handle
    peer.client_hdl = con->get_handle();

    try
    {
        peer.state = ConnectionState::CONNECTING;
        peers.updatePeer(peer);
        client->connect(con);

        std::cout << "[CONNECT_WS] Websocket connected to peer: " << peer.id << " at " << peer.address << ":" << peer.port << "\n";
        return true; // Connection initiated successfully
    }
    catch (const std::exception &e)
    {
        peer.state = ConnectionState::FAILED;
        peers.updatePeer(peer);
        std::cerr << "[CONNECT_WS][ERROR] Websocket connection NOT established with peer"
                  << peer.id << " at " << peer.address << " : " << peer.port << ". Reason: " << e.what() << '\n';
        return false; // Connection initiation failed
    }
}

// General function to send a message to a random subset of active peers.
// excludeUri: optional sender URI to omit from broadcast (prevents echo-back)
void Network::broadcastMessage(const string &message, const string &messageType, const string &excludeUri)
{
    for (auto &item : peers.getRandomPeerSubset(gossipFanout_))
    {
        if (!excludeUri.empty() && item.second.uri == excludeUri)
            continue;
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
        Message msg = {fullMessage, websocketpp::frame::opcode::text};
        peers.enqueueMessage(peer.uri, msg);

        // connectWebSocket(peer);

        std::cout << "[SEND] Message queued for peer: " << peer.id << " at " << peer.address << ":" << peer.port << endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[SEND][ERROR]" << e.what() << '\n';
    }
}

void Network::requestTransaction(const std::string &txId, Peer &peer)
{
    Json::Value jsonData;
    jsonData["data"] = txId;
    jsonData["checksum"] = "0"; // Not needed for simple ID
    jsonData["timestamp"] = std::to_string(std::time(nullptr));

    Json::StreamWriterBuilder writer;
    string jsonString = Json::writeString(writer, jsonData);
    sendMessage(jsonString, "TX_REQ", peer);
    std::cout << "[ORPHAN] Requested missing transaction " << txId << " from peer " << peer.id << std::endl;
}

void Network::sendTangle(Peer& peer)
{
    // Serialize entire Tangle and send to peer
    // Used for initial sync or peer catch-up
    std::string tangleData = tangle.serialize();
    std::string checksum = computeChecksum(tangleData);

    // Create JSON payload
    Json::Value jsonData;
    jsonData["data"] = tangleData;
    jsonData["checksum"] = checksum;
    jsonData["timestamp"] = std::to_string(std::time(nullptr));

    Json::StreamWriterBuilder writer;
    std::string jsonString = Json::writeString(writer, jsonData);

    // Send to the specific peer
    sendMessage(jsonString, "SYNC_ACK", peer);
    std::cout << "[SEND] Sent Tangle sync data to peer: " << peer.id << std::endl;
}

void Network::expireOrphans()
{
    // NOTE: caller must already hold orphansMutex
    int64_t now = timeNow();
    size_t totalCount = 0;

    for (auto it = orphans.begin(); it != orphans.end(); )
    {
        auto &list = it->second;
        for (auto listIt = list.begin(); listIt != list.end(); )
        {
            if (now - listIt->orphanedAt > orphanTTL)
            {
                listIt = list.erase(listIt);
            }
            else
            {
                ++listIt;
                ++totalCount;
            }
        }

        if (list.empty())
        {
            it = orphans.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Enforce hard cap: evict oldest entries if over limit
    while (totalCount > orphanPoolMax)
    {
        int64_t oldestTime = now;
        std::string oldestParent;
        size_t oldestIdx = 0;
        bool found = false;

        for (auto &pair : orphans)
        {
            for (size_t i = 0; i < pair.second.size(); ++i)
            {
                if (pair.second[i].orphanedAt < oldestTime)
                {
                    oldestTime = pair.second[i].orphanedAt;
                    oldestParent = pair.first;
                    oldestIdx = i;
                    found = true;
                }
            }
        }

        if (!found) break;

        auto it = orphans.find(oldestParent);
        if (it != orphans.end() && oldestIdx < it->second.size())
        {
            it->second.erase(it->second.begin() + oldestIdx);
            if (it->second.empty())
            {
                orphans.erase(it);
            }
        }
        --totalCount;
    }
}

void Network::processOrphans(const std::string &parentId)
{
    std::lock_guard<std::recursive_mutex> lock(orphansMutex);
    expireOrphans();

    auto it = orphans.find(parentId);
    if (it != orphans.end())
    {
        std::cout << "[ORPHAN] Processing " << it->second.size() << " orphans for parent " << parentId << std::endl;
        std::vector<Transaction> readyToProcess;

        // Check which orphans are now ready (all parents present)
        auto &orphanList = it->second;
        for (auto listIt = orphanList.begin(); listIt != orphanList.end(); )
        {
            bool allParentsPresent = true;
            for (const auto &p : listIt->tx.data.parents)
            {
                std::string pStr = p;
                if (tangle.getTransaction(pStr).data.transaction_id.empty() && p != "genesis")
                {
                    allParentsPresent = false;
                    break;
                }
            }

            if (allParentsPresent)
            {
                bool allFinal = true;
                for (const auto &p : listIt->tx.data.parents)
                {
                    if (p == "genesis") continue;
                    Transaction pTx = tangle.getTransaction(p);
                    if (pTx.metadata.status != TransactionStatus::FINAL)
                    {
                        allFinal = false;
                        break;
                    }
                }
                if (allFinal)
                {
                    readyToProcess.push_back(listIt->tx);
                    listIt = orphanList.erase(listIt);
                }
                else
                {
                    ++listIt; // Stay orphaned until all parents are FINAL
                }
            }
            else
            {
                ++listIt;
            }
        }

        if (orphanList.empty())
        {
            orphans.erase(it);
        }

        // Process the ready orphans (re-inject them as if they just arrived)
        for (auto &tx : readyToProcess)
        {
            std::cout << "[ORPHAN] Un-orphaning transaction " << tx.data.transaction_id << std::endl;

            int isTxPresent = tangle.addTransaction(tx, 1);
            if (isTxPresent == 2 || isTxPresent == 1)
            {
                postProcessAddedTransaction(tx.data.transaction_id);
                removeOrphanFromParentQueues(tx.data.transaction_id, tx.data.parents);
            }
        }
    }
}

// Phase 8: Per-peer rate limiting

bool Network::consumeToken(const std::string& peerUri)
{
    std::lock_guard<std::mutex> lock(quotaMutex);
    int64_t now = timeNow();
    PeerQuota &quota = peerQuotas[peerUri];

    // Initialize lastRefill on first use
    if (quota.lastRefill == 0)
    {
        quota.tokens = rateLimitBurst;
        quota.lastRefill = now;
    }

    double elapsedSec = (now - quota.lastRefill) / 1000.0;
    int peerCount = std::max(1, peers.countPeers());
    double reputation = (quota.validCount + quota.invalidCount > 0)
        ? static_cast<double>(quota.validCount) / (quota.validCount + quota.invalidCount)
        : 1.0;
    double refillRate = rateLimitBase * reputation / peerCount;

    quota.tokens = std::min(rateLimitBurst, quota.tokens + refillRate * elapsedSec);
    quota.lastRefill = now;

    if (quota.tokens >= 1.0)
    {
        quota.tokens -= 1.0;
        return true;
    }
    return false;
}

void Network::recordValidation(const std::string& peerUri, bool isValid)
{
    std::lock_guard<std::mutex> lock(quotaMutex);
    int64_t now = timeNow();
    PeerQuota &quota = peerQuotas[peerUri];

    quota.validationWindow.push_back({now, isValid});

    // Trim entries older than the sliding window
    while (!quota.validationWindow.empty() &&
           (now - quota.validationWindow.front().first) > rateLimitWindowMs)
    {
        quota.validationWindow.pop_front();
    }

    // Recalculate counts
    quota.validCount = 0;
    quota.invalidCount = 0;
    for (const auto& entry : quota.validationWindow)
    {
        if (entry.second)
            ++quota.validCount;
        else
            ++quota.invalidCount;
    }
}

void Network::handleTxAck(Peer &peer, const Json::Value &jsonData)
{
    try
    {
        string data = jsonData["data"].asString();
        string checksum = jsonData["checksum"].asString();

        cout << "[HANDLER] Received TX_ACK from peer." << endl;
        Transaction tx = deserializeTransaction(data);

        // Verify checksum
        if (!verifyChecksum(data, checksum))
        {
            cerr << "[HANDLER][ERROR] Checksum verification failed for TX_ACK." << endl;
            recordValidation(peer.uri, false);
            return;
        }

        // Validate signature 1 (sender)
        string txSerialized = serializeTransactionData(tx);
        if (!verifyTransaction(txSerialized, tx.metadata.signature1, tx.data.sender))
        {
            cerr << "[HANDLER][ERROR] TX_ACK signature1 verification failed. Dropping." << endl;
            recordValidation(peer.uri, false);
            return;
        }

        // Check for missing parents (still required for orphan resolution)
        bool missingParent = false;
        for (const auto &parent : tx.data.parents)
        {
            std::string parentId = parent;
            Transaction pTx = tangle.getTransaction(parentId);
            if (!tangle.transactionPresent(pTx) && parent != "genesis")
            {
                if (pTx.data.transaction_id.empty()) {
                    std::cout << "[ORPHAN] TX_ACK transaction " << tx.data.transaction_id << " missing parent " << parent << ". Queuing as orphan." << std::endl;
                    std::lock_guard<std::recursive_mutex> lock(orphansMutex);
                    expireOrphans();
                    orphans[parent].push_back({tx, timeNow()});
                    missingParent = true;
                    requestTransaction(parent, peer);
                }
            }
        }
        if (missingParent)
        {
            recordValidation(peer.uri, false);
            return;
        }

        // Add to tangle (no parent-status check for TX_ACK — this is catch-up data)
        int isTxPresent = tangle.addTransaction(tx, 1);
        if (isTxPresent == 2 || isTxPresent == 1)
        {
            std::cout << "[HANDLER] Applied TX_ACK for " << tx.data.transaction_id << "." << endl;
            postProcessAddedTransaction(tx.data.transaction_id);
        }
        else
        {
            std::cout << "[HANDLER] TX_ACK transaction already exists. No updates." << endl;
        }

        recordValidation(peer.uri, true);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[HANDLER][ERROR] Exception in handleTxAck: " << e.what() << std::endl;
        recordValidation(peer.uri, false);
    }
}

void Network::postProcessAddedTransaction(const std::string &txId, const std::string &excludeUri)
{
    try
    {
        Transaction tx = tangle.getTransaction(txId);
        if (tx.data.transaction_id.empty())
        {
            std::cerr << "[POSTPROCESS][ERROR] Transaction " << txId << " not found in Tangle." << std::endl;
            return;
        }

        const std::string uid = getenv("UID");

        // If this node is the receiver and sig2 is missing, double-sign
        if (tx.data.receiver == uid && tx.metadata.signature2.empty())
        {
            std::cout << "[POSTPROCESS] Transaction is for this node. Double signing it." << std::endl;
            string txSerialized = serializeTransactionData(tx);
            tx.metadata.signature2 = signTransaction(txSerialized);
            tx.metadata.lastUpdated = timeNow();
            tx.metadata.verificationTimestamp = timeNow();
            tx.metadata.verificationDuration = timeNow() - tx.data.timestamp;
            tangle.updateTransaction(tx);

            // Add implicit vote for sender (receiver = local node gets explicit vote below)
            tangle.addVote(txId, tx.data.sender);

            // Broadcast TX_APPROVAL delta
            Json::Value approvalJson;
            approvalJson["tx_id"] = tx.data.transaction_id;
            approvalJson["sig2"] = tx.metadata.signature2;
            approvalJson["receiver_id"] = uid;
            Json::StreamWriterBuilder writer;
            string approvalMsg = Json::writeString(writer, approvalJson);
            string approvalChecksum = computeChecksum(approvalMsg);
            Json::Value wrap;
            wrap["data"] = approvalMsg;
            wrap["checksum"] = approvalChecksum;
            wrap["timestamp"] = std::to_string(std::time(nullptr));
            string wrapStr = Json::writeString(writer, wrap);
            broadcastMessage(wrapStr, "TX_APPROVAL");

            // Explicit vote (local node); handles voted_by check internally
            createAndBroadcastVote(txId);
        }
        else if (!tx.metadata.signature2.empty())
        {
            // sig2 already present — add implicit votes for sender and receiver
            tangle.addVote(txId, tx.data.sender);
            tangle.addVote(txId, tx.data.receiver);
            createAndBroadcastVote(txId);
        }
        else
        {
            // No sig2 yet and we're not the receiver — just vote explicitly
            createAndBroadcastVote(txId);
        }

        // Broadcast full TX_PROPOSAL so peers have the latest state
        broadcastTxProposal(tangle.getTransaction(txId), excludeUri);

        // Resolve any orphans waiting for this transaction
        processOrphans(txId);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[POSTPROCESS][ERROR] Exception: " << e.what() << std::endl;
    }
}

void Network::removeOrphanFromParentQueues(const std::string& txId, const std::vector<std::string>& parents)
{
    std::lock_guard<std::recursive_mutex> lock(orphansMutex);
    for (const auto& parent : parents)
    {
        if (parent == "genesis") continue;
        auto it = orphans.find(parent);
        if (it == orphans.end()) continue;
        auto& list = it->second;
        for (auto lit = list.begin(); lit != list.end(); )
        {
            if (lit->tx.data.transaction_id == txId)
            {
                std::cout << "[ORPHAN] Cleaning ghost entry for " << txId
                          << " from parent " << parent << " queue." << std::endl;
                lit = list.erase(lit);
            }
            else
            {
                ++lit;
            }
        }
        if (list.empty())
        {
            orphans.erase(it);
        }
    }
}

// Phase 8: Per-peer rate limiting
