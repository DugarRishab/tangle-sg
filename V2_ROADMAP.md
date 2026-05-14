# Tangle-SG Architecture & Implementation Roadmap

## Section 1: Design Decisions (with reasoning)

### 1.1 Two-Phase Transaction Model
- **What:** Sender creates Tx1 (sig1), receiver adds sig2 in-place, rebroadcasts via `TX_APPROVAL` delta.
- **Why not separate Tx2?** Original docs suggested separate approval node, but code correctly uses in-place update. Separate Tx2 adds orphan risk, doubles storage, and doesn't improve semantics.
- **Future consideration:** If we ever want separate approval DAG nodes, we'd need to redesign the entire graph validation logic.

### 1.2 DAG retains central role
- **What:** DAG structure provides causality, parallelism, and organic confidence accumulation.
- **Why not pure BFT log?** That would discard the core innovation — graph-based reputation and MCMC tip selection. BFT is layered on top, not replacing the DAG.
- **BFT vs DAG relationship:** BFT provides deterministic finality. DAG provides weighted graph topology and reputation. They stack.

### 1.3 Permissioned with HMAC peering
- **What:** All nodes share `HMAC_SECRET`. UDP broadcast handshake with nonces.
- **Why not open enrollment?** Microgrid meters are known entities. Permissioned bounds the threat model to Byzantine insiders, not arbitrary outsiders.
- **Limitation:** `HMAC_SECRET` compromise = total impersonation. Rotate periodically in production.

### 1.4 Gossip as transport
- **What:** WebSocket-based epidemic gossip with per-message random peer selection. Fanout dynamically set to `ceil(ln(N))` where `N = TOTAL_NODES`.
- **Why:** Physical microgrid nodes don't have full connectivity. Gossip achieves O(log N) propagation diameter. Random per-message selection provides resilience against churn and Byzantine peers; a fixed neighbor list would create permanent blind spots on peer failure.
- **Theoretical basis:** Per epidemic/gossip literature, `ln(N)` is the minimum fanout required for reliable dissemination with high probability (Leitao, n.d.; Kermarrec & van Steen, 2007). For N=100, fanout=5 yields ~99.9% coverage in 3 rounds.
- **Critical rule:** Validate every message cryptographically before forwarding. Forwarding unvalidated messages = giving attackers the key.

### 1.5 Static `TOTAL_NODES` for threshold
- **What:** `TOTAL_NODES` set via env at startup. Threshold = `ceil(2/3 * TOTAL_NODES)`.
- **Why not dynamic discovery?** Auto-incrementing N on new peer arrival is a Sybil vulnerability. A single attacker joining with 5 fake identities dilutes the threshold.
- **Future work:** Dynamic membership requires supermajority-certified view changes (PBFT-style). Not implemented in v1.

### 1.6 `cumulative_weight` = recursive reference count
- **What:** Number of direct + indirect descendant transactions (children + children-of-children).
- **Why:** Matches IOTA semantics. Gives MCMC a meaningful score to walk toward. Separated from `votes` which is consensus state.
- **Old confusion resolved:** Old code used `cumulative_weight` for "how many nodes have seen this tx." That was a replication counter, not a graph metric.

### 1.7 `votes` + `voted_by` for BFT finality
- **What:** After `TX_APPROVAL`, each node signs and gossips a `VOTE` message. When `votes > threshold`, tx transitions to `FINAL`.
- **Why:** Deterministic finality replaces probabilistic cumulative weight consensus.
- **Vote authentication:** Every vote signed with Ed25519 (`sign(tx_id + voter_id)`). Verified at every gossip hop.

### 1.8 MCMC Tip Selection
- **What:** Weighted random walk from random starting points, transition probability ∝ `cumulative_weight` of the selected tip.
- **Why:** Prevents mesh-like DAG. Converges on high-reputation transactions. Natural resistance to lazy tip selection.
- **Why not pure random?** Current pure-random TSA produces dense mesh graphs where every tx references every other tx. MCMC makes the DAG sparse and tree-like.

### 1.9 TSA parent eligibility = `FINAL` status only
- **What:** Only transactions with `status == FINAL` can be selected as parents during TSA.
- **Why:** Enforces bilateral consent at the graph level. A sender cannot build reputation on an unapproved transaction. This is the "gate" for the entire DAG.

### 1.10 No weight update messages
- **What:** Weight is derived locally from parent references. When a new tx arrives, nodes increment parent weights iteratively.
- **Why:** Eliminates ~50% of network traffic. Every node can independently compute the same weight from the DAG structure.

### 1.11 PoW removed entirely
- **What:** SHA256 brute-force nonce search removed.
- **Why:** At difficulty 3, it takes <1ms on any CPU. Provides zero spam resistance in permissioned setting. Wastes energy in a system literally designed for energy efficiency.
- **Spam defense replaced by:** Per-peer rate limiting + reputation-based throttling.

### 1.12 Per-peer rate limiting
- **What:** Token-bucket or quota-based limiting per peer identity. Rate = `base_rate * reputation_score`.
- **Why:** Permissioned networks know peer identities. Reputation score derived from valid tx participation ratio. Bad actors get throttled, not blocked.
- **Future work:** Adaptive reputation decay and recovery for transient faults.

### 1.13 Lightweight message protocol
- **Old:** Every update sends full transaction JSON (heavy, redundant).
- **New:**
  - `TX_PROPOSAL` — full tx JSON (sender initial broadcast)
  - `TX_APPROVAL` — `{tx_id, sig2, receiver_id}` delta only
  - `VOTE` — `{tx_id, voter_id, signature}` small message
  - `SYNC_BATCH` — delta sync with only changed fields
- **Why:** ~80% bandwidth reduction for approvals and votes.

---

## Section 2: Current Implementation Plan (v2.0)

### Phase 1: Remove PoW
- Delete [pow.cpp](cci:7://file:///d:/coding/dev/tangleProj/tangle-sg/src/modules/pow.cpp:0:0-0:0), [pow.h](cci:7://file:///d:/coding/dev/tangleProj/tangle-sg/src/headers/pow.h:0:0-0:0)
- Remove [performPoW()](cci:1://file:///d:/coding/dev/tangleProj/tangle-sg/src/headers/pow.h:4:0-4:48) calls from [network.cpp](cci:7://file:///d:/coding/dev/tangleProj/tangle-sg/src/modules/network.cpp:0:0-0:0)
- Remove PoW duration fields from metadata

### Phase 2: Tangle data structure update
- Add `children` index (`unordered_map<string, unordered_set<string>>`) to [Tangle](cci:2://file:///d:/coding/dev/tangleProj/tangle-sg/src/headers/tangle.h:10:0-43:1)
- Add `TransactionStatus` enum: `PROPOSED`, `APPROVED`, `FINAL`
- Add `votes` (int) and `voted_by` (`unordered_set<string>`) to metadata
- Add `reference_count` (direct children count, separate from recursive weight)

### Phase 3: Message protocol refactoring
- Define new message types: `TX_PROPOSAL`, `TX_APPROVAL`, `VOTE`, `SYNC_BATCH`
- Implement delta serialization for `TX_APPROVAL` and `VOTE`
- Add signature verification to every gossip hop

### Phase 4: Rewrite TSA with MCMC
- Implement `findTips()` — identify all transactions with empty children set + `FINAL` status
- Implement MCMC weighted random walk:
  - Start from multiple random verified transactions
  - Walk toward tips, transition probability ∝ `cumulative_weight`
  - Run until `numTips` distinct tips found
- Enforce parent eligibility: only `FINAL` txs

### Phase 5: BFT voting layer
- `handleTxApproval()` — verify sig2, auto-add sender+receiver votes, broadcast `VOTE`
- `handleVote()` — verify vote signature, add to `voted_by`, check threshold
- `finalizeTransaction()` — transition to `FINAL`, trigger weight update

### Phase 6: Iterative weight update (replace recursion)
- Convert [updateCumulativeWeightOfParents()](cci:1://file:///d:/coding/dev/tangleProj/tangle-sg/src/modules/tangle.cpp:113:0-130:1) from recursive to BFS queue
- Fine-grained locking: lock per-transaction or batch updates

### Phase 7: Orphan TTL + pool limit
- Add `timestamp` to orphan entries
- Expire orphans after configurable TTL (e.g., 30s)
- Cap total orphan pool size (e.g., 1000 entries)

### Phase 8: Per-peer rate limiting
- Add `PeerQuota` struct with token bucket
- Reputation score = `valid_txs / (valid_txs + invalid_txs)` over sliding window
- Refill rate = `base_rate * reputation / peer_count`

---

## Section 3: Future Work (beyond v2.0)

### 3.1 Dynamic membership / view changes
- Supermajority-certified membership proposals
- New node joins only after `> 2/3` existing nodes sign the view change
- Required for production microgrids with churn

### 3.2 DAG pruning / snapshotting
- Remove `FINAL` transactions older than retention window
- Keep only recent graph and state summaries
- Reduces memory for long-running nodes

### 3.3 BFT vote aggregation
- Collect votes into compact aggregate signatures (BLS or threshold signatures)
- Reduces vote message count from O(N²) to O(N)
- Required for scaling beyond ~200 nodes

### 3.4 Adaptive MCMC parameters
- Tune random-walk depth and starting count based on graph density
- Monitor convergence metrics

### 3.5 Formal safety/liveness proofs
- Model the BFT+DAG hybrid in TLA+ or Coq
- Prove safety (no two conflicting txs can both be FINAL)
- Prove liveness (all honest txs eventually become FINAL)

### 3.6 Network partition detection
- Heartbeat-based partition detection
- Automatic recovery when partition heals

---

## Section 4: Key Terms & Concepts

| Term | Definition | Where used |
|---|---|---|
| **Tip** | Transaction with no children (not referenced as parent by any tx) | TSA, graph traversal |
| **Cumulative weight** | Recursive count of all descendant transactions | MCMC TSA, reputation |
| **Reference count** | Direct children count only | Lightweight metric |
| **FINAL** | Status: tx has `votes > threshold` | TSA parent eligibility, consensus |
| **APPROVED** | Status: tx has sig2 but not enough votes | Intermediate state |
| **PROPOSED** | Status: tx has sig1 only | Pending approval |
| **Threshold** | `ceil(2/3 * TOTAL_NODES)` | Finality condition |
| **Gossip** | Epidemic message propagation with validation | Network layer |
| **MCMC** | Markov Chain Monte Carlo weighted random walk | Tip selection |

---

## Section 5: Files to modify

| File | Changes |
|---|---|
| [src/headers/tangle.h](cci:7://file:///d:/coding/dev/tangleProj/tangle-sg/src/headers/tangle.h:0:0-0:0) | Add `children`, `TransactionStatus`, `votes`, `voted_by`, `reference_count` |
| [src/headers/transaction.h](cci:7://file:///d:/coding/dev/tangleProj/tangle-sg/src/headers/transaction.h:0:0-0:0) | Add status enum, vote-related fields |
| [src/modules/tangle.cpp](cci:7://file:///d:/coding/dev/tangleProj/tangle-sg/src/modules/tangle.cpp:0:0-0:0) | Add children tracking, iterative weight update, finalize logic |
| [src/modules/tsa.cpp](cci:7://file:///d:/coding/dev/tangleProj/tangle-sg/src/modules/tsa.cpp:0:0-0:0) | Rewrite with MCMC, FINAL eligibility check |
| [src/modules/network.cpp](cci:7://file:///d:/coding/dev/tangleProj/tangle-sg/src/modules/network.cpp:0:0-0:0) | Add `TX_APPROVAL`, `VOTE` handlers, remove PoW calls, validation before gossip |
| [src/modules/transaction.cpp](cci:7://file:///d:/coding/dev/tangleProj/tangle-sg/src/modules/transaction.cpp:0:0-0:0) | Add vote serialization, status helpers |
| [src/modules/pow.cpp](cci:7://file:///d:/coding/dev/tangleProj/tangle-sg/src/modules/pow.cpp:0:0-0:0) | Delete |
| [src/headers/pow.h](cci:7://file:///d:/coding/dev/tangleProj/tangle-sg/src/headers/pow.h:0:0-0:0) | Delete |
| [DAG_DOCUMENTATION.md](cci:7://file:///d:/coding/dev/tangleProj/tangle-sg/DAG_DOCUMENTATION.md:0:0-0:0) | Update to match v2.0 architecture |

---
