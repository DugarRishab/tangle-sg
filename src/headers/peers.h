// peers.h
#pragma once

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <queue>
#include <functional>
#include "headers/tangle.h"

// Choose a WebSocket library: WebSocket++ is header-only and supports C++11.
// Alternatively, uWebSockets offers high performance but is more involved to integrate.
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

using ws_client = websocketpp::client<websocketpp::config::asio_client>;
using connection_hdl = websocketpp::connection_hdl;

struct Update
{
	std::string payload;
};

class Peers
{
public:
	Peers(uint16_t port = 9000, Tangle &tangle);
	~Peers();

	// 2) Handlers for broadcast discovery
	void listenForRequests();	 // runs in its own thread
	void sendDiscoveryRequest(); // broadcast, gather within timeout

	// 4) Handshake function (HMAC-based)
	bool performHandshake(connection_hdl hdl);

	// 6) Outgoing queue processing
	void networkLoop(); // runs in its own thread
	void submitUpdate(const Update &u);

	// 8) Gossip integration callback
	void updatePeerList(const std::vector<std::string> &peers);

private:
	uint16_t _port;
	size_t _maxPeers;
	std::atomic<bool> _running;

	// WebSocket++ client and connections
	ws_client _wsClient;
	std::mutex _connMutex;
	std::unordered_map<std::string, connection_hdl> _peers;

	// Thread-safe outgoing queue
	std::mutex _queueMutex;
	std::condition_variable _queueCv;
	std::queue<Update> _outgoing;

	std::thread _listenerThread;
	std::thread _discoveryThread;
	std::thread _networkThread;

	// Helpers
	void onMessage(connection_hdl hdl, ws_client::message_ptr msg);
	void connectToPeer(const std::string &addr);
	void closePeer(const std::string &addr);
};
