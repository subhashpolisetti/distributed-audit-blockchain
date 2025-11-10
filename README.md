# Distributed Audit Blockchain

A **5-node fault-tolerant distributed system** for tamper-evident file-access audit logging, built in C++17. Nodes communicate over gRPC, elect a leader via a Raft-inspired protocol, and reach consensus on committed blocks through a majority-quorum propose-vote-commit pipeline.

---

## Architecture

```
                        ┌─────────────────────────────────────┐
                        │           gRPC Cluster (5 nodes)    │
                        │                                     │
  Python Client         │   ┌────────┐     ┌────────┐        │
  audit_client.py  ───► │   │ Node 1 │◄───►│ Node 2 │        │
  (RSA-signed audit)    │   │(leader)│     │        │        │
                        │   └────┬───┘     └────────┘        │
                        │        │  propose / vote / commit   │
                        │   ┌────▼───┐     ┌────────┐        │
                        │   │ Node 3 │◄───►│ Node 4 │        │
                        │   └────────┘     └────────┘        │
                        │           ┌────────┐               │
                        │           │ Node 5 │               │
                        │           └────────┘               │
                        └─────────────────────────────────────┘
```

### Core Components

| Component | File | Responsibility |
|---|---|---|
| Node | `src/node.cc` | Leader election, heartbeat, block recovery, block proposal |
| BlockManager | `src/block_manager.cc` | Create/verify/commit blocks, Merkle root, disk persistence |
| BlockchainService | `src/blockchain_service.cc` | gRPC — whisper, propose, vote, commit, get block |
| FileAuditService | `src/file_audit_service.cc` | gRPC — submit audit, validate, verify RSA signature |
| Mempool | `src/mempool.cc` | Thread-safe pending audit store with content-hash dedup |
| CryptoUtils | `src/crypto_utils.cc` | SHA-256, Merkle tree, RSA-SHA256 verify (OpenSSL) |
| Python Client | `client/audit_client.py` | Generate RSA keys, sign audits, submit over gRPC |

---

## How It Works

### 1. Leader Election (Raft-inspired)
- Nodes send **heartbeats every 10s** carrying term, leader address, and latest block ID.
- Missing **3 consecutive heartbeats** triggers a term election.
- Candidate fans out `TriggerElection` RPCs via `std::async`/futures concurrently.
- Vote criteria: higher `latest_block_id` → larger mempool → larger address.
- Winner broadcasts `NotifyLeadership` to all peers.

### 2. Audit Submission and Gossip
```
Client signs audit (RSA-SHA256)
  → SubmitAudit RPC → validate fields + verify signature
  → add to mempool
  → WhisperAuditRequest to all peers (gossip propagation)
```

### 3. Consensus — Propose → Vote → Commit
```
Leader (mempool ≥ 5 audits):
  createBlock  → Merkle root + SHA-256 hash chain
  ProposeBlock → async fan-out to all peers, majority vote required
  commitBlock  → verify id sequence, prev hash, Merkle root, block hash
  CommitBlock  → broadcast to all peers
  remove committed audits from mempool
```

### 4. Block Integrity (Tamper-Evident by Construction)
```
blockHash    = SHA-256( id : previous_hash : merkle_root )
merkle_root  = pairwise SHA-256 over all audit hashes (odd node duplicated)
```
Altering any audit changes its hash → changes the Merkle root → changes the block hash → breaks the chain.

### 5. Automatic Block Recovery
Background loop (every 30s):
- Compares own `latest_block_id` against peers' (from heartbeat metadata).
- If behind, pulls missing blocks via `GetBlock` RPC from the most-advanced peer.
- Verifies and commits each block, removes audits from mempool.

---

## gRPC Services

```protobuf
service FileAuditService {
  rpc SubmitAudit          (FileAudit)              returns (FileAuditResponse);
}

service BlockChainService {
  rpc WhisperAuditRequest  (FileAudit)              returns (WhisperResponse);
  rpc ProposeBlock         (Block)                  returns (BlockVoteResponse);
  rpc CommitBlock          (Block)                  returns (BlockCommitResponse);
  rpc GetBlock             (GetBlockRequest)         returns (GetBlockResponse);
  rpc SendHeartbeat        (HeartbeatRequest)        returns (HeartbeatResponse);
  rpc TriggerElection      (TriggerElectionRequest)  returns (TriggerElectionResponse);
  rpc NotifyLeadership     (NotifyLeadershipRequest) returns (NotifyLeadershipResponse);
}
```

---

## Build and Run

### Prerequisites
```
CMake 3.14+  |  C++17 compiler  |  gRPC + Protocol Buffers
OpenSSL      |  yaml-cpp        |  nlohmann/json  |  Abseil
```

### Build
```bash
./build.sh
```

### Start 5-node cluster
```bash
./start_nodes.sh
# nodes start on ports 50051–50055
# logs: node1.log … node5.log
```

### Submit audits
```bash
cd client
pip install grpcio grpcio-tools cryptography
python generate_proto.py
python audit_client.py \
  --server localhost:50051 \
  --file-id "file123" --file-name "report.pdf" \
  --user-id "user456" --user-name "John Doe" \
  --access-type 1 --generate-keys
```

### Watch consensus in real time
```bash
tail -f node1.log | grep -E "CONSENSUS|COMMIT|VERIFY|ELECT"
```

### Stop cluster
```bash
./stop_nodes.sh
```

---

## Concurrency Design

| Mechanism | Where | Why |
|---|---|---|
| `std::lock_guard` (RAII) | All shared state | Exception-safe mutex handling |
| `verifyBlockNoLock` | Commit path | Avoids re-entrant deadlock during commit |
| `std::async` + futures | Vote/commit fan-out | Non-blocking parallel peer calls |
| Separate `std::thread` per concern | Heartbeat, proposal, recovery | Clean separation of lifecycle loops |

---

## Tech Stack

| | |
|---|---|
| Language | C++17 |
| RPC | gRPC |
| Serialization | Protocol Buffers |
| Cryptography | OpenSSL (SHA-256, RSA-SHA256) |
| Build | CMake 3.14+ |
| Config | yaml-cpp, nlohmann/json |
| Client | Python 3, grpcio, cryptography |

---

## Design Decisions and Trade-offs

| Decision | Why | Known Limitation |
|---|---|---|
| Raft-inspired (not full Raft) | Simpler; sufficient for append-only audit log | No term/vote persistence across restarts |
| Majority quorum | Tolerates up to (n-1)/2 failures | Requires > half nodes live to commit |
| Gossip ("whisper") propagation | Decentralized audit ingestion | Eventual consistency in mempool |
| Hash chain + Merkle | O(log n) audit proof, tamper-evident | Merkle proof not exposed via API |
