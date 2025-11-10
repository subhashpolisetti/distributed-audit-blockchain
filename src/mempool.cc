#include "mempool.h"
#include "crypto_utils.h"
#include <algorithm>
#include <iostream>

Mempool::Mempool() {
    std::cout << "Initializing mempool" << std::endl;
}

bool Mempool::addAudit(const common::FileAudit& audit) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (audits_.find(audit.req_id()) != audits_.end()) {
        std::cout << "Audit with reqId " << audit.req_id() << " already exists in mempool" << std::endl;
        return false;
    }
    
    std::string auditHash = computeAuditHash(audit);
    
    if (auditHashes_.find(auditHash) != auditHashes_.end()) {
        std::cout << "Duplicate audit detected (different reqId but same content)" << std::endl;
        return false;
    }
    
    audits_[audit.req_id()] = audit;
    auditHashes_.insert(auditHash);
    
    std::cout << "Added audit with reqId " << audit.req_id() << " to mempool. Current size: " 
              << audits_.size() << std::endl;
    
    return true;
}

std::vector<common::FileAudit> Mempool::getAllAudits() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<common::FileAudit> result;
    result.reserve(audits_.size());
    
    for (const auto& pair : audits_) {
        result.push_back(pair.second);
    }
    
    return result;
}

std::vector<common::FileAudit> Mempool::getSortedAudits() {
    std::vector<common::FileAudit> audits = getAllAudits();
    
    std::sort(audits.begin(), audits.end(), 
              [](const common::FileAudit& a, const common::FileAudit& b) {
                  return a.timestamp() < b.timestamp();
              });
    
    return audits;
}

void Mempool::removeAudits(const std::vector<common::FileAudit>& audits) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& audit : audits) {
        auto it = audits_.find(audit.req_id());
        if (it != audits_.end()) {
            std::string auditHash = computeAuditHash(it->second);
            auditHashes_.erase(auditHash);
            
            audits_.erase(it);
        }
    }
    
    std::cout << "Removed " << audits.size() << " audits from mempool. Current size: " 
              << audits_.size() << std::endl;
}

bool Mempool::containsAudit(const std::string& reqId) {
    std::lock_guard<std::mutex> lock(mutex_);
    return audits_.find(reqId) != audits_.end();
}

size_t Mempool::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return audits_.size();
}

std::string Mempool::computeAuditHash(const common::FileAudit& audit) {
    std::string auditStr = 
        audit.file_info().file_id() + ":" + 
        audit.file_info().file_name() + ":" + 
        audit.user_info().user_id() + ":" + 
        audit.user_info().user_name() + ":" + 
        std::to_string(static_cast<int>(audit.access_type())) + ":" + 
        std::to_string(audit.timestamp());
    
    return crypto::sha256(auditStr);
}
