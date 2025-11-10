#include "crypto_utils.h"
#include <sstream>
#include <iomanip>
#include <vector>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

namespace crypto {

std::string sha256(const std::string& data) {
    return sha256(std::vector<uint8_t>(data.begin(), data.end()));
}

std::string sha256(const std::vector<uint8_t>& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data.data(), data.size());
    SHA256_Final(hash, &sha256);
    
    return bytesToHex(std::vector<uint8_t>(hash, hash + SHA256_DIGEST_LENGTH));
}

std::string computeMerkleRoot(const std::vector<std::string>& hashes) {
    if (hashes.empty()) {
        return sha256("");
    }
    
    if (hashes.size() == 1) {
        return hashes[0];
    }
    
    std::vector<std::string> nextLevel;
    
    for (size_t i = 0; i < hashes.size(); i += 2) {
        if (i + 1 < hashes.size()) {
            std::string combined = hashes[i] + hashes[i + 1];
            nextLevel.push_back(sha256(combined));
        } else {
            std::string combined = hashes[i] + hashes[i];
            nextLevel.push_back(sha256(combined));
        }
    }
    
    return computeMerkleRoot(nextLevel);
}

bool verifySignature(const std::string& data, const std::string& signatureHex, const std::string& publicKeyPEM) {
    try {
        std::vector<uint8_t> signatureBytes = hexToBytes(signatureHex);
        
        BIO* bio = BIO_new_mem_buf(publicKeyPEM.c_str(), -1);
        if (!bio) {
            return false;
        }
        
        EVP_PKEY* pubKey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);
        
        if (!pubKey) {
            return false;
        }
        
        EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
        if (!mdctx) {
            EVP_PKEY_free(pubKey);
            return false;
        }
        
        if (EVP_DigestVerifyInit(mdctx, nullptr, EVP_sha256(), nullptr, pubKey) != 1) {
            EVP_MD_CTX_free(mdctx);
            EVP_PKEY_free(pubKey);
            return false;
        }
        
        if (EVP_DigestVerifyUpdate(mdctx, data.c_str(), data.size()) != 1) {
            EVP_MD_CTX_free(mdctx);
            EVP_PKEY_free(pubKey);
            return false;
        }
        
        int result = EVP_DigestVerifyFinal(mdctx, signatureBytes.data(), signatureBytes.size());
        
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pubKey);
        
        return result == 1;
    } catch (const std::exception& e) {
        return false;
    }
}

std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoi(byteString, nullptr, 16));
        bytes.push_back(byte);
    }
    
    return bytes;
}

std::string bytesToHex(const std::vector<uint8_t>& bytes) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    
    for (const auto& byte : bytes) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    
    return ss.str();
}

std::string base64Encode(const std::vector<uint8_t>& data) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_write(b64, data.data(), static_cast<int>(data.size()));
    BIO_flush(b64);
    
    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);
    
    std::string result(bptr->data, bptr->length - 1); 
    BIO_free_all(b64);
    
    return result;
}

std::vector<uint8_t> base64Decode(const std::string& base64) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new_mem_buf(base64.c_str(), static_cast<int>(base64.length()));
    bmem = BIO_push(b64, bmem);
    
    std::vector<uint8_t> result(base64.length());
    int decodedSize = BIO_read(bmem, result.data(), static_cast<int>(result.size()));
    
    BIO_free_all(bmem);
    
    if (decodedSize > 0) {
        result.resize(decodedSize);
        return result;
    }
    
    return std::vector<uint8_t>();
}

} 
