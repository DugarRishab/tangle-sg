# Tangle-SG: Directed Acyclic Graph (DAG) System Documentation
> This is a permissioned, distributed, decentralized, ledgering system for low trust enviroment. Example use-case - microgrid energy transactions.

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Core Components](#core-components)
4. [Transaction Lifecycle](#transaction-lifecycle)
5. [DAG Structure & Consensus](#dag-structure--consensus)
6. [Auto-Peering Mechanism](#auto-peering-mechanism)
7. [Transaction Broadcasting](#transaction-broadcasting)
8. [Performance Characteristics](#performance-characteristics)
9. [Caveats & Limitations](#caveats--limitations)
10. [Future Scope](#future-scope)

---

## Overview

**Tangle-SG** is a decentralized, DAG-based ledger system designed for microgrid energy transactions. It implements a directed acyclic graph (DAG) where transactions are organized as vertices with parent-child relationships, enabling parallel transaction processing and asynchronous consensus without requiring a global ordering mechanism like blockchain.

### Key Design Principles

- **Decentralized**: No central authority; all nodes are peers
- **Permissionless**: Any node can join and participate
- **Dual-Signature**: Both sender and receiver must cryptographically approve transactions
- **Asynchronous Consensus**: BFT vote-based finality without explicit consensus rounds

---

## Architecture

### High-Level System Design

```
┌─────────────────────────────────────────────────────────────┐
│                    Tangle-SG Node                           │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │   Network    │  │    Tangle    │  │    Peers     │      │
│  │   (WebSocket)│  │   (DAG)      │  │  (Discovery) │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
│         │                 │                  │              │
│         └─────────────────┼──────────────────┘              │
│                           │                                 │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │ Transaction  │  │   Tangle     │  │  Telemetry   │      │
│  │  (Signing)   │  │  (Consensus) │  │  (Metrics)   │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### Module Organization

```
src/
├── headers/
│   ├── transaction.h      # Transaction data structures
│   ├── tangle.h           # DAG storage and operations
│   ├── network.h          # WebSocket communication
│   ├── peers2.h           # Peer management
│   ├── peerDiscovery.h    # Auto-peering via UDP broadcast
│   ├── telemetry.h        # Metrics and monitoring
│   ├── utils.h            # Utility functions
│   └── ...
├── modules/
│   ├── transaction.cpp    # Transaction signing & verification
│   ├── tangle.cpp         # DAG operations
│   ├── network.cpp        # WebSocket server/client
│   ├── peers2.cpp         # Peer state management
│   ├── peerDiscovery.cpp  # UDP-based peer discovery
│   ├── telemetry.cpp      # Metrics collection
│   └── ...
└── main.cpp               # Application entry point
```

---

## Core Components

### 1. Transaction Structure

**File**: `@/src/headers/transaction.h:9-46`

```cpp
struct tx_data {
    std::string transaction_id;      // Unique ID: timestamp_sender_receiver
    std::string sender;              // Sender node UID
    std::string receiver;            // Receiver node UID
    double amount;                   // Energy units
    std::string unit;                // Unit type (e.g., "kWh")
    double price_per_unit;           // Tariff rate
    std::string currency;            // Currency code
    int64_t timestamp;               // Creation timestamp
    std::vector<std::string> parents; // References to parent transactions
};

struct tx_metadata {
    int64_t lastUpdated;                              // Last modification time
    std::unordered_set<std::string> weightMap;        // Nodes that approved this tx
    int cumulative_weight;                            // Total approval count
    std::string signature1;                           // Sender's signature
    std::string signature2;                           // Receiver's signature
    std::string checksum;                             // Data integrity hash
    int64_t consensusTimestamp;                       // When consensus reached
    int64_t consensusDuration;                        // Time to consensus (ms)
    int64_t verificationTimestamp;                    // When verified
    int64_t verificationDuration;                     // Verification time (ms)
    int64_t tsaDuration;                              // Tip Selection Algorithm time (ms)
    int64_t completionDuration;                       // Total completion time (ms)
    int64_t propagationDelay;                         // Total propagation time: last_hop - first_hop (ms)
    int64_t avgPropagationDelay;                      // Average propagation per hop (ms)
    std::vector<std::pair<int64_t, std::string>> hops; // Hop trace (timestamp, node_uid)
};
```

**Key Features**:

- **Immutable Data**: Transaction data (`tx_data`) is signed and never modified
- **Mutable Metadata**: Metadata tracks approval state and consensus progress
- **Hop Tracking**: Records every node that processes the transaction for audit trails
- **Dual Signatures**: Both `signature1` (sender) and `signature2` (receiver) required

### 2. Tangle (DAG Storage)

**File**: `@/src/headers/tangle.h:11-36`

The `Tangle` class manages the entire DAG:

```cpp
class Tangle {
public:
    Transaction getTransaction(std::string &transaction_id);
    std::unordered_map<std::string, Transaction> getAllTransactions();
    Transaction addNewTransaction(Transaction &tx);
    int addTransaction(Transaction& tx, int update = 0);
    void updateCumulativeWeightOfParents(vector<std::string> &parents, int weightIncrement = 1);
    void updateCumulativeWeight(const std::string& transaction_id, int weightIncrement = 1);
    std::string serialize();
    std::unordered_map<std::string, Transaction> deserialize(const std::string &data);
    void updateFromSerialized(const std::string& data);

    bool transactionPresent(Transaction& tx);
    bool transactionNeedsUpdate(Transaction &tx);
    int updateTransaction(Transaction &tx, int no_lock = 0);
    int updateTransactionMetrics(Transaction &tx);

    // Consensus threshold configuration
    void setConsensusThreshold(int threshold) { consensusThreshold = threshold; }
    int getConsensusThreshold() const { return consensusThreshold; }

    // Propagation metrics calculation
    void calculatePropagationMetrics(Transaction &tx);

private:
    std::unordered_map<std::string, Transaction> transactions;
    std::shared_mutex tangleMutex;
    int consensusThreshold = 3; // Default: tx reaches consensus when cumulative_weight >= 3
};
```

**Operations**:

- **Add Transaction**: Inserts new transaction with initial weight 0
- **Update Weight**: Increments cumulative weight when approved
- **Recursive Weight Propagation**: Updates parent weights up the DAG
- **Thread-Safe**: Uses `shared_mutex` for concurrent reads and exclusive writes

### 3. Network Module

**File**: `@/src/headers/network.h:24-63`

Handles all peer communication via WebSocket:

```cpp
class Network {
public:
    Network(uint16_t wsPort, Tangle &tangle, Peers &peers);
    void broadcastTransaction(const Transaction &Tx);
    void sendTangle(Peer& peer);
    void handleTangleUpdate(std::string receivedData);
    void handleIncomingMessage(Peer& peer, const std::string &payload);
    void broadcastMessage(const std::string &message, const std::string &messageType);
    void sendMessage(const string &message, const string &messageType, Peer& peer);
    void startPeerMonitor(std::chrono::milliseconds interval = std::chrono::seconds(30));
    // ... connection management
private:
    std::shared_ptr<WsClient> client;
    std::shared_ptr<WsServer> server;
    std::unordered_map<std::string, std::vector<Transaction>> orphans;
    std::mutex orphansMutex;
};
```

**Features**:

- **Dual WebSocket**: Both client and server for bidirectional communication
- **Peer Monitoring**: Periodic health checks and reconnection attempts
- **Orphan Pool**: Handles transactions with missing parents
- **Message Queuing**: Queues messages for disconnected peers

### 4. Peer Discovery & Management

**File**: `@/src/headers/peerDiscovery.h:32-77`

Auto-peering mechanism using UDP broadcast and HMAC-based handshake:

```cpp
class PeerDiscovery {
public:
    PeerDiscovery(int port, Network &net, Peers &peers);
    void findPeers(int maxPeers = 5, int maxTimeLimitMs = 10000);
    void responderLoop();
    void start();
    void Stop();
private:
    uint64_t NONCE_A;
    std::string UID_A;
    std::string secretK_;  // HMAC secret
    void discoveryLoop();
    bool performHandshake(Peer p);
    bool verifyHMAC(const Json::Value &msg);
};
```

**Handshake Protocol**:

1. **Phase 1**: Node A broadcasts `PEER_REQUEST` with `NONCE_A`
2. **Phase 2**: Node B responds with `PEER_RESPONSE` containing `NONCE_B` and HMAC
3. **Phase 3**: Node A verifies HMAC and sends `HS_ACK`
4. **Connection**: WebSocket connection established after successful handshake

---

## Transaction Lifecycle

### Phase 1: Sender Creates & Broadcasts Proposal (Tx1)

**File**: `@/src/modules/transaction.cpp:21-55`

```
Sender Node:
  1. Assemble transaction body
     {
       sender_id: S,
       receiver_id: R,
       units: U,
       tariff_rate: T,
       timestamp: t1
     }

  2. Sign with sender's private key
     sig_sender = Sign_SK_S(H(body))

  3. Select parents
     Parent A: Sender's last approved transaction
     Parent B: Global tip selected via weighted algorithm

  4. Broadcast Tx1
     Send: {tx JSON with sig1, parents}
```

**Code Flow**:

- `signTransaction()`: Uses libsodium's `crypto_sign_detached()` with sender's secret key
- `addNewTransaction()`: Stores in Tangle with status = PROPOSED, cumulative_weight = 0

### Phase 2: Network Gossip & Initial Verification

**File**: `@/src/modules/network.cpp:204-219`

```
Any Node:
  1. Receive Tx1

  2. Verify sender's signature
     Check: verifyTransaction(H(body), sig_sender, sender_uid)

  3. If valid: Store and gossip to peers
     If invalid: Drop
```

**Implementation**:

- `handleIncomingMessage()`: Receives and validates transactions
- `verifyTransaction()`: Uses libsodium's `crypto_sign_verify_detached()`
- `broadcastTransaction()`: Sends to all connected peers

### Phase 3: Receiver Detects & Inspects Proposal

**File**: `@/src/modules/network.cpp:281-296`

```
Receiver Node:
  1. Monitor for transactions where receiver_id == R

  2. Validate proposal content
     - Confirm units and tariff_rate
     - Verify sig_sender

  3. If acceptable: Proceed to in-place approval
```

### Phase 4: Receiver Signs & Broadcasts Approval

**File**: `@/src/modules/network.cpp:520-534`

```
Receiver Node:
  1. Detect transaction where receiver_id == self.uid
     - Verify sig_sender is valid
     - Validate transaction content (units, tariff_rate)

  2. Sign approval
     sig_receiver = Sign_SK_R(H(tx_data))

  3. Add sig_receiver to tx.metadata.signature2
     - Update tx.metadata.lastUpdated
     - tx.metadata.verificationTimestamp = timeNow()
     - tx.metadata.status transitions to APPROVED

  4. Broadcast lightweight TX_APPROVAL delta
     Send: {full tx JSON with both sig1 and sig2}
```

**Note**: The receiver does **not** create a separate "Tx2" transaction. The approval is an in-place metadata update (`signature2`, `status = APPROVED`) on the original transaction object. This preserves the DAG structure where each energy trade is represented by exactly one node. Receivers extend their reputation by selecting this transaction as a parent in their own future transactions, not by creating a separate approval node.

### Phase 5: Final Verification & Weight Update

**File**: `@/src/modules/network.cpp:539-583`

```
Any Node:
  1. Validate updated transaction
     - Verify sig_sender
     - Verify sig_receiver

  2. If valid: Update transaction in Tangle
     - Store or update the transaction with both signatures
     - Transaction is now bilaterally signed (status = APPROVED)

  3. Update cumulative weight
     - Increment tx.metadata.cumulative_weight for all ancestors (BFS)
     - Uses children index for O(1) tip detection

  4. Update tip selection eligibility
     - Only APPROVED or FINAL transactions with reference_count == 0 are eligible as parents
     - Unapproved proposals are not selected by TSA
```

**Weight Propagation Algorithm**:

```cpp
void updateDescendantWeight(const std::string& tx_id, int increment = 1) {
    std::queue<std::string> q;
    for (const auto& parent : transactions[tx_id].data.parents) {
        q.push(parent);
    }
    while (!q.empty()) {
        std::string current = q.front(); q.pop();
        auto it = transactions.find(current);
        if (it != transactions.end()) {
            it->second.metadata.cumulative_weight += increment;
            it->second.metadata.lastUpdated = timeNow();
            for (const auto& grandparent : it->second.data.parents) {
                q.push(grandparent);
            }
        }
    }
}
```

- **Iterative BFS**: Replaces recursive DFS to avoid stack overflow and lock contention
- **Children Index**: `reference_count` tracks direct children for O(1) tip detection
- **Descendant Count**: `cumulative_weight` counts all descendants (direct + indirect)

---

## DAG Structure & Consensus

### DAG Properties

1. **Directed**: Edges point from child to parent (reverse chronological)
2. **Acyclic**: No cycles possible due to timestamp ordering
3. **Weighted**: Cumulative weight represents approval count
4. **Immutable**: Transaction data never changes after creation

### Tip Selection Algorithm

**Current Implementation**: Uses children index + status filtering

```
Tips = {transactions with reference_count == 0 AND status >= APPROVED}
Preferred Tips = {tips with cumulative_weight == 0}
Other Tips     = {tips with cumulative_weight > 0}
Selected Tip   = Random selection from Preferred Tips, fallback to Other Tips
```

**Rationale**:

- Encourages building on approved transactions
- Prevents spam by deprioritizing low-weight tips
- Maintains DAG connectivity

### Consensus Mechanism

**Type**: Asynchronous BFT vote-based finality

```
Consensus Threshold = ceil(2/3 * TOTAL_NODES) (derived from TOTAL_NODES env var)
PROPOSED  -> sig1 only
APPROVED  -> sig1 + sig2 (both parties verified)
FINAL     -> votes >= consensusThreshold (BFT finality achieved)
```

**Consensus Detection**:
When a transaction's vote count reaches the BFT threshold:

1. `consensusTimestamp` is set to current time
2. `consensusDuration` is calculated as `consensusTimestamp - transaction_timestamp`
3. `propagationDelay` and `avgPropagationDelay` are calculated from hop history
4. A log message is emitted: `[TANGLE][CONSENSUS] Transaction X reached consensus!`

**Key Characteristics**:

- **No explicit rounds**: Consensus emerges as votes accumulate
- **Deterministic BFT finality**: Once votes >= threshold, transaction is FINAL
- **Async-friendly**: Nodes don't need to wait for global consensus rounds
- **Configurable**: Threshold can be adjusted based on network size and security requirements
- **Metrics tracked**: Consensus time, propagation delay, and average per-hop delay are all recorded

### Example DAG Structure

```
                    Tx5 (weight: 3)
                   /    \
                Tx3      Tx4 (weight: 2)
               /  \        /
            Tx1    Tx2 (weight: 1)
             |      |
           Genesis  Genesis

Legend:
- Tx1, Tx2: Fully approved transactions (sig1 + sig2), eligible as TSA parents
- Tx3, Tx4, Tx5: New transactions that reference approved transactions as parents
- Weight increases as more nodes process the transaction and as more children reference it
```

---

## Auto-Peering Mechanism

### Overview

Tangle-SG uses **UDP broadcast-based peer discovery** with **HMAC-based handshake** for secure, decentralized peer finding.

**File**: `@/src/modules/peerDiscovery.cpp:149-242`

### Discovery Protocol

#### Phase 1: Broadcast PEER_REQUEST

```
Node A (Initiator):
  1. Generate random NONCE_A
  2. Broadcast UDP packet to subnet broadcast address:
     {
       "type": "PEER_REQUEST",
       "from": UID_A,
       "nonce_A": NONCE_A
     }
  3. Listen for responses
```

**Configuration**:

- **Port**: Configurable via environment (default: UDP port from config)
- **Broadcast Address**: Derived from `BASE_IP` environment variable
    - Example: If `BASE_IP=192.168.1.100`, broadcast to `192.168.1.255`
- **Max Peers**: Configurable via `MAX_PEERS` environment variable

#### Phase 2: Responder Sends PEER_RESPONSE

```
Node B (Responder):
  1. Receive PEER_REQUEST from Node A
  2. Generate random NONCE_B
  3. Compute HMAC:
     HMAC = HMAC_SHA256(secretK, A_UID || B_UID || N1 || N2)
  4. Send PEER_RESPONSE:
     {
       "type": "PEER_RESPONSE",
       "from": UID_B,
       "nonce_A": NONCE_A,
       "nonce_B": NONCE_B,
       "hmac": HMAC,
       "ws_port": 9000,
       "address": "192.168.1.X"
     }
```

#### Phase 3: Handshake Acknowledgment

```
Node A:
  1. Receive PEER_RESPONSE from Node B
  2. Verify HMAC:
     HMAC_computed = HMAC_SHA256(secretK, A_UID || B_UID || N1 || N2)
     Assert HMAC_computed == HMAC_received
  3. If valid: Send HS_ACK
     {
       "type": "HS_ACK",
       "from": UID_A,
       "nonce_B": NONCE_B,
       "hmac": HMAC_SHA256(secretK, UID_A || UID_B || NONCE_B)
     }
  4. Establish WebSocket connection to Node B
```

### Security Mechanisms

1. **HMAC-Based Authentication**:
    - Shared secret (`HMAC_SECRET`) prevents unauthorized peer claims
    - Nonce prevents replay attacks
    - Both nodes must know the secret to complete handshake

2. **Self-Identification Check**:
    - Nodes ignore packets from themselves
    - Prevents self-peering loops

3. **Nonce Verification**:
    - Each handshake uses unique nonces
    - Prevents replay of old handshakes

### Configuration

**Environment Variables**:

```bash
BASE_IP=192.168.1.0          # Subnet for broadcast discovery
HMAC_SECRET=shared_secret_key # Shared secret for handshake
UID=node_unique_identifier    # Node's unique ID
MAX_PEERS=5                   # Maximum peers to discover
```

### Peer State Management

**File**: `@/src/headers/peers2.h:44-68`

```cpp
enum class ConnectionState {
    DISCONNECTED,   // Not connected
    CONNECTING,     // Connection in progress
    OPEN,          // WebSocket established
    CLOSING,       // Close in progress
    CLOSED,        // Closed
    FAILED         // Connection failed
};

struct Peer {
    std::string id;                              // UID
    std::string address;                         // IP address
    int port;                                    // WebSocket port
    std::string uri;                             // ws://address:port
    websocketpp::connection_hdl client_hdl;      // Client handle
    websocketpp::connection_hdl server_hdl;      // Server handle
    uint64_t nonce;                              // Handshake nonce
    ConnectionState state;                       // Current state
    std::deque<Message> outgoingQueue;           // Queued messages
    int retryCount;                              // Reconnection attempts
    std::chrono::steady_clock::time_point nextRetry; // Next retry time
};
```

### Peer Monitoring & Reconnection

**File**: `@/src/modules/network.cpp:54-137`

```cpp
void Network::startPeerMonitor(std::chrono::milliseconds interval) {
    // Periodic health checks every `interval` milliseconds
    // For each peer:
    //   1. If OPEN: Drain outgoing message queue
    //   2. If DISCONNECTED/FAILED: Attempt reconnection
    //   3. If max retries exceeded: Mark as FAILED
    //   4. Exponential backoff: nextRetry = now + 2 * retryCount seconds
}
```

**Retry Strategy**:

- **Max Retries**: 3 attempts per peer
- **Backoff**: 2 seconds × retry count
    - 1st retry: 2 seconds
    - 2nd retry: 4 seconds
    - 3rd retry: 6 seconds
    - After 3rd: Marked as FAILED

---

## Transaction Broadcasting

### Broadcasting Strategy

**File**: `@/src/modules/network.cpp:30-34`

```cpp
void Network::broadcastTxProposal(const Transaction &Tx) {
    // Send transaction proposal to all connected peers
    // Message format: JSON serialized transaction
    // Message type: "TX_PROPOSAL"
}

void Network::broadcastMessage(const std::string &message, const std::string &messageType) {
    // Generic broadcast to all peers
    // Handles message queuing for disconnected peers
}
```

### Message Format

**Transaction Message**:

```json
{
	"type": "TRANSACTION",
	"payload": {
		"data": {
			"transaction_id": "1234567890_nodeA_nodeB",
			"sender": "nodeA",
			"receiver": "nodeB",
			"amount": 10.5,
			"unit": "kWh",
			"price_per_unit": 5.0,
			"currency": "USD",
			"timestamp": 1234567890,
			"parents": ["parent1_id", "parent2_id"]
		},
		"metadata": {
			"lastUpdated": 1234567890,
			"weightMap": ["nodeA", "nodeB", "nodeC"],
			"cumulative_weight": 3,
			"signature1": "base64_encoded_sig1",
			"signature2": "base64_encoded_sig2",
			"hops": [
				{ "timestamp": 1234567890, "uid": "nodeA" },
				{ "timestamp": 1234567891, "uid": "nodeB" }
			]
		}
	}
}
```

### Tangle Synchronization

**File**: `@/src/modules/network.cpp:31-32`

```cpp
void Network::sendTangle(Peer& peer) {
    // Serialize entire Tangle and send to peer
    // Used for initial sync or peer catch-up
}

void Network::handleTangleUpdate(std::string receivedData) {
    // Deserialize received Tangle and merge with local state
    // Update transactions that are newer or missing
}
```

### Orphan Handling

**File**: `@/src/modules/network.cpp:58-62`

```cpp
std::unordered_map<std::string, std::vector<Transaction>> orphans;

void Network::processOrphans(const std::string &parentId) {
    // When a parent transaction arrives, process any orphans
    // that were waiting for this parent
}

void Network::requestTransaction(const std::string &txId, Peer &peer) {
    // Request missing transaction from peer
}
```

**Orphan Resolution**:

1. Transaction arrives with missing parents
2. Stored in orphan pool
3. When parent arrives, orphans are validated and added to Tangle
4. If parent never arrives, orphans are pruned after timeout

### Message Queuing

**File**: `@/src/headers/peers2.h:37-42`

```cpp
struct Message {
    std::string payload;
    websocketpp::frame::opcode::value opcode;
};

// Each peer has:
std::deque<Message> outgoingQueue;
```

**Queue Management**:

- Messages queued when peer is DISCONNECTED
- Queue drained when peer transitions to OPEN
- Prevents message loss during temporary disconnections

---

## Performance Characteristics

### Time Complexity

| Operation        | Complexity | Notes                                    |
| ---------------- | ---------- | ---------------------------------------- |
| Add Transaction  | O(1)       | Hash map insertion                       |
| Get Transaction  | O(1)       | Hash map lookup                          |
| Update Weight    | O(d)       | d = DAG depth (recursive parent updates) |
| Broadcast        | O(p)       | p = number of peers                      |
| Tip Selection    | O(t)       | t = number of tips                       |
| Serialize Tangle | O(n)       | n = total transactions                   |

### Space Complexity

| Component      | Space | Notes                                  |
| -------------- | ----- | -------------------------------------- |
| Transaction    | O(1)  | Fixed-size metadata + variable parents |
| Tangle         | O(n)  | n = total transactions                 |
| Peer List      | O(p)  | p = number of peers                    |
| Orphan Pool    | O(o)  | o = orphaned transactions              |
| Message Queues | O(m)  | m = total queued messages              |

### Throughput

**Factors Affecting Throughput**:

1. **Network Latency**: Gossip propagation time
    - Local network: ~10-50 ms
    - Internet: ~100-500 ms

2. **Signature Verification**: Libsodium operations
    - Per transaction: ~1-2 ms

3. **Weight Propagation**: Iterative BFS parent updates
    - DAG depth 10: ~1-5 ms
    - DAG depth 100: ~10-50 ms

**Estimated Throughput** (single node):

- **Best case**: 1000+ tx/sec (local network)
- **Realistic case**: 100-500 tx/sec (internet latency)
- **Conservative case**: 10-100 tx/sec (high latency, deep DAG)

### Memory Usage

**Per Node**:

- Base: ~10-50 MB
- Per 1000 transactions: ~10-20 MB
- Per peer: ~1-2 MB
- Per queued message: ~1-10 KB

**Example**: 10,000 transactions + 10 peers

- Tangle: ~100-200 MB
- Peers: ~10-20 MB
- Total: ~110-220 MB

### Latency Breakdown

**Transaction Lifecycle** (end-to-end):

```
Sender Signing:       1-2 ms (libsodium sign)
Broadcast:            10-50 ms (network latency)
Receiver Processing:  1-5 ms
Broadcast Approval:   10-50 ms
Verification:         1-2 ms
Weight Propagation:   1-50 ms (depends on DAG depth)
─────────────────────────────
Total:                24-159 ms (typical: 50-100 ms)
```

---

## Caveats & Limitations

### 1. Finality Implemented ✓

**Status**: ✅ **Implemented**

**Implementation**:

- Consensus threshold derived from `TOTAL_NODES` env var: `ceil(2/3 * TOTAL_NODES)` (default: 10 nodes -> threshold 7)
- `TransactionStatus` enum: PROPOSED (sig1) -> APPROVED (sig1+sig2) -> FINAL (votes >= threshold)
- `consensusTimestamp`, `consensusDuration`, `propagationDelay`, and `avgPropagationDelay` are calculated and stored
- Log messages track consensus events for monitoring

**Note**: While consensus detection is implemented, transaction pruning is still future scope (see Caveat #3).

### 2. Centralized Tip Selection

**Issue**:

- Current implementation: Random selection from preferred tips
- No sophisticated tip selection algorithm (e.g., MCMC, weighted random walk)
- Vulnerable to tip-splitting attacks

**Impact**:

- Possible DAG fragmentation
- Reduced consensus efficiency
- Attackers could create isolated sub-DAGs

**Mitigation**:

- Implement weighted random walk (like IOTA)
- Use cumulative weight as selection probability
- Add tip age penalty to discourage old tips

### 3. No Transaction Pruning

**Issue**:

- All historical transactions kept in memory
- No archival or snapshot mechanism
- Tangle grows indefinitely

**Impact**:

- Memory exhaustion on long-running nodes
- Slow serialization/deserialization
- Network sync becomes expensive

**Mitigation**:

- Implement periodic snapshots
- Archive confirmed transactions to disk
- Implement state channels for off-tangle transactions

### 4. Synchronous Weight Propagation

**Issue**:

- Weight updates propagate recursively up the DAG
- Deep DAGs cause cascading updates
- Potential for lock contention

**Impact**:

- O(d) time complexity for weight updates (d = depth)
- Possible performance degradation with deep DAGs
- Mutex contention on high-concurrency scenarios

**Mitigation**:

- Implement lazy weight propagation
- Use lock-free data structures
- Batch weight updates

### 5. No Partition Tolerance

**Issue**:

- Network partitions cause divergent DAGs
- No consensus mechanism to resolve partitions
- Nodes may accept conflicting transactions

**Impact**:

- Double-spending possible during partitions
- Partition healing creates conflicts
- No guaranteed consistency across partitions

**Mitigation**:

- Implement Byzantine Fault Tolerance (BFT)
- Add conflict resolution rules
- Implement partition detection and healing

### 6. Weak Sybil Attack Resistance

**Issue**:

- No economic cost to create identities
- Attacker can create many fake peers and transactions
- BFT threshold assumes honest majority (<= 1/3 malicious)

**Impact**:

- Attacker can spam network with fake transactions
- Attacker can create many fake peers
- If >1/3 nodes are malicious, BFT guarantees break

**Mitigation**:

- Implement reputation system based on cumulative weight
- Add stake-based identity binding
- Implement rate limiting per peer
- Use permissioned network for production deployments

### 7. No Transaction Ordering Guarantee

**Issue**:

- DAG allows parallel transactions
- No global ordering of transactions
- Conflicting transactions possible

**Impact**:

- Applications must handle concurrent updates
- No ACID guarantees
- Possible race conditions in state updates

**Mitigation**:

- Implement application-level ordering
- Use vector clocks for causal ordering
- Implement conflict-free replicated data types (CRDTs)

### 8. Orphan Pool Unbounded Growth

**Issue**:

- Orphaned transactions stored indefinitely
- No timeout for orphan cleanup
- Attacker can fill orphan pool

**Impact**:

- Memory exhaustion
- Slow orphan processing
- Denial-of-service vulnerability

**Mitigation**:

- Implement orphan timeout (e.g., 5 minutes)
- Limit orphan pool size
- Implement orphan garbage collection

### 9. No Transaction Expiration

**Issue**:

- Transactions never expire
- Old proposals can be approved indefinitely
- Receiver can approve old proposals after long delay

**Impact**:

- Stale transactions can be finalized
- Possible replay of old transactions
- Confusion about transaction state

**Mitigation**:

- Add transaction TTL (time-to-live)
- Reject approvals for expired transactions
- Implement transaction versioning

### 10. Peer Discovery Limited to Local Subnet

**Issue**:

- UDP broadcast only reaches local subnet
- Cannot discover peers across internet
- Requires manual peer configuration for WAN

**Impact**:

- Difficult to deploy across multiple networks
- Requires VPN or manual peering
- Limits network scalability

**Mitigation**:

- Implement DHT-based peer discovery
- Add bootstrap node mechanism
- Support DNS-based peer discovery

---

## Future Scope

### Planned Features

#### 1. Finality & Pruning

- [ ] Define finality threshold based on network size
- [ ] Implement snapshot mechanism
- [ ] Archive old transactions to disk
- [ ] Implement state channels for off-tangle transactions

#### 2. Advanced Tip Selection

- [ ] Implement weighted random walk (MCMC)
- [ ] Add tip age penalty
- [ ] Implement tip quality scoring
- [ ] Add anti-spam tip selection

#### 3. Partition Tolerance

- [ ] Implement Byzantine Fault Tolerance (BFT)
- [ ] Add conflict resolution rules
- [ ] Implement partition detection
- [ ] Add partition healing mechanism

#### 4. Enhanced Security

- [ ] Increase PoW difficulty
- [ ] Implement reputation system
- [ ] Add stake-based identity binding
- [ ] Implement rate limiting per peer

#### 5. Scalability

- [ ] Implement sharding
- [ ] Add state channels
- [ ] Implement rollups
- [ ] Add hierarchical DAG structure

#### 6. Network Improvements

- [ ] Implement DHT-based peer discovery
- [ ] Add bootstrap node mechanism
- [ ] Support DNS-based peer discovery
- [ ] Add peer reputation scoring

#### 7. Monitoring & Observability

- [ ] Enhanced telemetry
- [ ] DAG visualization
- [ ] Performance profiling
- [ ] Network health monitoring

---

## Configuration & Deployment

### Environment Variables

```bash
# Node Identity
UID=node_unique_id                    # Node's unique identifier
SK_b64=base64_encoded_secret_key      # Sender's secret key (base64)
PK_b64=base64_encoded_public_key      # Sender's public key (base64)

# Network Configuration
BASE_IP=192.168.1.0                   # Subnet for peer discovery
HMAC_SECRET=shared_secret_key         # Shared secret for handshake
MAX_PEERS=5                           # Maximum peers to discover
MONITOR_PERIOD=5                      # Peer monitor interval (seconds)

# Consensus Parameters
POW=3                                 # Proof-of-Work difficulty (2-5)
CONSENSUS_THRESHOLD=3                 # Min cumulative weight for consensus

# Simulation & Robustness
TX_COUNT=10                           # Number of transactions to generate
TX_DELAY=30                           # Seconds between transactions
WAIT_PERIOD=300                       # Seconds to wait after last transaction
SIMULATION_TIMEOUT=3600               # Max simulation duration (seconds)
IDLE_TIMEOUT=600                      # Exit if no activity (seconds)
AUTOSAVE_INTERVAL=300                 # Auto-save interval (seconds)

# Storage
TANGLE_SNAPSHOT_INTERVAL=3600         # Snapshot interval (seconds, not implemented)
ARCHIVE_PATH=/path/to/archive         # Archive directory (not implemented)
```

### Building

```bash
cd tangle-sg
make clean
make
./tangle_poc
```

### Dependencies

- **C++17** or later
- **libsodium**: Cryptographic signatures
- **OpenSSL**: SHA256, HMAC
- **jsoncpp**: JSON serialization
- **websocketpp**: WebSocket communication
- **Boost**: System and threading libraries
- **libcurl**: HTTP requests (optional)

---

## References

### Related Documents

- `README.md`: Transaction lifecycle overview
- `TODO.md`: Feature roadmap
- `install2.sh`: Deployment script

### Key Algorithms

- **Proof-of-Work**: SHA256-based difficulty
- **Signatures**: Ed25519 (libsodium)
- **Hashing**: SHA256
- **HMAC**: HMAC-SHA256

### Standards

- **WebSocket**: RFC 6455
- **JSON**: RFC 7159
- **Ed25519**: RFC 8032

---

## Appendix: Code Examples

### Creating a Transaction

```cpp
// Create transaction data
Transaction tx;
tx.data.sender = "nodeA";
tx.data.receiver = "nodeB";
tx.data.amount = 10.5;
tx.data.unit = "kWh";
tx.data.price_per_unit = 5.0;
tx.data.currency = "USD";
tx.data.timestamp = timeNow();
tx.data.parents = {parent1_id, parent2_id};

// Add to Tangle (signs and performs PoW)
tx = tangle.addNewTransaction(tx);

// Broadcast to peers
network.broadcastTransaction(tx);
```

### Verifying a Transaction

```cpp
// Deserialize received transaction
Transaction rx = deserializeTransaction(jsonString);

// Verify sender's signature
bool sig1_valid = verifyTransaction(
    serializeTransactionData(rx),
    rx.metadata.signature1,
    rx.data.sender
);

// Verify receiver's signature (if present)
bool sig2_valid = verifyTransaction(
    serializeTransactionData(rx),
    rx.metadata.signature2,
    rx.data.receiver
);

// Verify PoW
bool pow_valid = (computeHash(rx) < target);

// All checks must pass
if (sig1_valid && sig2_valid && pow_valid) {
    tangle.addTransaction(rx, 1);  // Add and update if needed
}
```

### Updating Cumulative Weight

```cpp
// When fully-signed transaction is verified
std::string tx_id = tx.data.transaction_id;

// Update weight of this transaction
tangle.updateCumulativeWeight(tx_id, 1);

// This recursively updates all parents:
// tx.weight++
// Parent(tx).weight++
// Parent(Parent(tx)).weight++
// ... up to genesis
```

---

**Document Version**: 1.1  
**Last Updated**: 2026-04-05  
**Author**: Rishab Dugar
