#include <Arduino.h>
#include <vector>
#include "logger.h"
#include "addr.h"
#include "csha256.h"

#define SHA256_SIZE 32
static const String BASE58_ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
static const char *BECH32_ALPHABET  = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

bool isBase58(const String &str) {
    for (char c : str) {
        if (BASE58_ALPHABET.indexOf(c) == -1) {
            return false;
        }
    }
    return true;
}

std::vector<unsigned char> base58Decode(const String &str) {
    std::vector<unsigned char> result; 
    for (char c : str) {
        int carry = BASE58_ALPHABET.indexOf(c);
        for (auto &byte : result) {
            carry += 58 * byte;
            byte = carry % 256;
            carry /= 256;
        }
        while (carry) {
            result.insert(result.begin(), carry % 256);
            carry /= 256;
        }
    }
    auto it = result.begin();
    while (it != result.end() && *it == 0) {
        it = result.erase(it);
    }
    return result;
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
    // if (decoded.size() != 25) {
    //     return false;
    // }
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

bool validateInput(const String &input) {
    int dotIndex = input.indexOf('.');
    if (dotIndex == -1) {
        // if no dot, check if it is a BTC address
        return validateBTCAddress(input);
    } else {
        // if there is a dot, split the input into two parts
        String part1 = input.substring(0, dotIndex);
        String part2 = input.substring(dotIndex + 1);
        if (validateBTCAddress(part1) && part2.length() > 0) {
            return true;
        }
        if (part1.length() > 0 && part2.length() > 0) {
            return true;
        }
    }
    return false;
}

void btc_addr_test() {
    String input1 = "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1.001";
    String input2 = "1wSzR8Z6Hwrub6q8WxcbHWzTHLbMT126F";
    String input3 = "username.worker01";
    String input4 = "bc1q2wyhh3msmyv2pgrahp665lfyy7z7vwd2mvvys4";

    // if (validateInput(input1)) {
    //     LOG_I("Input 1 is valid");
    // } else {
    //     LOG_E("Input 1 is invalid");
    // }

    if (validateInput(input2)) {
        LOG_I("Input 2 is valid");
    } else {
        LOG_E("Input 2 is invalid");
    }

    // if (validateInput(input3)) {
    //     LOG_I("Input 3 is valid");
    // } else {
    //     LOG_E("Input 3 is invalid");
    // }

    // if (validateInput(input4)) {
    //     LOG_I("Input 4 is valid");
    // } else {
    //     LOG_E("Input 4 is invalid");
    // }
}

