#pragma once

#include <string>
#include <vector>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/sha.h>

namespace crypto {

std::string sha256(const std::string& data);
std::string sha256(const std::vector<uint8_t>& data);

std::string computeMerkleRoot(const std::vector<std::string>& hashes);

bool verifySignature(const std::string& data, const std::string& signature, const std::string& publicKeyPEM);

std::vector<uint8_t> hexToBytes(const std::string& hex);
std::string bytesToHex(const std::vector<uint8_t>& bytes);

std::string base64Encode(const std::vector<uint8_t>& data);
std::vector<uint8_t> base64Decode(const std::string& base64);

} 