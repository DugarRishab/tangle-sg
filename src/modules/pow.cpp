#include "../headers/pow.h"
#include <openssl/sha.h>
#include <sstream>
#include <iostream>
#include <iomanip>  // Required for std::setw and std::setfill

//performPoW for default function
//performPoWclk for clockwork PoW
//performPoWfeistel for Feistel network PoW
//performPoWcm for Counter-Mode PoW

std::string sha256(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)input.c_str(), input.size(), hash);
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << (int)hash[i];
    }
    return ss.str();
}

// XOR 
std::string xor_strings(const std::string &a, const std::string &b) {
    std::string result;
    for (size_t i = 0; i < a.size(); ++i) {
        result += a[i] ^ b[i % b.size()];
    }
    return result;
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

std::string performPoWclk(const std::string& data, int difficulty) {
    std::cout << "Performing Counter-Mode PoW for: " << data << std::endl;
    int nonce = 0;
    const int MAX_ATTEMPTS = 1e8;

    while (nonce < MAX_ATTEMPTS) {
        std::string chain = data + std::to_string(nonce);
        
        // Counter-mode iterations (fixed 64 rounds)
        for (int i = 0; i < 64; i++) {
            std::string block = chain + "|" + std::to_string(i);
            chain = sha256(block);
        }
        
        // Final hash
        std::string hash = sha256(chain);

        // Difficulty check (leading zeros)
        if (hash.substr(0, difficulty) == std::string(difficulty, '0')) {
            std::cout << "Counter-Mode PoW solved for " << data 
                      << " at nonce: " << nonce << std::endl;
            return hash;
        }
        nonce++;
    }
    
    std::cerr << "Counter-Mode PoW failed: max attempts reached for " 
              << data << std::endl;
    return "INVALID_POW";
}

// Main PoW function with Feistel network
std::string performPoWfeistel(const std::string& data, int difficulty) {
    std::cout << "Performing Feistel PoW for: " << data << std::endl;
    int nonce = 0;
    const int MAX_ATTEMPTS = 1e8;

    while (nonce < MAX_ATTEMPTS) {
        // Prepare input (data + nonce)
        std::string input = data + std::to_string(nonce);
        input.resize(64, 0);  // Pad to 64 bytes

        // Split into left and right halves
        std::string left = input.substr(0, 32);
        std::string right = input.substr(32, 32);

        // 8-round Feistel network (simplified)
        for (int round = 0; round < 8; round++) {
            std::string temp = right;
            
            // Feistel function: SHA-256(right + round)
            std::string round_input = right + std::to_string(round);
            std::string feistel_output = sha256(round_input);
            
            // XOR and swap
            right = xor_strings(left, feistel_output);
            left = temp;
        }

        // Final combination
        std::string final_output = left + right;
        std::string hash = sha256(final_output);

        // Difficulty check (leading zeros)
        if (hash.substr(0, difficulty) == std::string(difficulty, '0')) {
            std::cout << "Feistel PoW solved for " << data 
                      << " at nonce: " << nonce << std::endl;
            return hash;
        }
        nonce++;
    }
    
    std::cerr << "Feistel PoW failed: max attempts reached for " 
              << data << std::endl;
    return "INVALID_POW";
}

std::string performPoWcm(const std::string& data, int difficulty) {
    std::cout << "Performing Counter-Mode PoW for: " << data << std::endl;
    int nonce = 0;
    const int MAX_ATTEMPTS = 1e8;

    while (nonce < MAX_ATTEMPTS) {
        std::string chain = data + std::to_string(nonce);
        
        // Counter-mode iterations (fixed 64 rounds)
        for (int i = 0; i < 64; i++) {
            std::string block = chain + "|" + std::to_string(i);
            chain = sha256(block);
        }
        
        // Final hash
        std::string hash = sha256(chain);

        // Difficulty check (leading zeros)
        if (hash.substr(0, difficulty) == std::string(difficulty, '0')) {
            std::cout << "Counter-Mode PoW solved for " << data 
                      << " at nonce: " << nonce << std::endl;
            return hash;
        }
        nonce++;
    }
    
    std::cerr << "Counter-Mode PoW failed: max attempts reached for " 
              << data << std::endl;
    return "INVALID_POW";
}