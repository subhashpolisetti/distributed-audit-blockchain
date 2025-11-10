#include "config.h"
#include <iostream>
#include <stdexcept>
#include <fstream>

Config::Config(const std::string& configFile) {
    loadConfig(configFile);
}

void Config::loadConfig(const std::string& configFile) {
    try {
        std::ifstream file(configFile);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open config file: " + configFile);
        }
        
        nlohmann::json config;
        file >> config;
        
        if (config.contains("servers") && config["servers"].is_array()) {
            for (const auto& server : config["servers"]) {
                ServerInfo serverInfo;
                serverInfo.address = server["address"];
                servers.push_back(serverInfo);
            }
        }
        
        if (config.contains("node")) {
            nodeInfo.id = config["node"]["id"];
            nodeInfo.address = config["node"]["address"];
            nodeInfo.dataDir = config["node"]["data_dir"];
        } else {
            nodeInfo.id = "node1";
            nodeInfo.address = "0.0.0.0:50051";
            nodeInfo.dataDir = "./data";
        }
        
        std::cout << "Loaded configuration from " << configFile << std::endl;
        std::cout << "Node ID: " << nodeInfo.id << std::endl;
        std::cout << "Node Address: " << nodeInfo.address << std::endl;
        std::cout << "Data Directory: " << nodeInfo.dataDir << std::endl;
        std::cout << "Peer Servers: " << servers.size() << std::endl;
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "Error parsing JSON config file: " << e.what() << std::endl;
        throw std::runtime_error("Failed to load configuration");
    } catch (const std::exception& e) {
        std::cerr << "Error loading configuration: " << e.what() << std::endl;
        throw std::runtime_error("Failed to load configuration");
    }
}
