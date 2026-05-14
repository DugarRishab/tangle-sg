# Tangle-SG Deferred Work

Items that are out of scope for the current sprint but must be completed later.

## v2.0 Core — Already Done

- [x] Remove PoW entirely
- [x] `TransactionStatus` enum (`PROPOSED`, `APPROVED`, `FINAL`)
- [x] Children index + `reference_count`
- [x] BFT voting layer (`addVote`, `finalizeTransaction`)
- [x] Iterative `updateDescendantWeight` (BFS queue)
- [x] Orphan TTL + pool size cap
- [x] Per-peer rate limiting (token bucket + reputation)
- [x] Dynamic gossip fanout `ceil(ln(N))`
- [x] MCMC tip-selection (random walk toward FINAL tips)
- [x] FINAL-only parent eligibility for TX_PROPOSAL

## Deferred Core Work

### 1. Delta Sync Protocol (`SYNC_BATCH`)

- [ ] Define `SYNC_BATCH` message format (list of changed tx fields since a checkpoint)
- [ ] Implement `handleSyncBatch()` in `network.cpp`
- [ ] Add checkpoint tracking (last synced tx_id or timestamp per peer)
- [ ] This replaces the full-Tangle `SYNC_ACK` for catch-up scenarios

### 2. Worker Entrypoint — Repo Branch Parameterization

- [ ] `docknet/worker/entrypoint.sh` hardcodes `REPO_BRANCH=monitor`
- [ ] `workerController.js` and `simulationController.js` pass `REPO_BRANCH` as env var
- [ ] Dockerfile accepts `REPO_BRANCH` as build arg

### 3. Telemetry Schema v2.0

- [ ] `sendTelemetry()` in `main.cpp` only sends `nodeId`, `tangle`, `peers`, `metrics`, `runId`
- [ ] Needs to include:
  - Consensus metrics (avg consensus duration, FINAL tx count)
  - Vote distribution (votes per tx, voter set)
  - Orphan stats (orphans created, resolved, expired)
  - Rate-limiting stats (rejected messages per peer)
  - TSA metrics (walk depth, convergence rate)
- [ ] Central's `POST /api/telemetry` handler must accept new fields
- [ ] SQLite schema needs new columns or JSON extension

### 4. Central API Cleanup — Remove `pow` Param

- [ ] Remove `pow` from `req.query` validation in `workerController.js` and `simulationController.js`
- [ ] Remove `pow` from `insertRunParams` and DB schema
- [ ] Update `ARCHITECTURE.md` API reference
- [ ] Update dashboard to not send `pow` in start-simulation requests

### 5. Dashboard UI Update for v2.0

- [ ] Replace PoW time display with consensus duration
- [ ] Add transaction status badges (`PROPOSED`, `APPROVED`, `FINAL`)
- [ ] Show vote count and `voted_by` set in tx detail view
- [ ] Display orphan pool size / TTL in node health panel
- [ ] Show per-peer rate-limit status

### 6. Documentation Overhaul

- [ ] `DAG_DOCUMENTATION.md` — update all code snippets to v2.0 APIs
- [ ] `ARCHITECTURE.md` — remove PoW references, update telemetry schema, update TSA description
- [ ] `V2_ROADMAP.md` — mark completed phases, move SYNC_BATCH to current plan

### 7. Dynamic Gossip Fanout Edge Cases

- [ ] `ceil(ln(N))` with `N < 3` yields `1`, but code clamps to `3` via `std::max(3, ...)`
- [ ] Should fanout scale down for very small networks (< 5 nodes)?
- [ ] Document behavior or make `min_fanout` configurable

### 8. HMAC Secret Rotation

- [ ] `HMAC_SECRET` is static per Docker image (set in `worker/Dockerfile`)
- [ ] Should be injected at runtime via central orchestrator or env
- [ ] Rotation policy: nodes with old secret should gracefully disconnect

## Known Bugs / Hardening

- [ ] `processOrphans()` re-checks `allFinal` but does not re-verify signatures on un-orphaning
- [ ] `getChildren()` currently takes `shared_lock` on each call; MCMC TSA calls it repeatedly per walk — consider batch-fetch or caching
- [ ] `selectTips()` starts from random FINAL transactions; if graph is deep, multiple walks may converge to same tip — add walk-diversity heuristic
- [ ] `main.cpp` `SIMULATION_TIMEOUT` does not account for consensus finality delays (BFT voting can take longer than tx generation)
- [ ] Central's `startWorkers` does not wait for worker containers to report healthy before returning 200