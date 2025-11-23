#ifndef NETWORK_H
#define NETWORK_H
#include "transaction.h"
#include "tangle.h"
#include "peers2.h"

#include <string>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/client.hpp>

using namespace std;

using WsClient = websocketpp::client<websocketpp::config::asio_client>;
using ConnectionHdl = websocketpp::connection_hdl;
	Network(uint16_t wsPort, Tangle &tangle, Peers &peers);
	~Network();

	void broadcastTransaction(const Transaction &Tx);
	void sendTangle(Peer& peer);
	void handleTangleUpdate(std::string receivedData);
	void handleIncomingMessage(Peer& peer, const std::string &payload);
#ifndef NETWORK_H
#define NETWORK_H
#include "transaction.h"
#include "tangle.h"
#include "peers2.h"

#include <string>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/client.hpp>

using namespace std;

using WsClient = websocketpp::client<websocketpp::config::asio_client>;
using ConnectionHdl = websocketpp::connection_hdl;
	Network(uint16_t wsPort, Tangle &tangle, Peers &peers);
	~Network();

	void broadcastTransaction(const Transaction &Tx);
	void sendTangle(Peer& peer);
	void handleTangleUpdate(std::string receivedData);
	void handleIncomingMessage(Peer& peer, const std::string &payload);
	void broadcastMessage(const std::string &message, const std::string &messageType);
	void sendMessage(const string &message, const string &messageType, Peer& peer);
	void printLastTransaction();
	static bool verifyChecksum(const std::string &data, const std::string &receivedChecksum);
	static std::string computeChecksum(const std::string &data);
	bool connectWebSocket(Peer &peer);
	void scheduleReconnect(Peer &peer);
	void startPeerMonitor(std::chrono::milliseconds interval = std::chrono::seconds(30));
	void stopPeerMonitor();

private:
	uint16_t ws_port;
	std::shared_ptr<WsClient> client;
	std::shared_ptr<WsServer> server;

	std::thread monitorThread;
	std::atomic<bool> monitorRunning_{false};

	Tangle &tangle;
	Peers &peers; // Reference to Peers object

	void initClient();
	void initServer();

    // Orphan Pool
    std::unordered_map<std::string, std::vector<Transaction>> orphans;
    std::mutex orphansMutex;
    void requestTransaction(const std::string &txId, Peer &peer);
    void processOrphans(const std::string &parentId);
};

// void startServer(Tangle& tangle);

#endif