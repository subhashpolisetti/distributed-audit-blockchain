#include "node.h"
#include <iostream>
#include <string>
#include <csignal>
#include <cstdlib>
#include <filesystem>

std::unique_ptr<Node> g_node;

void signalHandler(int signal) {
    std::cout << "Received signal " << signal << std::endl;
    if (g_node) {
        std::cout << "Stopping node..." << std::endl;
        g_node->stop();
    }
}

int main(int argc, char* argv[]) {
    std::string configFile = "config.json";
    std::string nodeId;
    std::string nodeAddress;
    std::string dataDir;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            return 0;
        } else if (arg == "--config" && i + 1 < argc) {
            configFile = argv[++i];
        } else if (arg == "--id" && i + 1 < argc) {
            nodeId = argv[++i];
        } else if (arg == "--address" && i + 1 < argc) {
            nodeAddress = argv[++i];
        } else if (arg == "--data-dir" && i + 1 < argc) {
            dataDir = argv[++i];
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            return 1;
        }
    }
    
    try {
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);
        
        g_node = std::make_unique<Node>(configFile);
        
        if (!nodeId.empty()) {
            g_node->getConfig()->setNodeId(nodeId);
        }
        
        if (!nodeAddress.empty()) {
            g_node->getConfig()->setNodeAddress(nodeAddress);
        }
        
        if (!dataDir.empty()) {
            g_node->getConfig()->setDataDir(dataDir);
        }
        
        std::string dataDir = g_node->getConfig()->getNodeInfo().dataDir;
        std::cout << "STARTUP: Verifying data directory: " << dataDir << std::endl;
        if (!std::filesystem::exists(dataDir)) {
            std::cout << "STARTUP: Data directory does not exist, creating: " << dataDir << std::endl;
            try {
                std::filesystem::create_directories(dataDir);
                std::cout << "STARTUP: Successfully created data directory: " << dataDir << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "ERROR: Failed to create data directory: " << e.what() << std::endl;
            }
        }
        
        g_node->initialize();
        
        g_node->start();
        
        g_node->wait();
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
