#ifndef ENCRYPTION_H
#define ENCRYPTION_H

#include <windows.h>
#include <vector>
#include <string>

// Encryption Functions
void XORData(std::vector<BYTE>& data);
void FinalEncryption(std::vector<BYTE>& data);

#endif // ENCRYPTION_H