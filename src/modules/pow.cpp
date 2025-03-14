#include "../headers/pow.h"
#include <openssl/sha.h>
#include <sstream>
#include <iostream>
std::string sha256(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)input.c_str(), input.size(), hash);
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << (int)hash[i];
    }
    return ss.str();
}
std::string performPoW(const std::string& data, int difficulty) {
    std::cout << "Performing PoW for: " << data << std::endl;
    int nonce = 0;
    const int MAX_ATTEMPTS = 1e8;  // Avoid infinite loops
    while (nonce < MAX_ATTEMPTS) {
        std::string attempt = data + std::to_string(nonce);
        std::string hash = sha256(attempt);
        
        if (hash.substr(0, difficulty) == std::string(difficulty, '0')) {
            std::cout << "PoW solved for " << data << " at nonce: " << nonce << std::endl;
            return hash;
        }
        nonce++;
    }
    std::cerr << "PoW failed: max attempts reached for " << data << std::endl;
    return "INVALID_POW";
}
