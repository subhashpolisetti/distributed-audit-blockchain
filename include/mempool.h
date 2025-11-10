#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <string>
#include "common.pb.h"

class Mempool {
public:
    Mempool();
    
    bool addAudit(const common::FileAudit& audit);
    
    std::vector<common::FileAudit> getAllAudits();
    
    std::vector<common::FileAudit> getSortedAudits();
    
    void removeAudits(const std::vector<common::FileAudit>& audits);
    
    bool containsAudit(const std::string& reqId);
    
    size_t size() const;
    
private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, common::FileAudit> audits_; 
    std::unordered_set<std::string> auditHashes_; 
    
    std::string computeAuditHash(const common::FileAudit& audit);
};
