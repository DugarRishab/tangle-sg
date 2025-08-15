// peers2.cpp
#include "../headers/peers2.h"
#include <thread>
#include <iostream>
#include <random>
#include <sstream>
#include <cstring>
#include <mutex>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <openssl/hmac.h>
#include <stdexcept>
#include <cstdlib>
#include <jsoncpp/json/json.h>

#include "../headers/network.h"
#include "../headers/tangle.h"
#include "../headers/debug_lock.h"
#include <execinfo.h>

using namespace std;

Peers::Peers()
{
}

Peers::~Peers()
{
}

static void dump_backtrace_once()
{
	void *buf[32];
	int n = backtrace(buf, sizeof(buf) / sizeof(buf[0]));
	char **strs = backtrace_symbols(buf, n);
	std::cerr << "=== backtrace (begin) ===\n";
	for (int i = 0; i < n; ++i)
	{
		std::cerr << "[" << i << "] " << (strs ? strs[i] : "(null)") << "\n";
	}
	std::cerr << "=== backtrace (end) ===\n";
	free(strs);
}

int Peers::addPeer(Peer &peer)
{
	// std::lock_guard<std::mutex> lock(peersMutex_);
	DebugScopedLock<std::mutex> lock(peersMutex_, "peersMutex_", 10);
	auto it = peers_.find(peer.uri);
	if (it != peers_.end())
	{
		std::cerr << "[PEER][ADD][WARN]Peer with URI " << peer.uri << " already exists. Skipping.\n";
		return 0; // Peer already exists
	}
	
	peers_.emplace(peer.uri, peer); // Use ID as key for quick access
	std::cout << "[PEER][ADD] Peer added: " << peer.id << " at " << peer.uri << "\n";
	std::cout << "[PEER][ADD] Total connected Peers: " << peers_.size() << "\n";
	return 1;						// Peer added successfully
}

int Peers::removePeer(const std::string &uri)
{
	// std::lock_guard<std::mutex> lock(peersMutex_);
	DebugScopedLock<std::mutex> lock(peersMutex_, "peersMutex_", 10);
	auto it = peers_.find(uri);
	if (it != peers_.end())
	{
		peers_.erase(it);
		return 1; // Peer removed successfully
	}

	std::cerr << "[PEER][WARN][REMOVE] Peer with ID " << uri << " not found.\n";
	return 0; // Peer not found
}

int Peers::updatePeer(Peer &peer)
{

	DebugScopedLock<std::mutex> lock(peersMutex_, "peersMutex_", 10);
	auto it = peers_.find(peer.uri);
	if (it != peers_.end())
	{
		it->second = peer; // Update the existing peer
		return 1;		   // Peer updated successfully
	}

	std::cerr << "[PEER][WARN][UPDATE] Peer with ID " << peer.uri << " not found.\n";
	return 0; // Peer not found
}

Peer Peers::getPeer(std::string uri)
{
	DebugScopedLock<std::mutex> lock(peersMutex_, "peersMutex_", 10);
	auto it = peers_.find(uri);
	if (it != peers_.end())
	{
		return it->second; // Return the found peer
	}

	std::cerr << "[PEER][WARN][GET] Peer with ID " << uri << " not found.\n";
	// dump_backtrace_once();
	return Peer{}; // Peer not found
}

int Peers::countPeers()
{
	DebugScopedLock<std::mutex> lock(peersMutex_, "peersMutex_", 10);
	return peers_.size(); // Return the number of peers
}

int Peers::updatePeerState(const std::string &uri, ConnectionState newState)
{
	DebugScopedLock<std::mutex> lock(peersMutex_, "peersMutex_", 10);
	auto it = peers_.find(uri);
	if (it != peers_.end())
	{
		it->second.state = newState; // Update the state of the peer
		return 1;					 // State updated successfully
	}

	std::cerr << "[PEER][WARN][UPDATE_STATE] Peer with ID " << uri << " not found.\n";
	return 0; // Peer not found
}

std::unordered_map<std::string, Peer> Peers::getPeerList()
{
	DebugScopedLock<std::mutex> lock(peersMutex_, "peersMutex_", 10);
	return peers_;
}

Peer Peers::getRandomPeer()
{
	DebugScopedLock<std::mutex> lock(peersMutex_, "peersMutex_", 10);

	if (peers_.empty())
	{
		throw std::runtime_error("No peers available");
	}

	// Generate a random index
	static thread_local std::mt19937 gen{std::random_device{}()};
	std::uniform_int_distribution<size_t> dist(0, peers_.size() - 1);

	auto it = peers_.begin();
	std::advance(it, dist(gen)); // move iterator to the random position

	return it->second;
}
void Peers::enqueueMessage(const std::string &uri, Message &qm)
{
	DebugScopedLock<std::mutex> lock(peersMutex_, "peersMutex_", 10);
	auto it = peers_.find(uri);
	if (it != peers_.end())
		it->second.outgoingQueue.push_back(qm);
}
bool Peers::drainOutgoingQueue(const std::string &uri, std::deque<Message> &outQ)
{
	DebugScopedLock<std::mutex> lock(peersMutex_, "peersMutex_", 10);
	auto it = peers_.find(uri);
	if (it == peers_.end())
		return false;
	outQ.swap(it->second.outgoingQueue);

	return true;
}
