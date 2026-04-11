// peerDiscovery.cpp
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

#include "../headers/peerDiscovery.h"
#include "../headers/peers2.h"
#include "../headers/network.h"
#include "../headers/tangle.h"

using namespace std;

PeerDiscovery::PeerDiscovery(int port, Network &net, Peers &peers) : port_(port), running_(true), ws_port(9000), net(net), peers(peers)
{
	// Load HMAC secret
	char *env = std::getenv("HMAC_SECRET");
	if (!env)
	{
		std::cerr << "[Error]: HMAC_SECRET environment variable not set.\n";
		throw std::runtime_error("HMAC_SECRET not set");
	}
	secretK_ = env ? env : "";

	// Load UID from environment
	char *uid = std::getenv("UID");
	if (!uid)
	{
		std::cerr << "[Error]: UID environment variable not set.\n";
		throw std::runtime_error("UID not set");
	}
	UID_A = uid;

	// Initialize nonce and HMAC values
	std::random_device rd;						  // Seed generator
	std::mt19937_64 eng(rd());					  // Mersenne Twister seeded with rd
	std::uniform_int_distribution<uint64_t> dist; // Uniform distribution over all uint64_t

	NONCE_A = dist(eng);

	// Set base IP from environment variable
	const char *ip_base = std::getenv("BASE_IP");
	if (!ip_base)
	{
		std::cerr << "[ERROR]: BASE_IP environment variable not set\n";
		throw std::runtime_error("BASE_IP not set");
	}
	baseIP = ip_base;

	// Set broadcast IP
	std::vector<int> octets;
	std::istringstream iss(baseIP);
	std::string token;
	while (std::getline(iss, token, '.'))
	{
		try
		{
			int val = std::stoi(token);
			if (val < 0 || val > 255)
				throw std::out_of_range("octet");
			octets.push_back(val);
		}
		catch (...)
		{
			std::cerr << "[ERROR]: Invalid BASE_IP format: " << baseIP << "\n";
			throw std::runtime_error("Invalid BASE_IP format");
		}
	}
	if (octets.size() != 4)
	{
		std::cerr << "[ERROR]: BASE_IP must have 4 octets: " << baseIP << "\n";
		throw std::runtime_error("Invalid BASE_IP format");
	}

	octets[3] = 255;

	std::ostringstream bcast;
	bcast << octets[0] << "."
		  << octets[1] << "."
		  << octets[2] << "."
		  << octets[3];
	broadcastIP = bcast.str();

	// load maxPeers from environment variable
	const char *maxPeersEnv = std::getenv("MAX_PEERS");
	if (maxPeersEnv)
	{
		try
		{
			maxPeers_ = std::stoi(maxPeersEnv);
			if (maxPeers_ <= 0)
			{
				std::cerr << "[ERROR]: MAX_PEERS must be a positive integer\n";
				throw std::runtime_error("Invalid MAX_PEERS value");
			}
			std::cout << "[INFO] Max peers set to: " << maxPeers_ << "\n";
		}
		catch (const std::exception &e)
		{
			std::cerr << "[ERROR]: Invalid MAX_PEERS value: " << e.what() << "\n";
			throw;
		}
	}
	else
	{
		std::cout << "[INFO] MAX_PEERS not set, using default value of 5\n";
	}

	// Setup UDP socket
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	int opt = 1; // Enable broadcast option
	setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
	if (sock < 0)
	{
		std::cerr << "[Error]: Failed to create UDP socket.\n";
		throw std::runtime_error("Socket creation failed");
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port_);
	addr.sin_addr.s_addr = INADDR_ANY;
	bind(sock, (sockaddr *)&addr, sizeof(addr));

	fcntl(sock, F_SETFL, O_NONBLOCK);
}

PeerDiscovery::~PeerDiscovery()
{
	running_ = false;
	if (responderThread_.joinable())
		responderThread_.join();
	if (discoveryThread_.joinable())
		discoveryThread_.join();
	close(sock);
}

void PeerDiscovery::start()
{
	running_ = true;
	responderThread_ = std::thread(&PeerDiscovery::responderLoop, this);
	discoveryThread_ = std::thread(&PeerDiscovery::discoveryLoop, this);
}

void PeerDiscovery::Stop()
{
	running_ = false;
	if (responderThread_.joinable())
		responderThread_.join();
	if (discoveryThread_.joinable())
		discoveryThread_.join();
	close(sock);
}

uint64_t PeerDiscovery::generateNonce()
{
	std::random_device rd;
	std::mt19937_64 eng(rd());
	return eng();
}

std::string PeerDiscovery::computeHMAC(const std::string &data)
{
	unsigned int len;
	unsigned char *result = HMAC(
		EVP_sha256(), secretK_.data(), secretK_.size(),
		(unsigned char *)data.data(), data.size(), nullptr, &len);
	std::ostringstream oss;
	for (unsigned int i = 0; i < len; i++)
		oss << std::hex << (int)result[i];
	return oss.str();
}

void PeerDiscovery::sendUDPPacket(const std::string &data, const sockaddr_in &addr)
{
	sendto(sock, data.data(), data.size(), 0, (sockaddr *)&addr, sizeof(addr));
}

void PeerDiscovery::sendUDPBroadcast(const string &data)
{

	sockaddr_in broadcastAddr{};
	broadcastAddr.sin_family = AF_INET;
	broadcastAddr.sin_port = htons(port_);
	broadcastAddr.sin_addr.s_addr = inet_addr(broadcastIP.c_str()); // Broadcast address

	sendto(sock, data.c_str(), data.size(), 0, (sockaddr *)&broadcastAddr, sizeof(broadcastAddr));
}

bool PeerDiscovery::performHandshake(Peer p)
{
	uint64_t NONCE_B = p.nonce;

	std::ostringstream d3;
	d3 << UID_A << p.id << NONCE_B;
	std::string HMAC3 = computeHMAC(d3.str());
	Json::Value ack;
	ack["type"] = "HS_ACK";
	ack["from"] = UID_A;
	ack["nonce_B"] = (Json::UInt64)NONCE_B;
	ack["hmac"] = HMAC3;

	sockaddr_in dest{};
	dest.sin_family = AF_INET;
	dest.sin_port = htons(p.port); // whatever port you intend
	if (!inet_aton(p.address.c_str(), &dest.sin_addr))
	{
		throw std::runtime_error("Invalid peer IP: " + p.address);
		return false;
	}
	sendUDPPacket(Json::FastWriter().write(ack), dest);

	return true;
}

bool PeerDiscovery::verifyHMAC(const Json::Value &msg)
{
	if (!msg.isMember("from") || !msg.isMember("nonce_A") || !msg.isMember("nonce_B") || !msg.isMember("hmac"))
		return false;

	if (msg["nonce_A"].asUInt64() != NONCE_A)
		return false;

	std::string peerId = msg["from"].asString();
	uint64_t nonceB = msg["nonce_B"].asUInt64();

	std::ostringstream data;
	data << UID_A << peerId << NONCE_A << nonceB;

	return computeHMAC(data.str()) == msg["hmac"].asString();
}

void PeerDiscovery::findPeers(int maxPeers, int /*maxTimeLimitMs*/)
{
	// Phase 1: Send PEER_REQUEST
	std::cout << "[PD] Starting peer discovery with max " << maxPeers << " peers.\n";

	Json::Value msg;
	msg["type"] = "PEER_REQUEST";
	msg["from"] = UID_A;
	msg["nonce_A"] = (Json::UInt64)NONCE_A;
	std::string payload = Json::FastWriter().write(msg);

	sendUDPBroadcast(payload);
	std::cout << "[PD] Broadcast sent: " << payload << "\n";
}

void PeerDiscovery::responderLoop()
{
	char buf[2048];
	sockaddr_in sender;
	socklen_t slen = sizeof(sender);
	std::cout << "[PD] Responder loop started, listening for incoming packets...\n";

	while (running_)
	{
		int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (sockaddr *)&sender, &slen);
		if (n > 0)
		{
			std::cout << "[PD] Received packet from " << inet_ntoa(sender.sin_addr) << ":" << ntohs(sender.sin_port) << "\n";
			buf[n] = '\0';
			Json::Value msg;
			Json::Reader r;
			if (r.parse(buf, msg))
			{
				std::string type = msg["type"].asString();
				std::cout << "[PD] Packet type: " << type << "\n";

				if (msg["from"].asString() == UID_A)
				{
					std::cout << "[PD][WARN] Ignoring packet from self: " << UID_A << "\n";
					continue; // Ignore packets from self
				}

				if (type == "PEER_REQUEST")
				{
					// if (peers.countPeers() >= maxPeers_)
					// {
					// 	std::cout << "[PD][WARN] Max peers reached, ignoring PEER_REQUEST.\n";
					// 	continue; // Ignore if max peers reached
					// }
					// generate N2 and HMAC
					uint64_t N1 = msg["nonce_A"].asUInt64();
					uint64_t N2 = NONCE_A;
					std::string A_UID = msg["from"].asString();
					std::string B_UID = UID_A;
					std::ostringstream data;
					data << A_UID << B_UID << N1 << N2;
					std::string tag2 = computeHMAC(data.str());

					std::cout << "[PD] Responding to PEER_REQUEST from " << A_UID << "\n";
					Json::Value resp;
					resp["type"] = "HS_RESPONSE";
					resp["from"] = B_UID;
					resp["nonce_A"] = (Json::UInt64)N1;
					resp["nonce_B"] = (Json::UInt64)N2;
					resp["hmac"] = tag2;
					resp["port"] = port_;
					std::string out = Json::FastWriter().write(resp);
					std::cout << "[PD] Sending HS_RESPONSE: " << out << "\n";
					sendUDPPacket(out, sender);
				}
				else if (type == "HS_ACK")
				{
					// if(peers.countPeers() >= maxPeers_)
					// {
					// 	std::cout << "[PD][WARN] Max peers reached, ignoring HS_ACK.\n";
					// 	continue; // Ignore if max peers reached
					// }
					// Phase 3
					uint64_t N2 = msg["nonce_B"].asUInt64();
					std::string A_UID = msg["from"].asString();
					std::string B_UID = UID_A;
					std::string tag3 = msg["hmac"].asString();
					// Verify
					std::ostringstream d3;
					d3 << A_UID << B_UID << N2;
					if (computeHMAC(d3.str()) == tag3)
					{
						
						Peer p;
						p.id = A_UID;
						p.address = inet_ntoa(sender.sin_addr);
						p.port = (int)port_;
						p.nonce = N2;
						p.nextRetry = std::chrono::steady_clock::now() + std::chrono::seconds(2);
						p.uri = "ws://" + p.address + ":" + std::to_string(ws_port) + "/";
						net.connectWebSocket(p);

						if (peers.addPeer(p) == 0)
						{
							std::cout << "[PD][WARN] Peer with ID " << p.uri << " already exists. Skipping.\n";
							continue; // Peer already exists
						}
						std::cout << "[PD] Handshake successful with peer: " << p.uri << "\n";
					}
				}
				else if (type == "HS_RESPONSE")
				{
					std::cout << "[PD] HS_RESPONSE from " << msg["from"].asString() << "\n";

					// if (peers.countPeers() >= maxPeers_)
					// {
					// 	std::cout << "[PD][WARN] Max peers reached, ignoring HS_RESPONSE.\n";
					// 	continue; // Ignore if max peers reached
					// }

					if (!verifyHMAC(msg))
					{
						std::cerr << "[PD][ERROR] HMAC verification failed for HS_RESPONSE from " << msg["from"].asString() << "\n";
						continue;
					}

					Peer p;
					p.id = msg["from"].asString();
					p.address = inet_ntoa(sender.sin_addr);
					p.port = msg.isMember("port") ? msg["port"].asInt() : 0;
					p.nonce = msg["nonce_B"].asUInt64();
					p.uri = "ws://" + p.address + ":" + std::to_string(ws_port) + "/";

					std::cout << "[PD] Discovered peer: " << p.id << " at " << p.address << ":" << p.port << "\n";

					

					if (performHandshake(p))
					{
						// on success, add to active list
						net.connectWebSocket(p);

						if (peers.addPeer(p) == 0)
						{
							std::cout << "[PD][WARN] Peer with ID " << p.uri << " already exists. Skipping.\n";
							continue; // Peer already exists
						}

						std::cout << "[PD] Peer added: " << p.id << " at " << p.address << ":" << p.port << "\n";
						std::cout << "[PD] Total connected Peers: " << peers.countPeers() << "\n";
					}
					else
					{
						std::cout << "[PD] Handshake FAILED with "
								  << p.id << " at " << p.address << ":" << p.port << "\n";
					}
				}
				else
				{
					std::cout << "[PD] Ignoring non-HS_RESPONSE packet of type: " << type << "\n";
				}
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
}

void PeerDiscovery::discoveryLoop()
{
	std::cout << "[PD] Discovery loop started.\n";
	while (running_)
	{
        // Always try to discover more peers to ensure full network connectivity
        // The MAX_PEERS limit only applies to gossip fan-out, not connections.
        std::cout << "[PD] Periodic discovery check. Current peers: " << peers.countPeers() << ". Starting discovery.\n";
        findPeers(5, 5000); // Discover 5 peers at a time
		
		// Sleep for a while before next check
		// Use small sleep steps to allow quick shutdown
		for (int i = 0; i < 100; ++i) {
			if (!running_) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
	std::cout << "[PD] Discovery loop stopped.\n";
}
