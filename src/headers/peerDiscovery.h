// peerDiscovery.h
#ifndef PEERDISCOVERY_H
#define PEERDISCOVERY_H

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
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
#include "network.h"

#include <thread>
#include <cstdint>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

using namespace std;

class PeerDiscovery
{
	public:
		PeerDiscovery(int port, Network &net, Peers &peers);
		~PeerDiscovery();

		void findPeers(int maxPeers = 5, int maxTimeLimitMs = 10000); // Discover peers with a timeout
		void responderLoop();
		void start();
		void Stop();

	private:
		Peers &peers;
		mutable std::mutex peersMutex_;

		int maxPeers_ = 5; // Default max peers to discover

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
		std::thread discoveryThread_;
		void discoveryLoop();

		// Helper methods
		uint64_t generateNonce();
		std::string computeHMAC(const std::string &data);
		void sendUDPPacket(const std::string &data, const sockaddr_in &addr);
		void sendUDPBroadcast(const std::string &data);

		// Handshake phases
		bool performHandshake(Peer p);

		bool verifyHMAC(const Json::Value &msg, uint64_t expectedNonceA);

		// Replay protection
		struct PendingResponse {
			uint64_t nonce_A;
			std::string peerId;
			int64_t timestamp;
			PendingResponse(uint64_t n = 0, std::string p = "", int64_t t = 0)
				: nonce_A(n), peerId(std::move(p)), timestamp(t) {}
		};

		std::unordered_map<uint64_t, int64_t> pendingRequests_; // nonce -> timestamp
		std::unordered_map<uint64_t, PendingResponse> pendingResponses_;
		std::unordered_set<uint64_t> consumedNonces_;
		std::mutex nonceMutex_;
		int64_t nonceTTL_ms_ = 30000; // 30s

		uint64_t generateFreshNonce();
		void expirePendingNonces();
		bool isNonceConsumed(uint64_t nonce);
		void markNonceConsumed(uint64_t nonce);
};
#endif // PEERDISCOVERY_H