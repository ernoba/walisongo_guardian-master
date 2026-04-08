#include "network_gate.h"
#include "config.h"
#include "utils.h"
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <fstream>
#include <filesystem>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")

std::string FetchExternalLink(const std::string& unused) {
    DWORD dwSize = 0, dwDownloaded = 0;
    LPSTR pszOutBuffer;
    std::string result = "";
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;

    hSession = WinHttpOpen(L"WalisongoGuardian/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession) {
        hConnect = WinHttpConnect(hSession, Config::GATEWAY_HOST.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    }
    if (hConnect) {
        hRequest = WinHttpOpenRequest(hConnect, L"GET", Config::GATEWAY_PATH.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    }

    if (hRequest) {
        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            WinHttpReceiveResponse(hRequest, NULL);
            do {
                dwSize = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                pszOutBuffer = new char[dwSize + 1];
                ZeroMemory(pszOutBuffer, dwSize + 1);
                if (WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) {
                    result += pszOutBuffer;
                }
                delete[] pszOutBuffer;
            } while (dwSize > 0);
        }
    }

    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    return result;
}

bool RefreshDynamicLink() {
    std::filesystem::path linkFile = GetCache("link") / "link.txt"; //
    std::string result = FetchExternalLink(""); 

    if (!result.empty()) {
        // Pembersihan string dari karakter sampah Google Sheets
        result.erase(result.find_last_not_of(" \n\r\t") + 1);
        result.erase(0, result.find_first_not_of(" \n\r\t"));

        if (result.find("http") == 0) {
            Config::DYNAMIC_SERVER_URL = result;
            // Gunakan trunc untuk mengganti isi file secara total
            std::ofstream ofs(linkFile, std::ios::trunc);
            if (ofs.is_open()) { 
                ofs << result; 
                ofs.flush();
                ofs.close(); 
                return true;
            }
        }
    }
    return false;
}