#include "peers.h"
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <chrono>
#include <asio.hpp>		  // for UDP broadcast
#include <openssl/hmac.h> // for handshake


std::vector<std::string> Peers::getPeers()
{
	std::lock_guard<std::mutex> lk(_peerMutex);
	return _peers;
}

void Peers::sendDiscoveryRequest()
{
	const std::string request = "PEER_REQUEST";
	while (_running)
	{
		{
			std::lock_guard<std::mutex> lk(_peerMutex);
			if (_peers.size() >= _maxPeers)
			{
				std::this_thread::sleep_for(std::chrono::minutes(1));
				continue;
			}
		}

		sendBroadcast(request);
		auto start = std::chrono::steady_clock::now();
		char buf[256];
		sockaddr_in src;
		socklen_t srclen = sizeof(src);

		// listen ~5s for responses
		while (_running &&
			   std::chrono::steady_clock::now() - start < std::chrono::seconds(5))
		{
			ssize_t n = recvfrom(_sockfd, buf, sizeof(buf) - 1, 0,
								 (sockaddr *)&src, &srclen);
			if (n > 0)
			{
				buf[n] = '\0';
				std::string msg(buf);
				if (msg.rfind("PEER_RESPONSE", 0) == 0)
				{
					std::string peerAddr = msg.substr(strlen("PEER_RESPONSE") + 1);
					std::lock_guard<std::mutex> lk(_peerMutex);
					if (_peers.size() < _maxPeers &&
						std::find(_peers.begin(), _peers.end(), peerAddr) == _peers.end())
					{
						_peers.push_back(peerAddr);
						std::cout << "Discovered peer: " << peerAddr << "\n";
					}
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		// retry after a minute
		
		std::this_thread::sleep_for(std::chrono::minutes(1));
	}
}

void Peers::respondToPeerRequest()
{
	char buf[256];
	sockaddr_in src;
	socklen_t srclen = sizeof(src);

	while (_running)
	{
		ssize_t n = recvfrom(_sockfd, buf, sizeof(buf) - 1, 0,
							 (sockaddr *)&src, &srclen);
		if (n > 0 && std::string(buf, n) == "PEER_REQUEST")
		{
			// build "PEER_RESPONSE ip:port"
			char ipStr[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &src.sin_addr, ipStr, sizeof(ipStr));
			std::string myAddr = std::string(ipStr) + ":" + std::to_string(_port);
			std::string resp = "PEER_RESPONSE " + myAddr;
			sendUnicast(resp, src);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
}

void Peers::sendBroadcast(const std::string &msg)
{
	sockaddr_in baddr{};
	baddr.sin_family = AF_INET;
	baddr.sin_port = htons(_port);
	baddr.sin_addr.s_addr = inet_addr("255.255.255.255");
	sendto(_sockfd, msg.data(), msg.size(), 0,
		   (sockaddr *)&baddr, sizeof(baddr));
}

void Peers::sendUnicast(const std::string &msg, const sockaddr_in &dest)
{
	sendto(_sockfd, msg.data(), msg.size(), 0,
		   (sockaddr *)&dest, sizeof(dest));
}

bool Peers::performHandshake(connection_hdl hdl)
{
	// 1) Send HELLO + nodeID + nonceA
	// 2) Receive CHALLENGE + nonceA + nonceB + HMAC
	// 3) Verify HMAC; send AUTH + HMAC(nonceB||nonceA)
	// Return true on success
	return true;
}

void Peers::connectToPeer(const std::string &addr)
{
	// Establish WebSocket connection to ws://addr:_port
	// Use _wsClient to create connection, performHandshake, then store in _peers
}

void Peers::closePeer(const std::string &addr)
{
	std::lock_guard<std::mutex> lk(_connMutex);
	// find hdl, close and erase
}

void Peers::submitUpdate(const Update &u)
{
	{
		std::lock_guard<std::mutex> lk(_queueMutex);
		_outgoing.push(u);
	}
	_queueCv.notify_one();
}

void Peers::networkLoop()
{
	while (_running)
	{
		std::unique_lock<std::mutex> lk(_queueMutex);
		_queueCv.wait(lk, [&]
					  { return !_outgoing.empty() || !_running; });
		if (!_running && _outgoing.empty())
			break;

		Update u = _outgoing.front();
		_outgoing.pop();
		lk.unlock();

		// Send over all open WebSocket connections
		std::lock_guard<std::mutex> connLk(_connMutex);
		for (auto &kv : _peers)
		{
			auto hdl = kv.second;
			_wsClient.send(hdl, u.payload, websocketpp::frame::opcode::binary);
		}
	}
}

void Peers::updatePeerList(const std::vector<std::string> &peers)
{
	// Diff with _peers keys, for additions call connectToPeer, for removals closePeer
}

void Peers::onMessage(connection_hdl hdl, ws_client::message_ptr msg)
{
	// Handle incoming tangle updates or handshake messages
}