#include "file_audit_service.h"
#include "crypto_utils.h"
#include <iostream>
#include <sstream>

FileAuditServiceImpl::FileAuditServiceImpl(
    std::shared_ptr<Mempool> mempool,
    std::shared_ptr<BlockchainServiceImpl> blockchainService
) : mempool_(mempool), blockchainService_(blockchainService) {
    
    std::cout << "Initialized FileAuditService" << std::endl;
}

grpc::Status FileAuditServiceImpl::SubmitAudit(
    grpc::ServerContext* context,
    const common::FileAudit* request,
    fileaudit::FileAuditResponse* response
) {
    std::cout << "Received audit submission with reqId: " << request->req_id() << std::endl;
    
    response->set_req_id(request->req_id());
    
    if (!validateAudit(*request)) {
        response->set_status("failure");
        response->set_error_message("Invalid audit data");
        std::cerr << "Audit validation failed for reqId: " << request->req_id() << std::endl;
        return grpc::Status::OK;
    }
    
    if (!verifySignature(*request)) {
        response->set_status("failure");
        response->set_error_message("Signature verification failed");
        std::cerr << "Signature verification failed for reqId: " << request->req_id() << std::endl;
        return grpc::Status::OK;
    }
    
    bool added = mempool_->addAudit(*request);
    
    if (!added) {
        response->set_status("failure");
        response->set_error_message("Audit already exists in mempool");
        std::cout << "Audit already exists in mempool: " << request->req_id() << std::endl;
        return grpc::Status::OK;
    }
    
    blockchainService_->whisperToAllPeers(*request);
    
    response->set_status("success");
    std::cout << "Successfully processed audit submission: " << request->req_id() << std::endl;
    
    return grpc::Status::OK;
}

bool FileAuditServiceImpl::validateAudit(const common::FileAudit& audit) {
    if (audit.req_id().empty()) {
        std::cerr << "Missing reqId" << std::endl;
        return false;
    }
    
    if (audit.file_info().file_id().empty()) {
        std::cerr << "Missing file_id" << std::endl;
        return false;
    }
    
    if (audit.file_info().file_name().empty()) {
        std::cerr << "Missing file_name" << std::endl;
        return false;
    }
    
    if (audit.user_info().user_id().empty()) {
        std::cerr << "Missing user_id" << std::endl;
        return false;
    }
    
    if (audit.user_info().user_name().empty()) {
        std::cerr << "Missing user_name" << std::endl;
        return false;
    }
    
    if (audit.access_type() == common::AccessType::UNKNOWN) {
        std::cerr << "Invalid access_type" << std::endl;
        return false;
    }
    
    if (audit.timestamp() <= 0) {
        std::cerr << "Invalid timestamp" << std::endl;
        return false;
    }
    
    if (audit.signature().empty()) {
        std::cerr << "Missing signature" << std::endl;
        return false;
    }
    
    if (audit.public_key().empty()) {
        std::cerr << "Missing public_key" << std::endl;
        return false;
    }
    
    return true;
}

bool FileAuditServiceImpl::verifySignature(const common::FileAudit& audit) {
    
    
    try {
        nlohmann::json auditJson;
        
        auditJson["access_type"] = static_cast<int>(audit.access_type());
        auditJson["file_info"] = nlohmann::json::object();
        auditJson["file_info"]["file_id"] = audit.file_info().file_id();
        auditJson["file_info"]["file_name"] = audit.file_info().file_name();
        auditJson["req_id"] = audit.req_id();
        auditJson["timestamp"] = audit.timestamp();
        auditJson["user_info"] = nlohmann::json::object();
        auditJson["user_info"]["user_id"] = audit.user_info().user_id();
        auditJson["user_info"]["user_name"] = audit.user_info().user_name();
        
        std::string dataToVerify = auditJson.dump(-1, ' ', false, nlohmann::json::error_handler_t::strict);
        
        dataToVerify.erase(std::remove_if(dataToVerify.begin(), dataToVerify.end(), 
                                         [](char c) { return c == ' ' || c == '\n' || c == '\t'; }), 
                          dataToVerify.end());
        
        size_t pos;
        while ((pos = dataToVerify.find(": ")) != std::string::npos) {
            dataToVerify.replace(pos, 2, ":");
        }
        while ((pos = dataToVerify.find(", ")) != std::string::npos) {
            dataToVerify.replace(pos, 2, ",");
        }
        
        std::cout << "Data to verify: " << dataToVerify << std::endl;
        
        return crypto::verifySignature(dataToVerify, audit.signature(), audit.public_key());
    } catch (const std::exception& e) {
        std::cerr << "Exception during signature verification: " << e.what() << std::endl;
        return false;
    }
    
}
