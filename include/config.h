#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

class Config {
public:
    Config(const std::string& configFile);
    
    struct ServerInfo {
        std::string address;
    };
    
    struct NodeInfo {
        std::string id;
        std::string address;
        std::string dataDir;
    };
    
    const std::vector<ServerInfo>& getServers() const { return servers; }
    const NodeInfo& getNodeInfo() const { return nodeInfo; }
    
    void setNodeId(const std::string& id) { nodeInfo.id = id; }
    void setNodeAddress(const std::string& addr) { nodeInfo.address = addr; }
    void setDataDir(const std::string& dir) { nodeInfo.dataDir = dir; }
    
private:
    std::vector<ServerInfo> servers;
    NodeInfo nodeInfo;
    
    void loadConfig(const std::string& configFile);
};
