// peerDiscovery.h
#ifndef PEERDISCOVERY_H
#define PEERDISCOVERY_H

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <chrono>
#include <unordered_map>
#include <memory>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <openssl/hmac.h>
#include <jsoncpp/json/json.h>

#include "tangle.h"
#include "peers2.h"

#include <thread>
#include <cstdint>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

using namespace std;

class PeerDiscovery
{
	public:
		void findPeers(int maxPeers = 5, int maxTimeLimitMs = 10000); // Discover peers with a timeout
		void responderLoop();
		void start();
		void Stop();

	private:
		Peers peers;
		mutable std::mutex peersMutex_;

		Network &net; // Reference to the Network object

		std::string baseIP, broadcastIP;

		int sock;
		int port_;
		std::string secretK_; // HMAC secret

		int ws_port;

		bool running_;
		uint64_t NONCE_A;  // Nonce for handshake
		std::string UID_A; // Unique identifier for this node

		std::thread responderThread_;

		uint64_t generateNonce();
		std::string computeHMAC(const std::string &data);
		void sendUDPPacket(const std::string &data, const sockaddr_in &addr);

		// Discovery
		void sendUDPBroadcast(const std::string &data);
		// std::vector<Peer> listenDiscovery(int maxPeers, int maxTimeLimitMs);

		// Handshake phases
		bool performHandshake(Peer p);

		bool verifyHMAC(const Json::Value &msg);
}
#endif // PEERDISCOVERY_H