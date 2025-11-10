#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <memory>
#include "block_chain.pb.h"
#include "crypto_utils.h"

class BlockManager {
public:
    BlockManager(const std::string& dataDir);
    
    void loadBlocks();
    
    blockchain::Block createBlock(const std::vector<common::FileAudit>& audits);
    
    bool verifyBlock(const blockchain::Block& block);
    
    bool commitBlock(const blockchain::Block& block);
    
    blockchain::Block getBlock(int64_t blockId);
    
    int64_t getLatestBlockId() const;
    
    std::string getLatestBlockHash() const;
    
private:
    std::string dataDir_;
    mutable std::mutex mutex_;
    int64_t latestBlockId_;
    std::string latestBlockHash_;
    std::unordered_map<int64_t, blockchain::Block> blocks_; 
    
    std::string computeBlockHash(const blockchain::Block& block);
    std::string computeMerkleRoot(const std::vector<common::FileAudit>& audits);
    bool saveBlockToDisk(const blockchain::Block& block);
    blockchain::Block loadBlockFromDisk(int64_t blockId);
    std::string getBlockFilePath(int64_t blockId);
    
    bool verifyBlockNoLock(const blockchain::Block& block);
};
