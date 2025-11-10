#include "node.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <future>

Node::Node(const std::string& configFile) 
    : running_(false), currentTerm_(0), isInElection_(false) {
    
    config_ = std::make_unique<Config>(configFile);
    
    currentLeader_ = ""; 
    lastHeartbeatTime_ = std::chrono::steady_clock::now();
    
    std::cout << "Initialized node with config file: " << configFile << std::endl;
}

Node::~Node() {
    stop();
}

void Node::initialize() {
    std::string dataDir = config_->getNodeInfo().dataDir;
    
    mempool_ = std::make_shared<Mempool>();
    blockManager_ = std::make_shared<BlockManager>(dataDir);
    
    blockchainService_ = std::make_shared<BlockchainServiceImpl>(
        mempool_,
        blockManager_,
        getPeerAddresses()
    );
    
    blockchainService_->setNode(this);
    
    fileAuditService_ = std::make_unique<FileAuditServiceImpl>(
        mempool_,
        blockchainService_
    );
    
    std::cout << "Node initialization complete" << std::endl;
}

void Node::start() {
    if (running_) {
        std::cout << "Node is already running" << std::endl;
        return;
    }
    
    running_ = true;
    
    startServer();
    
    heartbeatThread_ = std::thread(&Node::runHeartbeatLoop, this);
    
    blockProposalThread_ = std::thread(&Node::runBlockProposalLoop, this);
    
    blockRecoveryThread_ = std::thread(&Node::runBlockRecoveryLoop, this);
    
    std::cout << "Node started" << std::endl;
}

void Node::wait() {
    if (server_) {
        std::cout << "Waiting for server to shutdown..." << std::endl;
        server_->Wait();
    }
}

void Node::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    if (server_) {
        std::cout << "Shutting down server..." << std::endl;
        server_->Shutdown();
    }
    
    if (heartbeatThread_.joinable()) {
        heartbeatThread_.join();
    }
    
    if (blockProposalThread_.joinable()) {
        blockProposalThread_.join();
    }
    
    if (blockRecoveryThread_.joinable()) {
        blockRecoveryThread_.join();
    }
    
    std::cout << "Node stopped" << std::endl;
}

std::string Node::getNodeId() const {
    return config_->getNodeInfo().id;
}

std::string Node::getNodeAddress() const {
    return config_->getNodeInfo().address;
}

void Node::startServer() {
    std::string serverAddress = config_->getNodeInfo().address;
    
    grpc::ServerBuilder builder;
    builder.AddListeningPort(serverAddress, grpc::InsecureServerCredentials());
    
    builder.RegisterService(blockchainService_.get());
    builder.RegisterService(fileAuditService_.get());
    
    server_ = builder.BuildAndStart();
    
    std::cout << "Server listening on " << serverAddress << std::endl;
}

void Node::handleHeartbeat(const blockchain::HeartbeatRequest& request) {
    if (!request.current_leader_address().empty()) {
        std::lock_guard<std::mutex> lock(leaderMutex_);
        
        if (currentLeader_.empty() || currentLeader_ != request.current_leader_address()) {
            std::cout << "Updating leader from heartbeat: " << request.current_leader_address() << std::endl;
            currentLeader_ = request.current_leader_address();
            lastHeartbeatTime_ = std::chrono::steady_clock::now();
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(peerHeartbeatsMutex_);
        peerHeartbeats_[request.from_address()] = {
            request.from_address(),
            request.latest_block_id(),
            request.mem_pool_size(),
            std::chrono::steady_clock::now()
        };
    }
}

void Node::runHeartbeatLoop() {
    std::cout << "Starting heartbeat loop" << std::endl;
    
    const int MAX_MISSED_HEARTBEATS = 3;
    
    const auto HEARTBEAT_INTERVAL = std::chrono::seconds(10);
    
    const auto INITIAL_DELAY = std::chrono::seconds(20);
    
    auto lastLeaderCheckTime = std::chrono::steady_clock::now();
    
    auto startupTime = std::chrono::steady_clock::now();
    
    while (running_) {
        try {
            std::string currentLeader;
            {
                std::lock_guard<std::mutex> lock(leaderMutex_);
                currentLeader = currentLeader_;
            }
            
            blockchain::HeartbeatRequest request;
            request.set_from_address(getNodeAddress());
            request.set_current_leader_address(currentLeader); 
            
            request.set_latest_block_id(blockManager_->getLatestBlockId());
            request.set_mem_pool_size(mempool_->size());
            
            auto& peerStubs = blockchainService_->getPeerStubs();
            for (size_t i = 0; i < peerStubs.size(); i++) {
                grpc::ClientContext context;
                blockchain::HeartbeatResponse response;
                
                std::string peerAddress = getPeerAddresses()[i];
                
                auto status = peerStubs[i]->SendHeartbeat(&context, request, &response);
                
                if (status.ok()) {
                    std::lock_guard<std::mutex> lock(peerHeartbeatsMutex_);
                    peerHeartbeats_[peerAddress] = {
                        peerAddress,
                        request.latest_block_id(),
                        request.mem_pool_size(),
                        std::chrono::steady_clock::now()
                    };
                } else {
                    std::cerr << "Failed to send heartbeat to peer " << i << " (" << peerAddress << "): " 
                              << status.error_message() << std::endl;
                }
            }
            
            auto now = std::chrono::steady_clock::now();
            
            if (!isLeader() && !isInElection_) {
                bool initialDelayPassed = (now - startupTime) > INITIAL_DELAY;
                
                if (currentLeader.empty() || 
                    (now - lastHeartbeatTime_) > (HEARTBEAT_INTERVAL * MAX_MISSED_HEARTBEATS)) {
                    
                    if (initialDelayPassed) {
                        if (currentLeader.empty()) {
                            std::cout << "No leader set, triggering election" << std::endl;
                            triggerElection();
                        } else {
                            bool leaderActive = false;
                            {
                                std::lock_guard<std::mutex> lock(peerHeartbeatsMutex_);
                                auto it = peerHeartbeats_.find(currentLeader);
                                if (it != peerHeartbeats_.end()) {
                                    auto lastHeartbeat = it->second.lastHeartbeatTime;
                                    if ((now - lastHeartbeat) <= (HEARTBEAT_INTERVAL * MAX_MISSED_HEARTBEATS)) {
                                        leaderActive = true;
                                    }
                                }
                            }
                            
                            if (!leaderActive) {
                                std::cout << "Leader " << currentLeader << " is missing, triggering election" << std::endl;
                                triggerElection();
                            }
                        }
                    } else {
                        std::cout << "Initial delay not passed yet, waiting for heartbeats before triggering election" << std::endl;
                    }
                    
                    lastLeaderCheckTime = now;
                }
            }
            
            if (isLeader()) {
                lastHeartbeatTime_ = now;
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception in heartbeat loop: " << e.what() << std::endl;
        }
        
        std::this_thread::sleep_for(HEARTBEAT_INTERVAL);
    }
    
    std::cout << "Heartbeat loop stopped" << std::endl;
}

void Node::runBlockProposalLoop() {
    std::cout << "Starting block proposal loop" << std::endl;
    
    while (running_) {
        try {
            if (isLeader()) {
                if (mempool_->size() >= 5) {  
                    std::cout << "We are the leader and have enough audits, proposing a new block" << std::endl;
                    blockchainService_->proposeNewBlock();
                } else {
                    std::cout << "We are the leader but don't have enough audits to propose a block (have " 
                              << mempool_->size() << ", need 5)" << std::endl;
                }
            } else {
                std::cout << "Not the leader, skipping block proposal" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception in block proposal loop: " << e.what() << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
    
    std::cout << "Block proposal loop stopped" << std::endl;
}

std::vector<std::string> Node::getPeerAddresses() {
    std::vector<std::string> peerAddresses;
    
    for (const auto& server : config_->getServers()) {
        if (server.address != config_->getNodeInfo().address) {
            peerAddresses.push_back(server.address);
        }
    }
    
    return peerAddresses;
}

void Node::triggerElection() {
    if (isInElection_) {
        std::cout << "Already in an election, skipping" << std::endl;
        return;
    }
    
    isInElection_ = true;
    
    currentTerm_++;
    
    std::cout << "Triggering election for term " << currentTerm_ << std::endl;
    
    blockchain::TriggerElectionRequest request;
    request.set_term(currentTerm_);
    request.set_address(getNodeAddress());
    
    auto& peerStubs = blockchainService_->getPeerStubs();
    int voteCount = 1;  
    int totalVotes = 1;
    
    std::vector<std::future<bool>> futures;
    
    for (size_t i = 0; i < peerStubs.size(); i++) {
      futures.push_back(std::async(std::launch::async, [this, &request, i, &totalVotes]() {
            grpc::ClientContext context;
            blockchain::TriggerElectionResponse response;
            
            auto status = blockchainService_->getPeerStubs()[i]->TriggerElection(&context, request, &response);
            
            if (status.ok()) {
                
                if (response.vote()) {
                    std::cout << "Received vote from peer " << i << std::endl;
                   totalVotes=totalVotes+1;
                    return true;
                } else {
                    std::cout << "Peer " << i << " voted no: " << response.error_message() << std::endl;
                    return false;
                }
            } else {
                std::cerr << "Failed to get vote from peer " << i << ": " 
                          << status.error_message() << std::endl;
                return false;
            }
        }));
    }
    
    int respondedPeers = 0;
    for (auto& future : futures) {
        try {
            if (future.get()) {
                voteCount++;
            }
            respondedPeers++;
        } catch (const std::exception& e) {
            std::cerr << "Exception while waiting for vote: " << e.what() << std::endl;
            // I am not counting this peer in the responded peers
        }
    }
    
    int effectiveTotalVotes = respondedPeers + 1;  //voting +1 for self
    
    // Check if we won the election - need majority of responding peers -  other classmates implemented 100% majority
    // If we got votes from majority of all responding peers + including self, we should win
    bool wonElection = voteCount >= (effectiveTotalVotes / 2) + 1;
    
    std::cout << "DEBUG: voteCount=" << voteCount << ", effectiveTotalVotes=" << effectiveTotalVotes 
              << ", threshold=" << (effectiveTotalVotes / 2) + 1 << std::endl;
    
    std::cout << "Election result: " << voteCount << " out of " << effectiveTotalVotes 
              << " votes, " << (wonElection ? "won" : "lost") << std::endl;
    
    if (wonElection) {
        becomeLeader();
    } else {
        isInElection_ = false;
    }
}

bool Node::voteInElection(const std::string& candidateAddress, int64_t term) {
    std::lock_guard<std::mutex> lock(leaderMutex_);
    
    
    int64_t candidateLatestBlockId = 0; 
    int64_t candidateMempoolSize = 0;
    
    {
        std::lock_guard<std::mutex> lock(peerHeartbeatsMutex_);
        auto it = peerHeartbeats_.find(candidateAddress);
        if (it != peerHeartbeats_.end()) {
            candidateLatestBlockId = it->second.latestBlockId;
            candidateMempoolSize = it->second.mempoolSize;
        } else {
            std::cout << "No heartbeat information found for candidate " << candidateAddress << std::endl;
        }
    }
    
    int64_t ourLatestBlockId = blockManager_->getLatestBlockId();
    int64_t ourMempoolSize = mempool_->size();
    
    bool vote = false;
    
    if (candidateLatestBlockId > ourLatestBlockId) {
        vote = true;
    } 
    else if (candidateLatestBlockId == ourLatestBlockId) {
        if (candidateMempoolSize > ourMempoolSize) {
            vote = true;
        }
        else if (candidateMempoolSize == ourMempoolSize) {
            if (candidateAddress > getNodeAddress()) {
                vote = true;
            }
        }
    }
    
    std::cout << "Voting " << (vote ? "yes" : "no") << " for " << candidateAddress 
              << " in term " << term << std::endl;
    
    return vote;
}

void Node::becomeLeader() {
    std::lock_guard<std::mutex> lock(leaderMutex_);
    
    std::cout << "Becoming leader for term " << currentTerm_ << std::endl;
    
    currentLeader_ = getNodeAddress();
    
    blockchain::NotifyLeadershipRequest request;
    request.set_address(getNodeAddress());
    
    auto& peerStubs = blockchainService_->getPeerStubs();
    
    std::vector<std::future<bool>> futures;
    
    for (size_t i = 0; i < peerStubs.size(); i++) {
        futures.push_back(std::async(std::launch::async, [this, &request, i]() {
            grpc::ClientContext context;
            blockchain::NotifyLeadershipResponse response;
            
            auto status = blockchainService_->getPeerStubs()[i]->NotifyLeadership(&context, request, &response);
            
            if (status.ok() && response.status() == "success") {
                std::cout << "Successfully notified peer " << i << " of leadership" << std::endl;
                return true;
            } else {
                std::cerr << "Failed to notify peer " << i << " of leadership: " 
                          << (status.ok() ? response.error_message() : status.error_message()) << std::endl;
                return false;
            }
        }));
    }
    
    int successCount = 0;
    for (auto& future : futures) {
        if (future.get()) {
            successCount++;
        }
    }
    
    std::cout << "Successfully notified " << successCount << " out of " << peerStubs.size() 
              << " peers of leadership" << std::endl;
    
    isInElection_ = false;
}

void Node::updateLeader(const std::string& leaderAddress) {
    std::lock_guard<std::mutex> lock(leaderMutex_);
    
    std::cout << "Updating leader to " << leaderAddress << std::endl;
    
    currentLeader_ = leaderAddress;
    
    isInElection_ = false;
    
    lastHeartbeatTime_ = std::chrono::steady_clock::now();
}

void Node::runBlockRecoveryLoop() {
    std::cout << "Starting block recovery loop" << std::endl;
    
    const auto RECOVERY_INTERVAL = std::chrono::seconds(30);
    
    while (running_) {
        try {
            int64_t ourLatestBlockId = blockManager_->getLatestBlockId();
            
            int64_t highestBlockId = ourLatestBlockId;
            std::string peerWithHighestBlock;
            
            {
                std::lock_guard<std::mutex> lock(peerHeartbeatsMutex_);
                for (const auto& [peerAddress, peerInfo] : peerHeartbeats_) {
                    if (peerInfo.latestBlockId > highestBlockId) {
                        highestBlockId = peerInfo.latestBlockId;
                        peerWithHighestBlock = peerAddress;
                    }
                }
            }
            
            if (highestBlockId > ourLatestBlockId) {
                std::cout << "Block recovery: We are behind (our latest: " << ourLatestBlockId 
                          << ", highest: " << highestBlockId << " from " << peerWithHighestBlock << ")" << std::endl;
                
                if (!peerWithHighestBlock.empty()) {
                    bool success = syncMissingBlocks(highestBlockId, peerWithHighestBlock);
                    if (success) {
                        std::cout << "Block recovery: Successfully synced blocks up to " << highestBlockId << std::endl;
                    } else {
                        std::cerr << "Block recovery: Failed to sync blocks" << std::endl;
                    }
                }
            } else {
                std::cout << "Block recovery: We are up to date (latest block: " << ourLatestBlockId << ")" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception in block recovery loop: " << e.what() << std::endl;
        }
        
        std::this_thread::sleep_for(RECOVERY_INTERVAL);
    }
    
    std::cout << "Block recovery loop stopped" << std::endl;
}

bool Node::syncMissingBlocks(int64_t targetBlockId, const std::string& peerAddress) {
    auto& peerStubs = blockchainService_->getPeerStubs();
    auto peerAddresses = getPeerAddresses();
    
    int peerIndex = -1;
    for (size_t i = 0; i < peerAddresses.size(); i++) {
        if (peerAddresses[i] == peerAddress) {
            peerIndex = i;
            break;
        }
    }
    
    if (peerIndex == -1) {
        std::cerr << "Block recovery: Peer address not found: " << peerAddress << std::endl;
        return false;
    }
    
    int64_t ourLatestBlockId = blockManager_->getLatestBlockId();
    
    bool success = true;
    for (int64_t blockId = ourLatestBlockId + 1; blockId <= targetBlockId; blockId++) {
        std::cout << "Block recovery: Fetching block " << blockId << " from " << peerAddress << std::endl;
        
        blockchain::GetBlockRequest request;
        request.set_id(blockId);
        
        grpc::ClientContext context;
        blockchain::GetBlockResponse response;
        
        auto status = peerStubs[peerIndex]->GetBlock(&context, request, &response);
        
        if (status.ok() && response.status() == "success") {
            if (blockManager_->verifyBlock(response.block())) {
                if (blockManager_->commitBlock(response.block())) {
                    std::cout << "Block recovery: Successfully committed block " << blockId << std::endl;
                    
                    std::vector<common::FileAudit> audits;
                    for (int i = 0; i < response.block().audits_size(); i++) {
                        audits.push_back(response.block().audits(i));
                    }
                    mempool_->removeAudits(audits);
                } else {
                    std::cerr << "Block recovery: Failed to commit block " << blockId << std::endl;
                    success = false;
                    break;
                }
            } else {
                std::cerr << "Block recovery: Block verification failed for block " << blockId << std::endl;
                success = false;
                break;
            }
        } else {
            std::cerr << "Block recovery: Failed to get block " << blockId << " from " << peerAddress << ": " 
                      << (status.ok() ? response.error_message() : status.error_message()) << std::endl;
            success = false;
            break;
        }
    }
    
    return success;
}
