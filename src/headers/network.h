#ifndef NETWORK_H
#define NETWORK_H
#include "transaction.h"
#include "tangle.h"
#include <string>

#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

using namespace std;

using WsClient = websocketpp::client<websocketpp::config::asio_client>;
using ConnectionHdl = websocketpp::connection_hdl;
using MessagePtr = websocketpp::config::asio_client::message_type::ptr;

// void startServer(Tangle& tangle);
void broadcastTransaction(const Transaction &Tx);
void sendTangle(const Tangle &tangle, WebSocketPtr client, const ConnectionHdl &hdl);
void handleTangleUpdate(std::string receivedData, Tangle& tangle);
void setupMessageReceiver(WebSocketPtr client, Tangle &tangle);
void broadcastMessage(const std::string& message, const std::string& messageType);
void sendMessage(const std::string &message, const std::string &messageType, WebSocketPtr client, const ConnectionHdl &hdl);
void printLastTransaction(Tangle& tangle);
bool verifyChecksum(const std::string& data, const std::string& receivedChecksum);
std::string computeChecksum(const std::string& data);

#endif