# Lifecycle of a Transaction in the Microgrid Tangle Network

This document outlines the detailed, step-by-step lifecycle of a transaction in your DAG-based microgrid system, highlighting the roles of the sender, receiver, and other verifying nodes, along with the rationale for each step.

## Configuration

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `TX_COUNT` | 10 | Number of transactions to generate in simulation |
| `TX_DELAY` | 30 | Seconds between transaction generation |
| `WAIT_PERIOD` | 300 | Seconds to wait after last transaction for queue drain |
| `MONITOR_PERIOD` | 5 | Seconds between telemetry updates |
| `MAX_PEERS` | 5 | Maximum number of peer connections |
| `TOTAL_NODES` | 10 | Total nodes in network (used to derive BFT threshold) |
| `SIMULATION_TIMEOUT` | 3600 | Maximum simulation duration in seconds (1 hour) |
| `IDLE_TIMEOUT` | 600 | Exit if no activity for N seconds (10 minutes) |
| `AUTOSAVE_INTERVAL` | 300 | Auto-save tangle state every N seconds (5 minutes) |

### New Metrics (Added in Robustness Update)

The following metrics are now calculated and stored for each transaction:

| Metric | Description |
|--------|-------------|
| `consensusTimestamp` | Unix timestamp when consensus was reached |
| `consensusDuration` | Time (ms) from transaction creation to consensus |
| `propagationDelay` | Total propagation time: `last_hop - first_hop` (ms) |
| `avgPropagationDelay` | Average propagation time per hop (ms) |

**Note:** Consensus is reached when `votes >= ceil(2/3 * TOTAL_NODES)` (default: 10 nodes -> threshold 7)

---

## 1. Sender: Create & Broadcast a Proposal

### 1.1 Assemble Transaction Body

```json
{
  "sender_id": S,
  "receiver_id": R,
  "units": U,
  "tariff_rate": T,
  "timestamp": t1
}
```

**Why?** Defines the proposed energy exchange terms.

### 1.2 Sign with Sender's Key

* Compute `h = H(body)`
* Create `sig_sender = Sign_SKs(h)`
* Append `sig_sender` to the body

**Why?** Binds the sender to these exact transaction terms.

### 1.3 Select Parents

* **Parent A:** Sender's last approved transaction
* **Parent B:** A global tip selected via weighted algorithm

**Why?** Maintains per-user continuity and global DAG connectivity.

### 1.4 Broadcast Proposal (Tx1)

* Send: `{tx JSON with sig1, parents}`

**Why?** Announces the proposal to the network.

---

## 2. Any Node: Initial Verification & Gossip

### 2.1 Receive and Verify Tx1

* Validate `sig_sender`

**If valid:** Store and gossip
**If invalid:** Drop

**Why?** Ensures only well-formed proposals propagate.

---

## 3. Receiver: Detect & Inspect Proposal

### 3.1 Detect Proposal

* Monitor for transactions where `receiver_id == R`

**Why?** Identifies this node is the counterparty for this offer

### 3.2 Validate Proposal Content

* Confirm `units` and `tariff_rate`
* Verify `sig_sender`

**Why?** Prevents acceptance of incorrect or malicious data.

---

## 4. Receiver: Sign & Broadcast Approval

### 4.1 Sign Approval

* Compute `sig_receiver = Sign_SKr(H(body))`
* Add `sig_receiver` to tx.metadata.signature2
* tx.metadata.status transitions to APPROVED

**Why?** Prevents replay attacks and binds approval to this proposal.

### 4.2 Broadcast Lightweight Approval

* Send: `TX_APPROVAL` delta with `{tx_id, sig2, timestamp}`

**Why?** Confirms the transaction publicly.

---

## 5. Any Node: Final Verification & Tip Selection

### 5.1 Validate Tx2

* Verify `sig_sender` from Tx1
* Verify `sig_receiver` on `H(body)`

**If valid:** Mark Tx1 as APPROVED
**If fail:** Reject approval and do not propagate

### 5.2 Update Cumulative Weight

* Increment cumulative_weight for all ancestors (BFS)

**Why?** Builds network consensus toward approved branches.

### 5.3 Tip Selection

* Prefer tips with status >= APPROVED and reference_count == 0
* Deprioritize or prune unapproved tips

**Why?** Keeps the DAG clean and consensus-focused.

---

## 6. Final Confirmation & Robustness Features

### 6.1 Confirmation Threshold (Implemented)

Once `votes >= ceil(2/3 * TOTAL_NODES)`, the transaction reaches FINAL status:
- `consensusTimestamp` is set to current time
- `consensusDuration` is calculated as `consensusTimestamp - transaction_timestamp`
- `propagationDelay` and `avgPropagationDelay` are calculated from hop history

**Why?** Ensures finality and provides metrics for analysis.

### 6.2 Prune Old Confirmed Transactions

* Archive or remove from memory for performance

**Why?** Saves memory and improves efficiency

> **Note:** Pruning is not yet implemented and marked as future scope.

---

## 7. Robustness Features

### 7.1 Graceful Shutdown

On `SIGINT` (Ctrl+C) or `SIGTERM`:
1. Stop accepting new transactions
2. Save final tangle snapshot to `tangle_state.csv.final`
3. Upload final telemetry data
4. Close all connections cleanly

**Files Generated:**
- `tangle_state.csv` - Standard simulation output
- `tangle_state.csv.final` - Final snapshot on shutdown
- `tangle_state_autosave.csv` - Periodic auto-save during simulation

### 7.2 Retry Logic

Both snapshot saving and telemetry upload use exponential backoff retry:
- **Max retries:** 3 attempts
- **Backoff:** 1s, 2s, 4s between attempts
- **CSV retry:** 100ms * attempt number

### 7.3 Simulation Timeouts

| Timeout | Default | Behavior |
|---------|---------|----------|
| `SIMULATION_TIMEOUT` | 3600s | Hard stop if simulation exceeds limit |
| `IDLE_TIMEOUT` | 600s | Exit if no network activity detected |

### 7.4 Exception Handling

Critical network operations are wrapped in try-catch blocks:
- `handleIncomingMessage()` - Catches JSON parsing and processing errors
- `broadcastTxProposal()` - Catches serialization and broadcast errors
- Message handlers - Connection errors logged but don't crash node

### 7.5 Auto-Save

During simulation, the tangle state is periodically saved:
- Interval: `AUTOSAVE_INTERVAL` seconds (default: 300s = 5 minutes)
- File: `tangle_state_autosave.csv`
- Useful for recovery if simulation crashes

---

## Summary

* **Dual signatures (steps 1.2 & 4.1)** enforce both parties' explicit consent.
* **Two-phase messages (Tx₁ & Tx₂)** respect content-addressing immutability and give a full audit trail.
* **BFT vote-based finality** provides deterministic consensus once votes exceed the 2/3 threshold.
* **Consensus detection** automatically marks transactions as confirmed when cumulative weight reaches threshold.
* **Propagation metrics** track how quickly transactions spread through the network.
* **Graceful shutdown** ensures data is preserved even on interruption.
* **Retry logic** makes snapshot saving and telemetry upload reliable even on flaky networks.

This structured lifecycle ensures each energy transaction is fully auditable, mutually agreed upon, and cryptographically secure, while maintaining a fully decentralized architecture.
