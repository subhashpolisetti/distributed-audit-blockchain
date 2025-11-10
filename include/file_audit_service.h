#pragma once

#include <memory>
#include <algorithm>
#include <grpcpp/grpcpp.h>
#include <nlohmann/json.hpp>
#include "file_audit.grpc.pb.h"
#include "mempool.h"
#include "blockchain_service.h"

class FileAuditServiceImpl final : public fileaudit::FileAuditService::Service {
public:
    FileAuditServiceImpl(
        std::shared_ptr<Mempool> mempool,
        std::shared_ptr<BlockchainServiceImpl> blockchainService
    );
    
    grpc::Status SubmitAudit(
        grpc::ServerContext* context,
        const common::FileAudit* request,
        fileaudit::FileAuditResponse* response
    ) override;
    
private:
    std::shared_ptr<Mempool> mempool_;
    std::shared_ptr<BlockchainServiceImpl> blockchainService_;
    
    bool validateAudit(const common::FileAudit& audit);
    bool verifySignature(const common::FileAudit& audit);
};
