#ifndef NETWORK_H
#define NETWORK_H

#include "transaction.h"
#include "tangle.h"
#include "peers2.h"

#include <string>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <deque>
#include <chrono>

#include <jsoncpp/json/json.h>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/client.hpp>

using namespace std;

using WsClient = websocketpp::client<websocketpp::config::asio_client>;
using WsServer = websocketpp::server<websocketpp::config::asio>;
using ConnectionHdl = websocketpp::connection_hdl;

class Network
{
public:
	Network(uint16_t wsPort, Tangle &tangle, Peers &peers);
	~Network();

	void broadcastTxProposal(const Transaction &Tx, const std::string &excludeUri = "");
	void createAndBroadcastVote(const std::string& tx_id);
	void sendTangle(Peer& peer);
	void handleTangleUpdate(std::string receivedData);
	void handleIncomingMessage(Peer& peer, const std::string &payload);

	// NEW: lightweight message handlers
	void handleTxProposal(Peer& peer, const Json::Value &jsonData);
	void handleTxApproval(Peer& peer, const Json::Value &jsonData);
	void handleVote(Peer& peer, const Json::Value &jsonData);
	void broadcastMessage(const std::string &message, const std::string &messageType, const std::string &excludeUri = "");
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
	Peers &peers;

	void initClient();
	void initServer();

	// Orphan Pool
	struct OrphanEntry {
		Transaction tx;
		int64_t orphanedAt;
	};
	std::unordered_map<std::string, std::vector<OrphanEntry>> orphans;
	std::recursive_mutex orphansMutex;
	int64_t orphanTTL;
	size_t orphanPoolMax;
	void requestTransaction(const std::string &txId, Peer &peer);
	void processOrphans(const std::string &parentId);
	void expireOrphans();
	void postProcessAddedTransaction(const std::string &txId, const std::string &excludeUri = "");
	void removeOrphanFromParentQueues(const std::string& txId, const std::vector<std::string>& parents);

	// Per-peer rate limiting
	struct PeerQuota {
		double tokens = 0.0;
		int64_t lastRefill = 0;
		int validCount = 0;
		int invalidCount = 0;
		std::deque<std::pair<int64_t, bool>> validationWindow;
	};
	std::unordered_map<std::string, PeerQuota> peerQuotas;
	std::mutex quotaMutex;
	double rateLimitBase;
	double rateLimitBurst;
	int64_t rateLimitWindowMs;
	int gossipFanout_;
	bool consumeToken(const std::string& peerUri);
	void recordValidation(const std::string& peerUri, bool isValid);

	// Message handlers
	void handleTxAck(Peer& peer, const Json::Value &jsonData);
};

#endif // NETWORK_H