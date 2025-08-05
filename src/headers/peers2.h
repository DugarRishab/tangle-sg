// peers.h
#ifndef PEERS2_H
#define PEERS2_H

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <chrono>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <openssl/hmac.h>
#include <jsoncpp/json/json.h>
#include "tangle.h"
#include "network.h"
#include <thread>
#include <cstdint>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

// 	// Alias for WebSocket++ client
// using WsClient = websocketpp::client<websocketpp::config::asio_client>;
// using WebSocketPtr = std::shared_ptr<WsClient>;

using namespace std;

// Represents a generic message to send to a peer
struct Message
{
	std::string data;
};

// Represents a peer in the network
struct Peer
{
	std::string id;		 // UID of the peer
	std::string address; // IP address
	websocketpp::connection_hdl client_hdl; // WebSocket connection handle
	websocketpp::connection_hdl server_hdl;
	uint64_t nonce;
};

extern std::vector<Peer> activePeers; // Global store for active WebSocket connections

// Manages peers, discovery, HMAC-based handshake, and outgoing queue
class Peers
{
public:
	Peers(int port, Tangle &tangle, Network &net);
	~Peers();

	
	const std::vector<Peer> &getPeerList() const;

	void findPeers(int maxPeers = 5, int maxTimeLimitMs = 10000); // Discover peers with a timeout


private:
	std::vector<Peer> peers_;
	std::queue<Message> outgoingQueue_;

	mutable std::mutex queueMutex_, peersMutex_;

	std::string baseIP, broadcastIP;

	int sock;
	int port_;
	std::string secretK_; // HMAC secret

	bool running_;
	uint64_t NONCE_A; // Nonce for handshake
	std::string UID_A; // Unique identifier for this node

	Tangle &tangle; // Reference to the Tangle object
	Network &net; // Reference to the Network object

	std::vector<Peer> foundPeers_;
	std::mutex foundMutex_;
	std::condition_variable foundCv_;

	// TODO: using a standard Ed25519 tool
	// ed25519 - keygen
	// -- output - public node_X.pub -> 32bit public key
	// -- output - private node_X.key -> 64bit private key

	// then -> UID = Base58(PublicKey)

	// Utility
	void addPeer(const Peer &peer);

	uint64_t generateNonce();
	std::string computeHMAC(const std::string &data);
	void sendUDPPacket(const std::string &data, const sockaddr_in &addr);

	// Discovery
	void sendUDPBroadcast(const std::string &data);
	// std::vector<Peer> listenDiscovery(int maxPeers, int maxTimeLimitMs);

	// Handshake phases
	bool performHandshake(Peer p);
	void responderLoop();
	bool verifyHMAC(const Json::Value &msg);

	
	std::thread responderThread_;
	
};

#endif // PEERS_H