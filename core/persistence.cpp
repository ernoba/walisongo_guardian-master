#include "persistence.h"
#include "config.h"
#include "utils.h"
#include "globals.h"
#include <taskschd.h>
#include <comdef.h>
#include <rpc.h>
#include <shlobj.h>
#include <iostream>
#include <vector>
#include <filesystem>
#include <tlhelp32.h>
#include <processthreadsapi.h>

void KillProcess(const std::wstring& filename) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    
    // Dapatkan Process ID (PID) dari program installer ini
    DWORD currentPid = GetCurrentProcessId(); 

    if (Process32FirstW(hSnap, &pe)) {
        do {
            // JANGAN bunuh jika PID-nya sama dengan PID program ini sendiri
            if (filename == pe.szExeFile && pe.th32ProcessID != currentPid) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProc) { 
                    TerminateProcess(hProc, 0); 
                    CloseHandle(hProc); 
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    Sleep(1000); 
}

void TerminateOldProcess() {
    KillProcess(Config::EXE_NAME);
    // std::wstring cmd = L"taskkill /F /IM " + Config::EXE_NAME + L" /T >nul 2>&1";
    // _wsystem(cmd.c_str());
    Sleep(1000); 
}

bool SetupPersistence(std::wstring path) {
    std::wstring quotedPath = L"\"" + path + L"\"";
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) return false;

    ITaskService* pService = NULL;
    hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService);
    if (FAILED(hr)) { CoUninitialize(); return false; }

    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) { pService->Release(); CoUninitialize(); return false; }

    ITaskFolder* pRoot = NULL;
    pService->GetFolder(_bstr_t(L"\\"), &pRoot);
    if (!pRoot) { pService->Release(); CoUninitialize(); return false; }

    pRoot->DeleteTask(_bstr_t(Config::TASK_NAME.c_str()), 0);
    ITaskDefinition* pTask = NULL;
    pService->NewTask(0, &pTask);
    
    IPrincipal* pPrincipal = NULL;
    pTask->get_Principal(&pPrincipal);
    pPrincipal->put_RunLevel(TASK_RUNLEVEL_HIGHEST); 

    ITaskSettings* pSettings = NULL;
    pTask->get_Settings(&pSettings);
    if (pSettings) {
        pSettings->put_AllowDemandStart(VARIANT_TRUE);
        pSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
        pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
        pSettings->put_StartWhenAvailable(VARIANT_TRUE); // Zombie Mode: Langsung jalan jika tertunda
        pSettings->put_RestartInterval(_bstr_t(L"PT1M")); // Coba restart tiap 1 menit jika gagal
        pSettings->put_RestartCount(999);
        pSettings->put_Hidden(VARIANT_TRUE);
        pSettings->Release();
    }

    ITriggerCollection* pTriggers = NULL;
    pTask->get_Triggers(&pTriggers);
    ITrigger* pLogonTrigger = NULL;
    pTriggers->Create(TASK_TRIGGER_LOGON, &pLogonTrigger);

    ITrigger* pDailyTrigger = NULL;
    pTriggers->Create(TASK_TRIGGER_DAILY, &pDailyTrigger);
    IDailyTrigger* pDT = NULL;
    pDailyTrigger->QueryInterface(IID_IDailyTrigger, (void**)&pDT);
    pDT->put_StartBoundary(_bstr_t(L"2026-01-01T00:00:00"));
    pDT->put_DaysInterval(1);
    
    IRepetitionPattern* pRep = NULL;
    pDailyTrigger->get_Repetition(&pRep);
    pRep->put_Interval(_bstr_t(L"PT1M")); 

    IActionCollection* pActions = NULL;
    pTask->get_Actions(&pActions);
    IAction* pAction = NULL;
    pActions->Create(TASK_ACTION_EXEC, &pAction);
    IExecAction* pExec = NULL;
    pAction->QueryInterface(IID_IExecAction, (void**)&pExec);
    pExec->put_Path(_bstr_t(quotedPath.c_str())); 
    pExec->put_Arguments(_bstr_t(L"--background"));

    hr = pRoot->RegisterTaskDefinition(_bstr_t(Config::TASK_NAME.c_str()), pTask, 
                                      TASK_CREATE_OR_UPDATE, _variant_t(), _variant_t(), 
                                      TASK_LOGON_INTERACTIVE_TOKEN, _variant_t(), NULL);
    
    if(pRep) pRep->Release(); if(pDT) pDT->Release(); if(pExec) pExec->Release();
    if(pAction) pAction->Release(); if(pActions) pActions->Release();
    if(pTask) pTask->Release(); if(pRoot) pRoot->Release();
    if(pService) pService->Release();
    CoUninitialize();
    return SUCCEEDED(hr);
}

void Install() {
    // --- 1. HEADER & IDENTITAS ---
    std::wcout << L"==========================================================\n";
    std::wcout << L"         SISTEM KEAMANAN DIGITAL WALISONGO GUARDIAN       \n";
    std::wcout << L"     Pondok Pesantren Walisongo Karangmalang - Sragen     \n";
    std::wcout << L"==========================================================\n\n";

    // --- 2. SURAT PERNYATAAN & PERJANJIAN ---
    std::wcout << L"[SURAT PERNYATAAN PENGGUNAAN]\n";
    std::wcout << L"Dengan menginstal perangkat lunak ini, Anda menyetujui:\n";
    std::wcout << L"1. Program ini berfungsi sebagai instrumen monitoring disiplin.\n";
    std::wcout << L"2. Penggunaan aset digital harus sesuai dengan aturan pondok.\n";
    std::wcout << L"3. Segala bentuk modifikasi atau upaya penghapusan ilegal akan\n";
    std::wcout << L"   tercatat secara otomatis di server pusat.\n\n";

    // --- 3. PERINGATAN (WARNING) ---
    std::wcout << L"[PERINGATAN KEAMANAN]\n";
    std::wcout << L"(!) Dilarang keras membagikan lisensi kepada pihak luar.\n";
    std::wcout << L"(!) Sistem ini memantau aktivitas jendela dan log pengetikan.\n";
    std::wcout << L"(!) Pastikan koneksi internet dalam keadaan mati sebelum memulai proses\n";
    std::wcout << L"    instalasi ini. Ini dilakukan untuk menghindari antivirus\n\n";

    // --- 4. KONTAK ADMIN ---
    std::wcout << L"[KONTAK BANTUAN]\n";
    std::wcout << L"Jika terjadi kendala teknis dalam penginstaln hubungi team pengembang:\n";
    std::wcout << L"Email: ernobaproject@gmail.com\n";
    std::wcout << L"Project: ernobaproject\n";
    std::wcout << L"----------------------------------------------------------\n\n";

    // --- 6. VALIDASI LISENSI ---
    std::wcout << L"\nMASUKAN KODE LISENSI: "; 
    std::wstring l; std::getline(std::wcin, l);
    if (l != Config::INSTALL_LICENSE) { 
        std::wcerr << L"Error: Lisensi tidak valid atau telah kadaluarsa!\n"; 
        Sleep(3000); 
        return; 
    }

    time_t now = time(0);

    if (now > Config::EXPIRY_DATE) {
        std::wcerr << L"Error: Masa berlaku installer ini telah habis (Kadaluarsa)!\n";
        std::wcerr << L"Silakan hubungi admin untuk mendapatkan versi terbaru.\n";
        Sleep(3000);
        return;
    }

    // --- 7. INPUT DATA SANTRI ---
    std::wstring n, c;
    std::wcout << L"Nama Lengkap Santri : "; std::getline(std::wcin, n);
    std::wcout << L"Kelas : "; std::getline(std::wcin, c);
    if (n.empty() || c.empty()) {
        std::wcerr << L"Data tidak boleh kosong!\n";
        Sleep(3000);
        return;
    }

    // --- LOGIKA INSTALASI INTI ---
    std::wcout << L"\n[PROSES] Menyiapkan enkripsi sistem...\n";
    
    wchar_t myPathRaw[MAX_PATH];
    GetModuleFileNameW(NULL, myPathRaw, MAX_PATH);
    std::filesystem::path myPath(myPathRaw);

    std::vector<std::filesystem::path> fallbackPaths;
    fallbackPaths.push_back(GetStoragePath()); 

    std::filesystem::path finalExePath = L"";
    bool copySuccess = false;
    std::error_code ec;

    TerminateOldProcess();

    for (const auto& targetDir : fallbackPaths) {
        std::error_code ec;
        // 1. Pastikan direktori dibuat
        if (!std::filesystem::exists(targetDir)) { 
            std::filesystem::create_directories(targetDir, ec); 
        }

        std::filesystem::path targetExe = targetDir / Config::EXE_NAME;

        // 2. Bersihkan file lama jika ada
        if (std::filesystem::exists(targetExe)) {
            SetFileAttributesW(targetExe.wstring().c_str(), FILE_ATTRIBUTE_NORMAL);
            std::filesystem::remove(targetExe, ec);
        }

        // 3. Gunakan CopyFileW (WinAPI) alih-alih std::filesystem untuk reliabilitas
        if (CopyFileW(myPath.wstring().c_str(), targetExe.wstring().c_str(), FALSE)) {
            // Verifikasi apakah file benar-benar ada dan tidak 0-byte
            if (std::filesystem::exists(targetExe) && std::filesystem::file_size(targetExe) > 0) {
                finalExePath = targetExe;
                copySuccess = true;
                break; 
            }
        } else {
            // Debug: Ambil kode error jika gagal
            DWORD error = GetLastError();
            std::wcerr << L"Gagal menyalin ke " << targetDir.wstring() << L" (Error: " << error << L")" << std::endl;
        }
    }

    if (!copySuccess) {
        std::wcerr << L"\n[ERROR] Gagal menyalin file sistem ke lokasi mana pun.\n";
        std::wcerr << L"Penyebab umum: Antivirus memblokir atau kurang hak Administrator.\n";
        Sleep(5000); // Beri waktu lebih lama agar user bisa baca error
        return;
    }

    // 4. Set atribut Stealth (Hidden + System + ReadOnly)
    SetFileAttributesW(finalExePath.wstring().c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY);

    // Buat System ID unik
    UUID uuid; UuidCreate(&uuid);
    unsigned char* uStr; UuidToStringA(&uuid, &uStr);
    std::string sid = (char*)uStr; RpcStringFreeA(&uStr);
    std::wstring wsid(sid.begin(), sid.end());

    // Simpan ke Registry
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, Config::REG_PATH.c_str(), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"StudentName", 0, REG_SZ, (BYTE*)n.c_str(), (DWORD)(n.length() + 1) * 2);
        RegSetValueExW(hKey, L"StudentClass", 0, REG_SZ, (BYTE*)c.c_str(), (DWORD)(c.length() + 1) * 2);
        RegSetValueExW(hKey, L"SystemID", 0, REG_SZ, (BYTE*)wsid.c_str(), (DWORD)(wsid.length() + 1) * 2);
        RegCloseKey(hKey);
    }

    // Aktifkan Persistence & Cleanup
    std::wcout << L"[PROSES] Mendaftarkan ke Windows Task Scheduler...\n";
        
        if (SetupPersistence(finalExePath.wstring())) {
            ShellExecuteW(NULL, L"open", finalExePath.wstring().c_str(), L"--background", NULL, SW_HIDE);
            std::wcout << L"\n==========================================================\n";
            std::wcout << L"[SUKSES] Guardian telah aktif di latar belakang.\n";
            std::wcout << L"Sistem akan memantau keamanan perangkat secara otomatis.\n";
            std::wcout << L"==========================================================\n";
            for (int i = 5; i > 0; i--) {
                std::wcout << L"\rMenutup installer dalam: " << i << L" detik...   " << std::flush;
                Sleep(1000);
            }
            exit(0); 
        } else {
            // TAMBAHKAN BLOK ELSE INI
            std::wcerr << L"\n[ERROR FATAL] Gagal membuat Task di Windows Task Scheduler!\n";
            std::wcerr << L"Penyebab utama: Program tidak dijalankan sebagai Administrator.\n";
            std::wcerr << L"Solusi: Klik kanan pada file installer, lalu pilih 'Run as Administrator'.\n";
            
            // Hapus file yang terlanjur di-copy agar bersih
            SetFileAttributesW(finalExePath.wstring().c_str(), FILE_ATTRIBUTE_NORMAL);
            std::filesystem::remove(finalExePath);
            
            Sleep(7000); // Beri waktu 7 detik agar kamu bisa membaca errornya
        }
    }