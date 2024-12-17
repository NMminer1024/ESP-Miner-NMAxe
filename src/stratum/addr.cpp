#include <Arduino.h>
#include <vector>
#include "logger.h"
#include "addr.h"
#include "csha256.h"

#define SHA256_SIZE 32
static const char* BASE58_ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
static const char *BECH32_ALPHABET  = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

bool isBase58(const String &str) {
    for (char c : str) {
        if (std::strchr(BASE58_ALPHABET, c) == nullptr) {
            return false;
        }
    }
    return true; 
}

std::vector<unsigned char> base58Decode(const String &str) {
    std::vector<unsigned char> result;
    result.reserve(str.length());

    for (char c : str) {
        const char* p = std::strchr(BASE58_ALPHABET, c);
        if (p == nullptr) {
            throw std::runtime_error("Invalid character in Base58 string");
        }
        result.push_back(p - BASE58_ALPHABET);
    }

    std::vector<unsigned char> decoded;
    decoded.reserve((str.length() * 733) / 1000 + 1); // log(58) / log(256), rounded up

    int carry;
    for (unsigned char c : result) {
        carry = c;
        for (auto it = decoded.rbegin(); it != decoded.rend(); ++it) {
            carry += 58 * (*it);
            *it = carry % 256;
            carry /= 256;
        }
        while (carry) {
            decoded.insert(decoded.begin(), carry % 256);
            carry /= 256;
        }
    }

    // Skip leading zeroes in the input
    for (char c : str) {
        if (c != '1') break;
        decoded.insert(decoded.begin(), 0);
    }
    return decoded;
}

bool validateBase58BTCAddress(const String &address) {
    if (address.length() < 26 || address.length() > 35) {
        return false;
    }
    if (!isBase58(address)) {
        return false;
    }
    std::vector<unsigned char> decoded = base58Decode(address);
    LOG_W("decoded size %d \r\n", decoded.size());
    for (auto &byte : decoded) {
        log_i("%02x", byte);
    }
    if (decoded.size() != 25) {
        return false;
    }
    log_i("\r\n");

    unsigned char hash1[SHA256_SIZE];
    
    unsigned char hash2[SHA256_SIZE];

    csha256(decoded.data(), 21, hash1);
    csha256(hash1, SHA256_SIZE, hash2);

    for(int i = 0; i < SHA256_SIZE; i++){
        log_i("%02x", hash1[i]);
    }
    log_i("\r\n");
    for(int i = 0; i < SHA256_SIZE; i++){
        log_i("%02x", hash2[i]);
    }
    log_i("\r\n");

    return std::equal(hash2, hash2 + 4, decoded.end() - 4);
}

uint32_t polymod(const std::vector<uint8_t> &values) {
    uint32_t chk = 1;
    for (uint8_t v : values) {
        uint8_t top = chk >> 25;
        chk = (chk & 0x1ffffff) << 5 ^ v;
        if (top & 1) chk ^= 0x3b6a57b2;
        if (top & 2) chk ^= 0x26508e6d;
        if (top & 4) chk ^= 0x1ea119fa;
        if (top & 8) chk ^= 0x3d4233dd;
        if (top & 16) chk ^= 0x2a1462b3;
    }
    return chk;
}

bool bech32VerifyChecksum(const std::string &hrp, const std::vector<uint8_t> &data) {
    std::vector<uint8_t> values(hrp.size() * 2 + 1 + data.size());
    for (size_t i = 0; i < hrp.size(); ++i) {
        values[i] = hrp[i] >> 5;
    }
    values[hrp.size()] = 0;
    for (size_t i = 0; i < hrp.size(); ++i) {
        values[hrp.size() + 1 + i] = hrp[i] & 31;
    }
    std::copy(data.begin(), data.end(), values.begin() + hrp.size() * 2 + 1);
    return polymod(values) == 1;
}

bool validateBech32BTCAddress(const String &address) {
    if (!address.startsWith("bc1") || address.length() < 42 || address.length() > 62) {
        return false;
    }

    std::string addr = address.c_str();
    std::string hrp = "bc";
    std::vector<uint8_t> data(addr.size() - hrp.size() - 1);
    for (size_t i = hrp.size() + 1; i < addr.size(); ++i) {
        const char *p = strchr(BECH32_ALPHABET, addr[i]);
        if (p == nullptr) {
            return false;
        }
        data[i - hrp.size() - 1] = p - BECH32_ALPHABET;
    }

    return bech32VerifyChecksum(hrp, data);
}

bool validateBTCAddress(const String &address) {
    return validateBase58BTCAddress(address) || validateBech32BTCAddress(address);
}

bool validate_stratum_user(const String &input) {
    int dotIndex = input.indexOf('.');
    if (dotIndex == -1) {
        // if no dot, check if it is a BTC address
        return validateBTCAddress(input);
    } else {
        // if there is a dot, split the input into two parts
        String part1 = input.substring(0, dotIndex);
        String part2 = input.substring(dotIndex + 1);
        if (validateBTCAddress(part1) && (part2.length() > 0)) {
            LOG_W("*part1 %s, part2 %s", part1.c_str(), part2.c_str());
            return true;
        }
        if (part1.length() > 0 && (part2.length() > 0)) {
            LOG_W("+part1 %s, part2 %s", part1.c_str(), part2.c_str());
            return true;
        }
    }
    return false;
}

