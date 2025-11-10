#include "blockchain_service.h"
#include "node.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <future>

BlockchainServiceImpl::BlockchainServiceImpl(
    std::shared_ptr<Mempool> mempool,
    std::shared_ptr<BlockManager> blockManager,
    const std::vector<std::string>& peerAddresses
) : mempool_(mempool), blockManager_(blockManager), peerAddresses_(peerAddresses) {
    
    initializePeerStubs();
    
    std::cout << "Initialized BlockchainService with " << peerAddresses_.size() << " peers" << std::endl;
}

void BlockchainServiceImpl::initializePeerStubs() {
    for (const auto& address : peerAddresses_) {
        auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        auto stub = blockchain::BlockChainService::NewStub(channel);
        peerStubs_.push_back(std::move(stub));
        
        std::cout << "Created stub for peer: " << address << std::endl;
    }
}

grpc::Status BlockchainServiceImpl::WhisperAuditRequest(
    grpc::ServerContext* context,
    const common::FileAudit* request,
    blockchain::WhisperResponse* response
) {
    std::cout << "Received whispered audit with reqId: " << request->req_id() << std::endl;
    
    bool added = mempool_->addAudit(*request);
    
    if (added) {
        response->set_status("success");
        std::cout << "Added whispered audit to mempool" << std::endl;
    } else {
        response->set_status("failure");
        response->set_error_message("Audit already exists in mempool");
        std::cout << "Whispered audit already exists in mempool" << std::endl;
    }
    
    return grpc::Status::OK;
}

grpc::Status BlockchainServiceImpl::ProposeBlock(
    grpc::ServerContext* context,
    const blockchain::Block* request,
    blockchain::BlockVoteResponse* response
) {
    std::cout << "Received block proposal with ID: " << request->id() << std::endl;
    
    bool valid = blockManager_->verifyBlock(*request);
    
    if (valid) {
        response->set_vote(true);
        response->set_status("success");
        std::cout << "Voted to approve block " << request->id() << std::endl;
    } else {
        response->set_vote(false);
        response->set_status("failure");
        response->set_error_message("Block verification failed");
        std::cout << "Voted to reject block " << request->id() << std::endl;
    }
    
    return grpc::Status::OK;
}

grpc::Status BlockchainServiceImpl::CommitBlock(
    grpc::ServerContext* context,
    const blockchain::Block* request,
    blockchain::BlockCommitResponse* response
) {
    std::cout << "Received commit request for block ID: " << request->id() << std::endl;
    
    bool committed = blockManager_->commitBlock(*request);
    
    if (committed) {
        std::vector<common::FileAudit> audits;
        for (int i = 0; i < request->audits_size(); i++) {
            audits.push_back(request->audits(i));
        }
        mempool_->removeAudits(audits);
        
        response->set_status("success");
        std::cout << "Successfully committed block " << request->id() << std::endl;
    } else {
        response->set_status("failure");
        response->set_error_message("Failed to commit block");
        std::cout << "Failed to commit block " << request->id() << std::endl;
    }
    
    return grpc::Status::OK;
}

grpc::Status BlockchainServiceImpl::GetBlock(
    grpc::ServerContext* context,
    const blockchain::GetBlockRequest* request,
    blockchain::GetBlockResponse* response
) {
    std::cout << "Received request for block ID: " << request->id() << std::endl;
    
    blockchain::Block block = blockManager_->getBlock(request->id());
    
    if (block.id() > 0) {
        *response->mutable_block() = block;
        response->set_status("success");
        std::cout << "Returning block " << block.id() << std::endl;
    } else {
        response->set_status("failure");
        response->set_error_message("Block not found");
        std::cout << "Block " << request->id() << " not found" << std::endl;
    }
    
    return grpc::Status::OK;
}

grpc::Status BlockchainServiceImpl::SendHeartbeat(
    grpc::ServerContext* context,
    const blockchain::HeartbeatRequest* request,
    blockchain::HeartbeatResponse* response
) {
    std::cout << "Received heartbeat from: " << request->from_address() 
              << ", leader: " << request->current_leader_address()
              << ", latest block: " << request->latest_block_id()
              << ", mempool size: " << request->mem_pool_size() << std::endl;
    
    if (node_) {
        node_->handleHeartbeat(*request);
    }
    
    response->set_status("success");
    return grpc::Status::OK;
}

grpc::Status BlockchainServiceImpl::TriggerElection(
    grpc::ServerContext* context,
    const blockchain::TriggerElectionRequest* request,
    blockchain::TriggerElectionResponse* response
) {
    std::cout << "Received election request from: " << request->address() 
              << " with term: " << request->term() << std::endl;
    
    if (!node_) {
        response->set_vote(false);
        response->set_term(0);
        response->set_status("failure");
        response->set_error_message("Node reference not set");
        std::cerr << "Node reference not set in BlockchainServiceImpl" << std::endl;
        return grpc::Status::OK;
    }
    
    int64_t currentTerm = node_->getCurrentTerm();
    
    bool vote = node_->voteInElection(request->address(), request->term());
    
    response->set_vote(vote);
    response->set_term(currentTerm);
    response->set_status("success");
    
    if (!vote) {
        response->set_error_message("Vote denied based on election criteria");
    }
    
    return grpc::Status::OK;
}

grpc::Status BlockchainServiceImpl::NotifyLeadership(
    grpc::ServerContext* context,
    const blockchain::NotifyLeadershipRequest* request,
    blockchain::NotifyLeadershipResponse* response
) {
    std::cout << "Received leadership notification from: " << request->address() << std::endl;
    
    if (!node_) {
        response->set_status("failure");
        response->set_error_message("Node reference not set");
        std::cerr << "Node reference not set in BlockchainServiceImpl" << std::endl;
        return grpc::Status::OK;
    }
    
    node_->updateLeader(request->address());
    
    response->set_status("success");
    return grpc::Status::OK;
}

void BlockchainServiceImpl::proposeNewBlock() {
    std::vector<common::FileAudit> audits = mempool_->getSortedAudits();
    
    if (audits.empty()) {
        std::cout << "No audits in mempool, skipping block proposal" << std::endl;
        return;
    }
    
    blockchain::Block block = blockManager_->createBlock(audits);
    
    bool approved = collectVotes(block);
    
    if (approved) {
        std::cout << "COMMIT: Attempting to commit block " << block.id() << " locally" << std::endl;
        
        auto commitStartTime = std::chrono::steady_clock::now();
        
        bool committed = blockManager_->commitBlock(block);
        
        auto commitEndTime = std::chrono::steady_clock::now();
        auto commitDuration = std::chrono::duration_cast<std::chrono::milliseconds>(commitEndTime - commitStartTime).count();
        
        std::cout << "DEBUG: commitBlock call completed in " << commitDuration << "ms with result: " << (committed ? "success" : "failure") << std::endl;
        
        if (committed) {
            std::cout << "COMMIT: Block " << block.id() << " successfully committed to local chain" << std::endl;
            
            mempool_->removeAudits(audits);
            
            broadcastBlock(block);
            
            std::cout << "Block " << block.id() << " approved and committed" << std::endl;
        } else {
            std::cerr << "ERROR: Block " << block.id() << " failed to commit locally. Check data directory permissions and configuration." << std::endl;
        }
    } else {
        std::cout << "Block " << block.id() << " was not approved by peers" << std::endl;
    }
}

void BlockchainServiceImpl::broadcastBlock(const blockchain::Block& block) {
    std::cout << "Broadcasting block " << block.id() << " to " << peerStubs_.size() << " peers" << std::endl;
    
    std::vector<std::future<bool>> futures;
    
    for (size_t i = 0; i < peerStubs_.size(); i++) {
        futures.push_back(std::async(std::launch::async, [this, &block, i]() {
            grpc::ClientContext context;
            blockchain::BlockCommitResponse response;
            
            auto status = peerStubs_[i]->CommitBlock(&context, block, &response);
            
            if (status.ok() && response.status() == "success") {
                std::cout << "Peer " << i << " committed block " << block.id() << std::endl;
                return true;
            } else {
                std::cerr << "Peer " << i << " failed to commit block " << block.id() << ": " 
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
    
    std::cout << successCount << " out of " << peerStubs_.size() << " peers committed block " << block.id() << std::endl;
}

bool BlockchainServiceImpl::collectVotes(const blockchain::Block& block) {
    std::cout << "Collecting votes for block " << block.id() << " from " << peerStubs_.size() << " peers" << std::endl;
    
    std::vector<std::future<bool>> futures;
    
    for (size_t i = 0; i < peerStubs_.size(); i++) {
        futures.push_back(std::async(std::launch::async, [this, &block, i]() {
            grpc::ClientContext context;
            blockchain::BlockVoteResponse response;
            
            auto status = peerStubs_[i]->ProposeBlock(&context, block, &response);
            
            if (status.ok() && response.status() == "success") {
                std::cout << "Peer " << i << " voted " << (response.vote() ? "yes" : "no") 
                          << " for block " << block.id() << std::endl;
                return response.vote();
            } else {
                std::cerr << "Failed to get vote from peer " << i << " for block " << block.id() << ": " 
                          << (status.ok() ? response.error_message() : status.error_message()) << std::endl;
                return false;
            }
        }));
    }
    
    int yesVotes = 0;
    for (auto& future : futures) {
        if (future.get()) {
            yesVotes++;
        }
    }
    
    yesVotes++;
    
    int totalVotes = peerStubs_.size() + 1;  // +1 for self
    bool approved = yesVotes > totalVotes / 2;
    
    std::cout << "Block " << block.id() << " received " << yesVotes << " out of " << totalVotes 
              << " votes, " << (approved ? "approved" : "rejected") << std::endl;
    
    if (approved) {
        std::cout << "CONSENSUS: Block " << block.id() << " has reached consensus with " 
                  << yesVotes << "/" << totalVotes << " votes" << std::endl;
    }
    
    return approved;
}

bool BlockchainServiceImpl::whisperToAllPeers(const common::FileAudit& audit) {
    std::cout << "Whispering audit with reqId " << audit.req_id() << " to " << peerStubs_.size() << " peers" << std::endl;
    
    std::vector<std::future<bool>> futures;
    
    for (size_t i = 0; i < peerStubs_.size(); i++) {
        futures.push_back(std::async(std::launch::async, [this, &audit, i]() {
            try {
                grpc::ClientContext context;
                blockchain::WhisperResponse response;
                
                auto status = peerStubs_[i]->WhisperAuditRequest(&context, audit, &response);
                
                if (status.ok() && response.status() == "success") {
                    std::cout << "Successfully whispered to peer " << i << std::endl;
                    return true;
                } else {
                    std::cerr << "Failed to whisper to peer " << i << ": " 
                              << (status.ok() ? response.error_message() : status.error_message()) << std::endl;
                    return false;
                }
            } catch (const std::exception& e) {
                std::cerr << "Exception when whispering to peer " << i << ": " << e.what() << std::endl;
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
    
    std::cout << "Successfully whispered to " << successCount << " out of " << peerStubs_.size() << " peers" << std::endl;
    
    return successCount > 0;
}
