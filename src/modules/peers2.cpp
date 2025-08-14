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

using namespace std;

Peers::Peers()
{
}

Peers::~Peers()
{
}

int Peers::addPeer(Peer &peer)
{
	std::lock_guard<std::mutex> lock(peersMutex_);
	auto it = peers_.find(peer.uri);
	if (it != peers_.end())
	{
		std::cerr << "[WARN][ADD] Peer with URI " << peer.uri << " already exists. Skipping.\n";
		return 0; // Peer already exists
	}
	
	peers_.emplace(peer.uri, peer); // Use ID as key for quick access
	std::cout << "[LOG][ADD] Peer added: " << peer.id << " at " << peer.uri << "\n";
	std::cout << "[LOG][ADD] Total connected Peers: " << peers_.size() << "\n";
	return 1;						// Peer added successfully
}

int Peers::removePeer(const std::string &uri)
{
	std::lock_guard<std::mutex> lock(peersMutex_);
	auto it = peers_.find(uri);
	if (it != peers_.end())
	{
		peers_.erase(it);
		return 1; // Peer removed successfully
	}

	std::cerr << "[WARN][REMOVE] Peer with ID " << uri << " not found.\n";
	return 0; // Peer not found
}

int Peers::updatePeer(Peer &peer)
{

	std::lock_guard<std::mutex> lock(peersMutex_);
	auto it = peers_.find(peer.uri);
	if (it != peers_.end())
	{
		it->second = peer; // Update the existing peer
		return 1;		   // Peer updated successfully
	}

	std::cerr << "[WARN][UPDATE] Peer with ID " << peer.uri << " not found.\n";
	return 0; // Peer not found
}

Peer Peers::getPeer(std::string uri)
{
	std::lock_guard<std::mutex> lock(peersMutex_);
	auto it = peers_.find(uri);
	if (it != peers_.end())
	{
		return it->second; // Return the found peer
	}

	std::cerr << "[WARN][GET] Peer with ID " << uri << " not found.\n";
	return Peer{}; // Peer not found
}

int Peers::countPeers()
{
	std::lock_guard<std::mutex> lock(peersMutex_);
	return peers_.size(); // Return the number of peers
}

int Peers::updatePeerState(const std::string &uri, ConnectionState newState)
{
	std::lock_guard<std::mutex> lock(peersMutex_);
	auto it = peers_.find(uri);
	if (it != peers_.end())
	{
		it->second.state = newState; // Update the state of the peer
		return 1;					 // State updated successfully
	}

	std::cerr << "[WARN][UPDATE_STATE] Peer with ID " << uri << " not found.\n";
	return 0; // Peer not found
}

std::unordered_map<std::string, Peer> Peers::getPeerList()
{
	std::lock_guard<std::mutex> lock(peersMutex_);
	return peers_;
}

Peer Peers::getRandomPeer()
{
	std::lock_guard<std::mutex> lock(peersMutex_);

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
	std::lock_guard<std::mutex> lock(peersMutex_);
	auto it = peers_.find(uri);
	if (it != peers_.end())
		it->second.outgoingQueue.push_back(qm);
}
bool Peers::drainOutgoingQueue(const std::string &uri, std::deque<Message> &outQ)
{
	std::lock_guard<std::mutex> lock(peersMutex_);
	auto it = peers_.find(uri);
	if (it == peers_.end())
		return false;
	outQ = it->second.outgoingQueue;

	return true;
}
