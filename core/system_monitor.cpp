#include "system_monitor.h"
#include "utils.h"
#include <gdiplus.h>
#include <iostream>

using namespace Gdiplus;

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
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, w, h);
    SelectObject(hdcMem, hBitmap);
    BitBlt(hdcMem, 0, 0, w, h, hdcScreen, 0, 0, SRCCOPY);

    std::vector<BYTE> buffer;

    {
        Bitmap bmp(hBitmap, NULL);
        CLSID encoderClsid;
        GetEncoderClsid(L"image/jpeg", &encoderClsid);

        IStream* pStream = NULL;
        if (CreateStreamOnHGlobal(NULL, TRUE, &pStream) == S_OK) {
            EncoderParameters encoderParameters;
            encoderParameters.Count = 1;
            encoderParameters.Parameter[0].Guid = EncoderQuality;
            encoderParameters.Parameter[0].Type = EncoderParameterValueTypeLong;
            encoderParameters.Parameter[0].NumberOfValues = 1;
            ULONG quality = 100; 
            encoderParameters.Parameter[0].Value = &quality;

            if (bmp.Save(pStream, &encoderClsid, &encoderParameters) == Ok) {
                STATSTG statstg;
                pStream->Stat(&statstg, STATFLAG_NONAME);
                ULONG fileSize = (ULONG)statstg.cbSize.LowPart;
                buffer.resize(fileSize);

                LARGE_INTEGER liBegin = {0};
                pStream->Seek(liBegin, STREAM_SEEK_SET, NULL);
                pStream->Read(&buffer[0], fileSize, NULL);
            }
            pStream->Release();
        }
    }

    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    GdiplusShutdown(gdiplusToken);

    return buffer;
}

std::string SystemMonitor::GetActiveWindowTitle() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return "Unknown System";
    wchar_t title[512];
    if (GetWindowTextW(hwnd, title, 512) > 0) return Ws2S(title);
    return "Desktop/System";
}