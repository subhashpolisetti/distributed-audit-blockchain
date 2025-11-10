#pragma once

#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <grpcpp/grpcpp.h>
#include "config.h"
#include "mempool.h"
#include "block_manager.h"
#include "blockchain_service.h"
#include "file_audit_service.h"

class Node {
public:
    Node(const std::string& configFile);
    ~Node();
    
    void initialize();
    
    void start();
    
    void wait();
    
    void stop();
    
    std::string getNodeId() const;
    
    std::string getNodeAddress() const;
    
    Config* getConfig() { return config_.get(); }
    
    void triggerElection();
    bool voteInElection(const std::string& candidateAddress, int64_t term);
    void becomeLeader();
    void updateLeader(const std::string& leaderAddress);
    void handleHeartbeat(const blockchain::HeartbeatRequest& request);
    
    std::string getCurrentLeader() const { return currentLeader_; }
    int64_t getCurrentTerm() const { return currentTerm_; }
    bool isLeader() const { return currentLeader_ == getNodeAddress(); }
    
private:
    std::unique_ptr<Config> config_;
    std::shared_ptr<Mempool> mempool_;
    std::shared_ptr<BlockManager> blockManager_;
    std::shared_ptr<BlockchainServiceImpl> blockchainService_;
    std::unique_ptr<FileAuditServiceImpl> fileAuditService_;
    
    std::unique_ptr<grpc::Server> server_;
    std::atomic<bool> running_;
    std::thread heartbeatThread_;
    std::thread blockProposalThread_;
    std::thread blockRecoveryThread_;
    std::thread leaderElectionThread_;
    
    std::string currentLeader_;
    int64_t currentTerm_;
    std::atomic<bool> isInElection_;
    std::chrono::steady_clock::time_point lastHeartbeatTime_;
    std::mutex leaderMutex_;
    
    struct PeerInfo {
        std::string address;
        int64_t latestBlockId;
        int64_t mempoolSize;
        std::chrono::steady_clock::time_point lastHeartbeatTime;
    };
    std::unordered_map<std::string, PeerInfo> peerHeartbeats_;
    std::mutex peerHeartbeatsMutex_;
    
    void startServer();
    void runHeartbeatLoop();
    void runBlockProposalLoop();
    void runBlockRecoveryLoop();
    std::vector<std::string> getPeerAddresses();
    bool syncMissingBlocks(int64_t targetBlockId, const std::string& peerAddress);
};
