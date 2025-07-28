// net_reactor.h
#ifndef NET_REACTOR_H
#define NET_REACTOR_H

#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <json/json.h>
#include <functional>
#include <memory>
#include <string>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

using WsServer = websocketpp::server<websocketpp::config::asio>;

namespace net
{

	using asio = boost::asio;
	using udp = asio::ip::udp;
	using WsClient = websocketpp::client<websocketpp::config::asio_client>;
	using ConnectionHdl = websocketpp::connection_hdl;

	// Callback signature for inbound JSON messages
	using JsonHandler = std::function<void(const Json::Value &, ConnectionHdl)>;

	class NetReactor
	{
	public:
		// ctor: io_context for networking, cpuPool for heavy work,
		//       udpPort for peer discovery, wsUri for WebSocket,
		//       discoveryIntervalSec for periodic discovery
		NetReactor(asio::io_context &ioc,
				   asio::thread_pool &cpuPool,
				   unsigned short udpPort,
				   const std::string &wsUri,
				   int discoveryIntervalSec);

		// start listening, connecting, and scheduling
		void start();

		// send a JSON message over WebSocket (thread‑safe via strand)
		void sendJson(const Json::Value &msg);

		// register your application handler for incoming JSON
		void setJsonHandler(JsonHandler handler);

	private:
		void startUdpListener();
		void setupWebSocket();
		void schedulePeerDiscovery();
		void broadcastPeerDiscovery();

		asio::io_context &ioc_;
		asio::strand<asio::io_context::executor_type> strand_;
		asio::thread_pool &cpuPool_;

		unsigned short udpPort_;
		std::string wsUri_;
		int discoveryIntervalSec_;

		std::shared_ptr<udp::socket> udpSocket_;
		std::shared_ptr<WsClient> wsClient_;
		std::shared_ptr<asio::steady_timer> discoveryTimer_;

		JsonHandler jsonHandler_;
		ConnectionHdl wsHdl_;
	};

} // namespace net

#endif // NET_REACTOR_H
