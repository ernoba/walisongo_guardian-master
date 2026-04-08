#include "encryption.h"
#include "config.h"
#include <algorithm>

void XORData(std::vector<BYTE>& data) {
    if (data.empty()) return;
    BYTE key = 0x53; // 'S'
    for (size_t i = 0; i < data.size(); i++) {
        data[i] ^= key;
        key = (key + (BYTE)i) % 256; 
    }
}

void FinalEncryption(std::vector<BYTE>& data) {
    if (data.empty()) return;
    std::string key = Config::ENCRYPTION_KEY;
    for (size_t i = 0; i < data.size(); i++) {
        data[i] ^= key[i % key.size()];
    }
}