#include "block_manager.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

BlockManager::BlockManager(const std::string& dataDir) 
    : dataDir_(dataDir), latestBlockId_(-1), latestBlockHash_("") {
    
    if (!fs::exists(dataDir_)) {
        fs::create_directories(dataDir_);
        std::cout << "Created data directory: " << dataDir_ << std::endl;
    }
    
    loadBlocks();
    
    std::cout << "Initialized BlockManager with data directory: " << dataDir_ << std::endl;
    std::cout << "Latest block ID: " << latestBlockId_ << std::endl;
    std::cout << "Latest block hash: " << latestBlockHash_ << std::endl;
}

void BlockManager::loadBlocks() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        for (const auto& entry : fs::directory_iterator(dataDir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string filename = entry.path().filename().string();
                
                if (filename.substr(0, 6) == "block_") {
                    size_t dotPos = filename.find(".json");
                    if (dotPos != std::string::npos) {
                        std::string idStr = filename.substr(6, dotPos - 6);
                        try {
                            int64_t blockId = std::stoll(idStr);
                            blockchain::Block block = loadBlockFromDisk(blockId);
                            
                            blocks_[blockId] = block;
                            
                            if (blockId > latestBlockId_) {
                                latestBlockId_ = blockId;
                                latestBlockHash_ = block.hash();
                            }
                            
                            std::cout << "Loaded block " << blockId << " from disk" << std::endl;
                        } catch (const std::exception& e) {
                            std::cerr << "Error parsing block ID from filename: " << filename << std::endl;
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading blocks: " << e.what() << std::endl;
    }
}

blockchain::Block BlockManager::createBlock(const std::vector<common::FileAudit>& audits) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    blockchain::Block block;
    
    block.set_id(latestBlockId_ + 1);
    
    block.set_previous_hash(latestBlockHash_);
    
    for (const auto& audit : audits) {
        *block.add_audits() = audit;
    }
    
    block.set_merkle_root(computeMerkleRoot(audits));
    
    std::string blockHash = computeBlockHash(block);
    block.set_hash(blockHash);
    
    std::cout << "Created new block with ID " << block.id() << " containing " 
              << audits.size() << " audits" << std::endl;
    
    return block;
}

bool BlockManager::verifyBlock(const blockchain::Block& block) {


    std::lock_guard<std::mutex> lock(mutex_);
    
    std::cout << "VERIFY: Starting verification of block " << block.id() << std::endl;
    std::cout << "VERIFY: Current latest block ID: " << latestBlockId_ << std::endl;
    std::cout << "VERIFY: Current latest block hash: " << latestBlockHash_ << std::endl;
    
    std::cout << "VERIFY: Checking block ID: " << block.id() << " vs expected " << (latestBlockId_ + 1) << std::endl;
    if (block.id() != latestBlockId_ + 1) {
        std::cerr << "ERROR: Block ID mismatch. Expected: " << (latestBlockId_ + 1) 
                  << ", Got: " << block.id() << std::endl;
        return false;
    }
    
    std::cout << "VERIFY: Checking previous hash: " << block.previous_hash() << " vs expected " << latestBlockHash_ << std::endl;
    if (block.previous_hash() != latestBlockHash_) {
        std::cerr << "ERROR: Previous hash mismatch. Expected: " << latestBlockHash_ 
                  << ", Got: " << block.previous_hash() << std::endl;
        return false;
    }
    
    std::cout << "VERIFY: Computing merkle root for " << block.audits_size() << " audits" << std::endl;
    std::vector<common::FileAudit> audits;
    for (int i = 0; i < block.audits_size(); i++) {
        audits.push_back(block.audits(i));
    }
    
    std::string computedMerkleRoot = computeMerkleRoot(audits);
    std::cout << "VERIFY: Checking merkle root: " << block.merkle_root() << " vs computed " << computedMerkleRoot << std::endl;
    if (computedMerkleRoot != block.merkle_root()) {
        std::cerr << "ERROR: Merkle root mismatch. Expected: " << block.merkle_root() 
                  << ", Computed: " << computedMerkleRoot << std::endl;
        return false;
    }
    
    std::cout << "VERIFY: Computing block hash" << std::endl;
    std::string computedHash = computeBlockHash(block);
    std::cout << "VERIFY: Checking block hash: " << block.hash() << " vs computed " << computedHash << std::endl;
    if (computedHash != block.hash()) {
        std::cerr << "ERROR: Block hash mismatch. Expected: " << block.hash() 
                  << ", Computed: " << computedHash << std::endl;
        return false;
    }
    
    std::cout << "VERIFY: Block " << block.id() << " verification successful" << std::endl;
    return true;
}

bool BlockManager::commitBlock(const blockchain::Block& block) {
    try {
        std::cout << "COMMIT: Starting commit process for block " << block.id() << std::endl;
        
        bool verified = verifyBlockNoLock(block);
        if (!verified) {
            std::cerr << "ERROR: Block verification failed during commit" << std::endl;
            return false;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::cout << "COMMIT: Block " << block.id() << " verified, saving to disk at " << dataDir_ << std::endl;
        
        std::cout << "DEBUG: About to call saveBlockToDisk for block " << block.id() << std::endl;
        
        if (!saveBlockToDisk(block)) {
            std::cerr << "ERROR: Failed to save block to disk at " << dataDir_ << std::endl;
            return false;
        }
        
        blocks_[block.id()] = block;
        latestBlockId_ = block.id();
        latestBlockHash_ = block.hash();
        
        std::cout << "Committed block " << block.id() << " to the chain" << std::endl;
        std::cout << "COMMIT: Block " << block.id() << " successfully committed to local chain" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Exception in commitBlock: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "ERROR: Unknown exception in commitBlock" << std::endl;
        return false;
    }
}

blockchain::Block BlockManager::getBlock(int64_t blockId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = blocks_.find(blockId);
    if (it != blocks_.end()) {
        return it->second;
    }
    
    try {
        return loadBlockFromDisk(blockId);
    } catch (const std::exception& e) {
        std::cerr << "Error loading block " << blockId << ": " << e.what() << std::endl;
        return blockchain::Block();
    }
}

int64_t BlockManager::getLatestBlockId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latestBlockId_;
}

std::string BlockManager::getLatestBlockHash() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latestBlockHash_;
}

std::string BlockManager::computeBlockHash(const blockchain::Block& block) {
    std::cout << "HASH: Computing hash for block " << block.id() << std::endl;
    
    std::string blockStr = 
        std::to_string(block.id()) + ":" + 
        block.previous_hash() + ":" + 
        block.merkle_root();
    
    std::cout << "HASH: Block string representation: " << blockStr << std::endl;
    
    std::string hash = crypto::sha256(blockStr);
    std::cout << "HASH: Computed hash: " << hash << std::endl;
    
    return hash;
}

std::string BlockManager::computeMerkleRoot(const std::vector<common::FileAudit>& audits) {
    std::cout << "MERKLE: Computing merkle root for " << audits.size() << " audits" << std::endl;
    
    if (audits.empty()) {
        std::cout << "MERKLE: No audits, returning empty hash" << std::endl;
        return crypto::sha256("");
    }
    
    std::vector<std::string> hashes;
    for (size_t i = 0; i < audits.size(); i++) {
        const auto& audit = audits[i];
        std::string auditStr = 
            audit.req_id() + ":" + 
            audit.file_info().file_id() + ":" + 
            audit.file_info().file_name() + ":" + 
            audit.user_info().user_id() + ":" + 
            audit.user_info().user_name() + ":" + 
            std::to_string(static_cast<int>(audit.access_type())) + ":" + 
            std::to_string(audit.timestamp());
        
        std::string auditHash = crypto::sha256(auditStr);
        std::cout << "MERKLE: Audit " << i << " hash: " << auditHash << std::endl;
        hashes.push_back(auditHash);
    }
    
    std::string root = crypto::computeMerkleRoot(hashes);
    std::cout << "MERKLE: Computed root: " << root << std::endl;
    
    return root;
}

bool BlockManager::saveBlockToDisk(const blockchain::Block& block) {
    std::string filePath = getBlockFilePath(block.id());
    
    std::cout << "DISK: Saving block " << block.id() << " to file: " << filePath << std::endl;
    std::cout << "DISK: Current working directory: " << fs::current_path().string() << std::endl;
    std::cout << "DISK: Data directory: " << dataDir_ << std::endl;
    
    try {
        if (!fs::exists(dataDir_)) {
            std::cerr << "ERROR: Data directory does not exist: " << dataDir_ << std::endl;
            try {
                fs::create_directories(dataDir_);
                std::cout << "DISK: Created data directory: " << dataDir_ << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "ERROR: Failed to create data directory: " << e.what() << std::endl;
                return false;
            }
        }
        
        if (!fs::exists(dataDir_)) {
            std::cerr << "ERROR: Data directory still does not exist after creation attempt: " << dataDir_ << std::endl;
            return false;
        }
        
        std::cout << "DISK: Data directory exists: " << dataDir_ << std::endl;
        
        json j;
        j["id"] = block.id();
        j["hash"] = block.hash();
        j["previous_hash"] = block.previous_hash();
        j["merkle_root"] = block.merkle_root();
        
        j["audits"] = json::array();
        for (int i = 0; i < block.audits_size(); i++) {
            const auto& audit = block.audits(i);
            json auditJson;
            
            auditJson["req_id"] = audit.req_id();
            
            auditJson["file_info"] = {
                {"file_id", audit.file_info().file_id()},
                {"file_name", audit.file_info().file_name()}
            };
            
            auditJson["user_info"] = {
                {"user_id", audit.user_info().user_id()},
                {"user_name", audit.user_info().user_name()}
            };
            
            auditJson["access_type"] = static_cast<int>(audit.access_type());
            auditJson["timestamp"] = audit.timestamp();
            auditJson["signature"] = audit.signature();
            auditJson["public_key"] = audit.public_key();
            
            j["audits"].push_back(auditJson);
        }
        
        std::ofstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "ERROR: Failed to open file for writing: " << filePath << std::endl;
            
            try {
                auto perms = fs::status(dataDir_).permissions();
                std::cout << "DISK: Directory permissions check - can write: " 
                          << ((perms & fs::perms::owner_write) != fs::perms::none) << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "ERROR: Failed to check directory permissions: " << e.what() << std::endl;
            }
            
            return false;
        }
        
        std::string jsonStr = j.dump(2);
        file << jsonStr;
        
        if (file.fail()) {
            std::cerr << "ERROR: Failed to write to file: " << filePath << std::endl;
            file.close();
            return false;
        }
        
        file.close();
        
        if (!fs::exists(filePath)) {
            std::cerr << "ERROR: File does not exist after writing: " << filePath << std::endl;
            return false;
        }
        
        std::cout << "DISK: Successfully saved block " << block.id() << " to disk: " 
                  << filePath << " (" << jsonStr.size() << " bytes)" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Exception while saving block to disk: " << e.what() << std::endl;
        return false;
    }
}

blockchain::Block BlockManager::loadBlockFromDisk(int64_t blockId) {
    std::string filePath = getBlockFilePath(blockId);
    
    try {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filePath);
        }
        
        json j;
        file >> j;
        file.close();
        
        blockchain::Block block;
        block.set_id(j["id"]);
        block.set_hash(j["hash"]);
        block.set_previous_hash(j["previous_hash"]);
        block.set_merkle_root(j["merkle_root"]);
        
        for (const auto& auditJson : j["audits"]) {
            common::FileAudit* audit = block.add_audits();
            
            audit->set_req_id(auditJson["req_id"]);
            
            audit->mutable_file_info()->set_file_id(auditJson["file_info"]["file_id"]);
            audit->mutable_file_info()->set_file_name(auditJson["file_info"]["file_name"]);
            
            audit->mutable_user_info()->set_user_id(auditJson["user_info"]["user_id"]);
            audit->mutable_user_info()->set_user_name(auditJson["user_info"]["user_name"]);
            
            audit->set_access_type(static_cast<common::AccessType>(auditJson["access_type"]));
            audit->set_timestamp(auditJson["timestamp"]);
            audit->set_signature(auditJson["signature"]);
            audit->set_public_key(auditJson["public_key"]);
        }
        
        return block;
    } catch (const std::exception& e) {
        std::cerr << "Error loading block from disk: " << e.what() << std::endl;
        throw;
    }
}

std::string BlockManager::getBlockFilePath(int64_t blockId) {
    return dataDir_ + "/block_" + std::to_string(blockId) + ".json";
}

bool BlockManager::verifyBlockNoLock(const blockchain::Block& block) {
    std::cout << "VERIFY-NOLOCK: Starting verification of block " << block.id() << std::endl;
    std::cout << "VERIFY-NOLOCK: Current latest block ID: " << latestBlockId_ << std::endl;
    std::cout << "VERIFY-NOLOCK: Current latest block hash: " << latestBlockHash_ << std::endl;
    
    std::cout << "VERIFY-NOLOCK: Checking block ID: " << block.id() << " vs expected " << (latestBlockId_ + 1) << std::endl;
    if (block.id() != latestBlockId_ + 1) {
        std::cerr << "ERROR: Block ID mismatch. Expected: " << (latestBlockId_ + 1) 
                  << ", Got: " << block.id() << std::endl;
        return false;
    }
    
    std::cout << "VERIFY-NOLOCK: Checking previous hash: " << block.previous_hash() << " vs expected " << latestBlockHash_ << std::endl;
    if (block.previous_hash() != latestBlockHash_) {
        std::cerr << "ERROR: Previous hash mismatch. Expected: " << latestBlockHash_ 
                  << ", Got: " << block.previous_hash() << std::endl;
        return false;
    }
    
    std::cout << "VERIFY-NOLOCK: Computing merkle root for " << block.audits_size() << " audits" << std::endl;
    std::vector<common::FileAudit> audits;
    for (int i = 0; i < block.audits_size(); i++) {
        audits.push_back(block.audits(i));
    }
    
    std::string computedMerkleRoot = computeMerkleRoot(audits);
    std::cout << "VERIFY-NOLOCK: Checking merkle root: " << block.merkle_root() << " vs computed " << computedMerkleRoot << std::endl;
    if (computedMerkleRoot != block.merkle_root()) {
        std::cerr << "ERROR: Merkle root mismatch. Expected: " << block.merkle_root() 
                  << ", Computed: " << computedMerkleRoot << std::endl;
        return false;
    }
    
    std::cout << "VERIFY-NOLOCK: Computing block hash" << std::endl;
    std::string computedHash = computeBlockHash(block);
    std::cout << "VERIFY-NOLOCK: Checking block hash: " << block.hash() << " vs computed " << computedHash << std::endl;
    if (computedHash != block.hash()) {
        std::cerr << "ERROR: Block hash mismatch. Expected: " << block.hash() 
                  << ", Computed: " << computedHash << std::endl;
        return false;
    }
    
    std::cout << "VERIFY-NOLOCK: Block " << block.id() << " verification successful" << std::endl;
    return true;
}
