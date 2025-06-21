#ifndef NETWORK_H
#define NETWORK_H
#include "transaction.h"
#include "tangle.h"
void startServer(Tangle& tangle);
void broadcastTangle(const Tangle& tangle);
#endif