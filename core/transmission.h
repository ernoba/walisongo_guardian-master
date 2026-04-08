#ifndef TRANSMISSION_H
#define TRANSMISSION_H

#include <vector>
#include <string>
#include <windows.h>

// Deklarasi dengan parameter default di akhir
bool PostDataChunked(const std::vector<BYTE>& chunk, 
                     std::string type, 
                     std::string fname, 
                     std::string fileID, 
                     int chunkNum, 
                     int chunkTotal, 
                     std::string overrideUrl = ""); // <--- Parameter ke-7

void ProcessOfflineQueue();

#endif