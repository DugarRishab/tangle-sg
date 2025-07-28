// net_reactor.cpp
#include "headers/net_reactor.h"
#include <iostream>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

// Required connection handle type
// 1. UDP - peer discovery
// 2. WebSocket - client connections
// 3. WebSocket - server connections
// 4. WebSocket - incoming messages
// 5. TCP/HTTP - REST API

namespace net
{
	using WsServer = websocketpp::server<websocketpp::config::asio>;

	struct Peer
	{
		// True if this hdl came in via the server side
		bool fromServer;
		std::shared_ptr<WsClient> client; // for outbound connections
		std::shared_ptr<WsServer> server; // for inbound connections
		ConnectionHdl hdl;
	};

	NetReactor::NetReactor(asio::io_context &ioc,
						   asio::thread_pool &cpuPool,
						   unsigned short udpPort,
						   unsigned short wsListenPort,
						   const std::string &wsConnectUri,
						   int discoveryIntervalSec)
		: ioc_(ioc), 
		strand_(ioc.get_executor()), 
		cpuPool_(cpuPool), 
		udpPort_(udpPort), 
		wsListenPort_(wsListenPort), 
		wsConnectUri_(wsConnectUri), 
		discoveryIntervalSec_(discoveryIntervalSec)
	{
	}

	void NetReactor::start()
	{
		startUdpListener();
		setupWebSocketServer();
		setupWebSocketClient();
		schedulePeerDiscovery();
	}

	void NetReactor::setJsonHandler(JsonHandler handler)
	{
		jsonHandler_ = std::move(handler);
	}

	void NetReactor::sendJson(const Json::Value &msg)
	{
		// Serialize
		std::string payload = Json::FastWriter().write(msg);
		// Post through strand to ensure ordered, non‑blocking sends
		asio::post(strand_, [client = wsClient_, hdl = wsHdl_, payload]()
		{
			websocketpp::lib::error_code ec;
			client->send(hdl, payload, websocketpp::frame::opcode::text, ec);

			if (ec) {
				std::cerr << "[WS SEND ERROR] " << ec.message() << "\n";
			} 
		});
	}

	void NetReactor::startUdpListener()
	{
		udpSocket_ = std::make_shared<udp::socket>(ioc_, udp::endpoint(udp::v4(), udpPort_));
		auto buffer = std::make_shared<std::array<char, 2048>>();
		auto sender = std::make_shared<udp::endpoint>();

		std::function<void()> doReceive;
		doReceive = [this, buffer, sender, &doReceive]()
		{
			udpSocket_->async_receive_from(
				asio::buffer(*buffer), *sender,
				[this, buffer, sender, &doReceive](boost::system::error_code ec, std::size_t len)
				{
					if (!ec)
					{
						std::string data(buffer->data(), len);
						// You can parse peer‑discovery JSON here, e.g.:
						// handlePeerDiscovery(data, *sender);
					}
					doReceive(); // re‑arm
				});
		};
		doReceive();
	}

	void NetReactor::setupWebSocket()
	{
		wsClient_ = std::make_shared<WsClient>();
		wsClient_->init_asio(&ioc_);

		// Capture the connection handle on open
		wsClient_->set_open_handler(
			[this](ConnectionHdl hdl)
			{
				wsHdl_ = hdl;
			});

		// Incoming messages → quick JSON parse → offload to cpuPool_
		wsClient_->set_message_handler(
			[this](ConnectionHdl hdl, WsClient::message_ptr msg)
			{
				std::string payload = msg->get_payload();
				// Quick JSON parse
				Json::Value root;
				Json::CharReaderBuilder rb;
				std::string errs;
				std::istringstream iss(payload);
				if (!Json::parseFromStream(rb, iss, &root, &errs))
				{
					std::cerr << "[JSON ERROR] " << errs << "\n";
					return;
				}
				// Offload any heavy validation to cpuPool_
				asio::post(cpuPool_, [this, hdl, root]()
				{
                	// (1) CPU‑heavy work: verify signatures, PoW, weight
                	// (2) Then hand back to application on the I/O strand:
					asio::post(strand_, [this, hdl, root](){
						if (jsonHandler_) jsonHandler_(root, hdl);
					}); 
				});
			});

		// Initiate connection
		websocketpp::lib::error_code ec;
		auto con = wsClient_->get_connection(wsUri_, ec);
		if (ec)
		{
			std::cerr << "[WS CONNECT ERROR] " << ec.message() << "\n";
			return;
		}
		wsClient_->connect(con);

		// Run the WebSocket's own loop (it will use our io_context)
		std::thread([client = wsClient_]() { client->run(); }).detach();
	}

	void NetReactor::schedulePeerDiscovery()
	{
		discoveryTimer_ = std::make_shared<asio::steady_timer>(ioc_, std::chrono::seconds(discoveryIntervalSec_));
		std::function<void()> doDiscover;
		doDiscover = [this, &doDiscover]()
		{
			discoveryTimer_->expires_after(std::chrono::seconds(discoveryIntervalSec_));
			discoveryTimer_->async_wait([this, &doDiscover](auto, auto)
			{
            	broadcastPeerDiscovery();
            	doDiscover(); 
			});
		};
		doDiscover();
	}

	void NetReactor::broadcastPeerDiscovery()
	{
		// TODO: build a JSON “PEER_REQUEST” and send via UDP broadcast
		// e.g. udpSocket_->send_to(..., endpoint("255.255.255.255", udpPort_), 0);
	}

} // namespace net
