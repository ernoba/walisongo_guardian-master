#ifndef NETWORK_GATE_H
#define NETWORK_GATE_H

#include <string>

// Mendapatkan link mentah dari internet
std::string FetchExternalLink(const std::string& unused);

// Logika cerdas untuk memperbarui link hanya saat dibutuhkan
bool RefreshDynamicLink();

#endif