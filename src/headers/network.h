#ifndef NETWORK_H
#define NETWORK_H
#include "transaction.h"
#include "tangle.h"

using WsClient = websocketpp::client<websocketpp::config::asio_client>;
using ConnectionHdl = websocketpp::connection_hdl;
using MessagePtr = websocketpp::config::asio_client::message_type::ptr;

// void startServer(Tangle& tangle);
void broadcastTransaction(const Tangle& tangle);
void sendTangle(const Tangle& tangle, WsClient& client, const ConnectionHdl& hdl);
void handleTCPClient(std::string receivedData, Tangle& tangle);
void setupMessageReceiver(WsClient& client, Tangle& tangle);
void broadcastMessage(const std::string& message, const std::string& messageType);
void sendMessage(const std::string& message, const std::string& messageType, WsClient& client, const ConnectionHdl& hdl);
void printLastTransaction(Tangle& tangle);
void verifyChecksum(const std::string& data, const std::string& receivedChecksum);
void computeChecksum(const std::string& data);

#endif