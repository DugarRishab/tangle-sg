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
using MessagePtr = websocketpp::config::asio_client::message_type::ptr;
using WebSocketPtr = std::shared_ptr<WsClient>;
using WsServer = websocketpp::server<websocketpp::config::asio>;

enum class ConnectionType // to determine the connection type
{
	Client,
	Server
};

class Network
{

public:
	Network(uint16_t wsPort, Tangle &tangle);
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
	void connectWebSocket(Peer &peer);
	void scheduleReconnect(Peer &peer);

private:
	uint16_t ws_port;
	std::shared_ptr<WsClient> client;
	std::shared_ptr<WsServer> server;

	Tangle &tangle;

	void initClient();
	void initServer();
};

// void startServer(Tangle& tangle);

#endif