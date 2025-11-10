#pragma once

#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "block_chain.grpc.pb.h"
#include "mempool.h"
#include "block_manager.h"

class Node;

class BlockchainServiceImpl final : public blockchain::BlockChainService::Service {
public:
    BlockchainServiceImpl(
        std::shared_ptr<Mempool> mempool,
        std::shared_ptr<BlockManager> blockManager,
        const std::vector<std::string>& peerAddresses
    );
    
    void setNode(Node* node) { node_ = node; }
    
    grpc::Status WhisperAuditRequest(
        grpc::ServerContext* context,
        const common::FileAudit* request,
        blockchain::WhisperResponse* response
    ) override;
    
    grpc::Status ProposeBlock(
        grpc::ServerContext* context,
        const blockchain::Block* request,
        blockchain::BlockVoteResponse* response
    ) override;
    
    grpc::Status CommitBlock(
        grpc::ServerContext* context,
        const blockchain::Block* request,
        blockchain::BlockCommitResponse* response
    ) override;
    
    grpc::Status GetBlock(
        grpc::ServerContext* context,
        const blockchain::GetBlockRequest* request,
        blockchain::GetBlockResponse* response
    ) override;
    
    grpc::Status SendHeartbeat(
        grpc::ServerContext* context,
        const blockchain::HeartbeatRequest* request,
        blockchain::HeartbeatResponse* response
    ) override;
    
    grpc::Status TriggerElection(
        grpc::ServerContext* context,
        const blockchain::TriggerElectionRequest* request,
        blockchain::TriggerElectionResponse* response
    ) override;
    
    grpc::Status NotifyLeadership(
        grpc::ServerContext* context,
        const blockchain::NotifyLeadershipRequest* request,
        blockchain::NotifyLeadershipResponse* response
    ) override;
    
    void proposeNewBlock();
    void broadcastBlock(const blockchain::Block& block);
    bool collectVotes(const blockchain::Block& block);
    bool whisperToAllPeers(const common::FileAudit& audit);
    
    std::vector<std::unique_ptr<blockchain::BlockChainService::Stub>>& getPeerStubs() { return peerStubs_; }
    
private:
    std::shared_ptr<Mempool> mempool_;
    std::shared_ptr<BlockManager> blockManager_;
    std::vector<std::string> peerAddresses_;
    std::vector<std::unique_ptr<blockchain::BlockChainService::Stub>> peerStubs_;
    Node* node_ = nullptr;  
    
    void initializePeerStubs();
};
