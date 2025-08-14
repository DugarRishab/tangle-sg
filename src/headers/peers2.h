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

#include <thread>
#include <cstdint>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

// 	// Alias for WebSocket++ client
// using WsClient = websocketpp::client<websocketpp::config::asio_client>;
// using WebSocketPtr = std::shared_ptr<WsClient>;

using namespace std;

class Network; // forward declaration of class to prevent circular dependency with Peers

// Represents a generic message to send to a peer
struct Message
{
	std::string payload;
	websocketpp::frame::opcode::value opcode;
};

enum class ConnectionState
{
	DISCONNECTED,
	CONNECTING,
	OPEN,
	CLOSING,
	CLOSED,
	FAILED
};

// Represents a peer in the network
struct Peer
{
	std::string id;		 // UID of the peer
	std::string address; // IP address
	int port;
	std::string uri;
	websocketpp::connection_hdl client_hdl; // WebSocket connection handle
	websocketpp::connection_hdl server_hdl;
	uint64_t nonce;
	ConnectionState state = ConnectionState::DISCONNECTED;
	std::deque<Message> outgoingQueue;
	int retryCount = 0;
	std::chrono::steady_clock::time_point nextRetry;
};



// Manages peers, discovery, HMAC-based handshake, and outgoing queue
class Peers
{
public:
	Peers();
	~Peers();

	int addPeer(Peer &peer);
	int removePeer(const std::string &uri);
	int updatePeer(Peer &peer);
	Peer getPeer(std::string uri);
	int countPeers();
	int updatePeerState(const std::string &uri, ConnectionState newState);
	std::unordered_map<std::string, Peer> getPeerList();
	Peer getRandomPeer();

	void enqueueMessage(const std::string &uri, Message &qm);
	bool drainOutgoingQueue(const std::string &uri, std::deque<Message> &outgoingQueue);

private:
	// std::vector<Peer> peers_;
	std::unordered_map<std::string, Peer> peers_;
	mutable std::mutex peersMutex_;
	
};

#endif // PEERS_H