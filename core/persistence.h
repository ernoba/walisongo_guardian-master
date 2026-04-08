#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include <windows.h>
#include <string>

// Persistence Functions
bool SetupPersistence(std::wstring path);
void Install();

#endif // PERSISTENCE_H