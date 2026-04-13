#include "system_monitor.h"
#include "utils.h"
#include <gdiplus.h>
#include <iostream>
#include <vector>

using namespace Gdiplus;

// Fungsi internal untuk mencari encoder JPEG
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;

    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL) return -1;

    GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    free(pImageCodecInfo);
    return -1;
}

std::vector<BYTE> SystemMonitor::TakeScreenshot() {
    // Mengambil resolusi seluruh layar (Mendukung Multi-Monitor)
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, w, h);
    HGDIOBJ hOld = SelectObject(hdcMem, hBitmap);
    
    // Proses penyalinan gambar dari layar ke memori
    BitBlt(hdcMem, 0, 0, w, h, hdcScreen, x, y, SRCCOPY);

    std::vector<BYTE> buffer;

    {
        // Scope GDI+ Object
        Bitmap bmp(hBitmap, NULL);
        CLSID encoderClsid;
        
        if (GetEncoderClsid(L"image/jpeg", &encoderClsid) != -1) {
            IStream* pStream = NULL;
            if (CreateStreamOnHGlobal(NULL, TRUE, &pStream) == S_OK) {
                EncoderParameters encoderParameters;
                encoderParameters.Count = 1;
                encoderParameters.Parameter[0].Guid = EncoderQuality;
                encoderParameters.Parameter[0].Type = EncoderParameterValueTypeLong;
                encoderParameters.Parameter[0].NumberOfValues = 1;
                
                // Kualitas 60: Keseimbangan terbaik antara detail dan ukuran file
                ULONG quality = 60; 
                encoderParameters.Parameter[0].Value = &quality;

                if (bmp.Save(pStream, &encoderClsid, &encoderParameters) == Ok) {
                    STATSTG statstg;
                    pStream->Stat(&statstg, STATFLAG_NONAME);
                    ULONG fileSize = (ULONG)statstg.cbSize.LowPart;
                    
                    buffer.resize(fileSize);
                    LARGE_INTEGER liBegin = {0};
                    pStream->Seek(liBegin, STREAM_SEEK_SET, NULL);
                    pStream->Read(buffer.data(), fileSize, NULL);
                }
                pStream->Release();
            }
        }
    }

    // Pembersihan Resource GDI
    SelectObject(hdcMem, hOld);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    return buffer;
}

std::string SystemMonitor::GetActiveWindowTitle() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return "System/Desktop";
    wchar_t title[512];
    if (GetWindowTextW(hwnd, title, 512) > 0) return Ws2S(title);
    return "Desktop/System";
}