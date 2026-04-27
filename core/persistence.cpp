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

// Helper untuk membersihkan COM interface dengan aman
template <typename T>
void SafeRelease(T** ppT) {
    if (*ppT) {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

// Fungsi untuk mengecek apakah program dijalankan sebagai Administrator
bool IsRunAsAdmin() {
    return IsUserAnAdmin() != FALSE;
}

void KillProcess(const std::wstring& filename) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    DWORD currentPid = GetCurrentProcessId(); 

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (filename == pe.szExeFile && pe.th32ProcessID != currentPid) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
                if (hProc) { 
                    TerminateProcess(hProc, 0); 
                    // TINGKATKAN: Tunggu proses benar-benar mati sebelum lanjut
                    WaitForSingleObject(hProc, 3000); 
                    CloseHandle(hProc); 
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
}

void TerminateOldProcess() {
    KillProcess(Config::EXE_NAME);
    // Sleep tetap ada sebagai buffer tambahan
    Sleep(1000); 
}

bool SetupPersistence(std::wstring path) {
    // Inisialisasi pointer ke NULL untuk cleanup yang aman
    ITaskService* pService = NULL;
    ITaskFolder* pRoot = NULL;
    ITaskDefinition* pTask = NULL;
    IPrincipal* pPrincipal = NULL;
    ITaskSettings* pSettings = NULL;
    ITriggerCollection* pTriggers = NULL;
    ITrigger* pLogonTrigger = NULL;
    ITrigger* pDailyTrigger = NULL;
    IDailyTrigger* pDT = NULL;
    IRepetitionPattern* pRep = NULL;
    IActionCollection* pActions = NULL;
    IAction* pAction = NULL;
    IExecAction* pExec = NULL;

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) return false;

    bool success = false;

    hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService);
    if (FAILED(hr)) goto cleanup;

    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) goto cleanup;

    hr = pService->GetFolder(_bstr_t(L"\\"), &pRoot);
    if (FAILED(hr) || !pRoot) goto cleanup;

    // Hapus task lama jika ada (abaikan error jika tidak ada)
    pRoot->DeleteTask(_bstr_t(Config::TASK_NAME.c_str()), 0);

    hr = pService->NewTask(0, &pTask);
    if (FAILED(hr)) goto cleanup;
    
    // Set Privilege Tinggi
    pTask->get_Principal(&pPrincipal);
    if (pPrincipal) pPrincipal->put_RunLevel(TASK_RUNLEVEL_HIGHEST); 

    // Konfigurasi Settings
    pTask->get_Settings(&pSettings);
    if (pSettings) {
        pSettings->put_AllowDemandStart(VARIANT_TRUE);
        pSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
        pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
        pSettings->put_StartWhenAvailable(VARIANT_TRUE); 
        pSettings->put_RestartInterval(_bstr_t(L"PT1M")); 
        pSettings->put_RestartCount(999);
        pSettings->put_Hidden(VARIANT_TRUE);
        // TINGKATKAN: Mencegah Windows menghentikan task setelah 3 hari (default Windows)
        pSettings->put_ExecutionTimeLimit(_bstr_t(L"PT0S")); 
    }

    // Set Triggers
    pTask->get_Triggers(&pTriggers);
    if (pTriggers) {
        pTriggers->Create(TASK_TRIGGER_LOGON, &pLogonTrigger);
        
        pTriggers->Create(TASK_TRIGGER_DAILY, &pDailyTrigger);
        if (pDailyTrigger) {
            pDailyTrigger->QueryInterface(IID_IDailyTrigger, (void**)&pDT);
            if (pDT) {
                // TINGKATKAN: Gunakan waktu sistem saat ini secara dinamis untuk StartBoundary
                SYSTEMTIME st;
                GetSystemTime(&st);
                wchar_t szStart[32];
                swprintf_s(szStart, L"%04d-%02d-%02dT%02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                
                pDT->put_StartBoundary(_bstr_t(szStart));
                pDT->put_DaysInterval(1);
                pDT->get_Repetition(&pRep);
                if (pRep) pRep->put_Interval(_bstr_t(L"PT1M")); 
            }
        }
    }

    // Set Action (Eksekusi File)
    pTask->get_Actions(&pActions);
    if (pActions) {
        pActions->Create(TASK_ACTION_EXEC, &pAction);
        if (pAction) {
            pAction->QueryInterface(IID_IExecAction, (void**)&pExec);
            if (pExec) {
                // Gunakan path murni tanpa tanda kutip untuk put_Path jika argumen dipisah
                pExec->put_Path(_bstr_t(path.c_str())); 
                pExec->put_Arguments(_bstr_t(L"--background"));
            }
        }
    }

    // Registrasi Task
    hr = pRoot->RegisterTaskDefinition(
        _bstr_t(Config::TASK_NAME.c_str()), 
        pTask, 
        TASK_CREATE_OR_UPDATE, 
        _variant_t(), 
        _variant_t(), 
        TASK_LOGON_INTERACTIVE_TOKEN, 
        _variant_t(), 
        NULL
    );

    success = SUCCEEDED(hr);

cleanup:
    // Bebaskan semua interface untuk mencegah memory leak
    SafeRelease(&pRep); SafeRelease(&pDT); SafeRelease(&pDailyTrigger);
    SafeRelease(&pLogonTrigger); SafeRelease(&pTriggers); SafeRelease(&pSettings);
    SafeRelease(&pPrincipal); SafeRelease(&pExec); SafeRelease(&pAction);
    SafeRelease(&pActions); SafeRelease(&pTask); SafeRelease(&pRoot);
    SafeRelease(&pService);
    CoUninitialize();
    return success;
}

void Install() {
    // --- 1. HEADER & IDENTITAS ---
    std::wcout << L"==========================================================\n";
    std::wcout << L"         SISTEM KEAMANAN DIGITAL WALISONGO GUARDIAN       \n";
    std::wcout << L"     Pondok Pesantren Walisongo Karangmalang - Sragen     \n";
    std::wcout << L"==========================================================\n\n";

    // --- VALIDASI HAK AKSES ---
    if (!IsRunAsAdmin()) {
        std::wcerr << L"[ERROR] Aplikasi harus dijalankan sebagai Administrator!\n";
        std::wcerr << L"Silakan klik kanan dan pilih 'Run as Administrator'.\n";
        Sleep(5000);
        return;
    }

    // --- 2. SURAT PERNYATAAN & PERJANJIAN ---
    std::wcout << L"Harap baca dengan seksama surat pernyataan berikut sebelum melanjutkan:\n\n";
    std::wcout << L"[SURAT PERNYATAAN PENGEMBANG]\n";
    std::wcout << L"Pengembang tidak bertanggung jawab atas kerusakan yang ditimbulkan oleh\n"; 
    std::wcout << L"penggunaan perangkat lunak ini.\n\n";

    std::wcout << L"[SURAT PERNYATAAN PENGGUNAAN]\n";
    std::wcout << L"Dengan menginstal perangkat lunak ini, Anda menyetujui:\n";
    std::wcout << L"1. Program ini berfungsi sebagai instrumen monitoring disiplin.\n";
    std::wcout << L"2. Penggunaan akses digital harus sesuai dengan aturan pondok.\n";
    std::wcout << L"3. Segala bentuk pelanggaran akan tercatat secara otomatis di server\n\n";

    // --- 3. PERINGATAN (WARNING) ---
    std::wcout << L"[PERINGATAN KEAMANAN]\n";
    std::wcout << L"(!) Program ini berfungsi sebagai instrumen monitoring disiplin.\n";
    std::wcout << L"(!) Dilarang keras membagikan lisensi kepada pihak luar.\n";
    std::wcout << L"(!) Sistem ini memantau aktivitas jendela dan log pengetikan.\n";
    std::wcout << L"(!) Matikan antivirus jika instalasi terhambat.\n\n";

    // --- 4. KONTAK ADMIN ---
    std::wcout << L"[KONTAK BANTUAN]\n";
    std::wcout << L"Email: ernobaproject@gmail.com\n";
    std::wcout << L"Project: ernobaproject\n";
    std::wcout << L"----------------------------------------------------------\n\n";

    // --- 6. VALIDASI LISENSI ---
    std::wcout << L"MASUKAN KODE LISENSI: "; 
    std::wstring l; std::getline(std::wcin, l);
    if (l != Config::INSTALL_LICENSE) { 
        std::wcerr << L"Error: Lisensi tidak valid!\n"; 
        Sleep(3000); 
        return; 
    }

    time_t now = time(0);
    if (now > Config::EXPIRY_DATE) {
        std::wcerr << L"Error: Installer ini telah kadaluarsa!\n";
        Sleep(3000);
        return;
    }

    // --- 7. INPUT DATA SANTRI ---
    std::wstring n, c;
    std::wcout << L"Nama Lengkap Santri : "; std::getline(std::wcin, n);
    std::wcout << L"Kelas (Angka saja)  : "; std::getline(std::wcin, c);
    
    if (n.empty() || c.empty() || n.length() > 80 || c.length() > 3 || c.find_first_not_of(L"0123456789") != std::wstring::npos) {
        std::wcerr << L"Data tidak valid! Pastikan Nama < 80 karakter dan Kelas berupa angka.\n";
        Sleep(3000);
        return;
    }

    // ambil link dari server terlebih dahulu sebelum memulai proses utama
    std::wcout << L"\n[PROSES] Mendapatkan link server terbaru...\n";
    if (!FetchLatestLink()) {
        std::wcerr << L"Warning: Gagal mendapatkan link server terbaru. Pastikan koneksi internet dalam keadaan mati saat instalasi.\n";
        std::wcerr << L"L" << L"Namun, proses instalasi akan tetap dilanjutkan dengan link yang tersimpan secara lokal (jika ada).\n";
        Sleep(3000);
    }

    // --- LOGIKA INSTALASI INTI ---
    std::wcout << L"\n[PROSES] Menyiapkan enkripsi sistem...\n";
    
    wchar_t myPathRaw[MAX_PATH];
    GetModuleFileNameW(NULL, myPathRaw, MAX_PATH);
    std::filesystem::path myPath(myPathRaw);

    std::filesystem::path targetDir = GetStoragePath(); 
    std::filesystem::path finalExePath = targetDir / Config::EXE_NAME;
    std::error_code ec;

    TerminateOldProcess();

    // Pastikan direktori ada
    if (!std::filesystem::exists(targetDir)) { 
        std::filesystem::create_directories(targetDir, ec); 
    }

    // Bersihkan file lama jika ada
    if (std::filesystem::exists(finalExePath)) {
        SetFileAttributesW(finalExePath.wstring().c_str(), FILE_ATTRIBUTE_NORMAL);
        std::filesystem::remove(finalExePath, ec);
    }

    // Salin file menggunakan WinAPI untuk keandalan lebih tinggi
    if (!CopyFileW(myPath.wstring().c_str(), finalExePath.wstring().c_str(), FALSE)) {
        std::wcerr << L"[ERROR] Gagal menyalin file (Error: " << GetLastError() << L")\n";
        Sleep(5000);
        return;
    }

    // Set Atribut Hidden + System + ReadOnly
    SetFileAttributesW(finalExePath.wstring().c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY);

    // Buat System ID unik
    UUID uuid; 
    UuidCreate(&uuid);
    RPC_WSTR uStr; 
    UuidToStringW(&uuid, &uStr);
    std::wstring wsid((wchar_t*)uStr);
    RpcStringFreeW(&uStr);

    // TINGKATKAN: Simpan ke HKEY_LOCAL_MACHINE karena kita punya akses Admin
    // Ini lebih aman dan berlaku untuk semua user di PC tersebut
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, Config::REG_PATH.c_str(), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"StudentName", 0, REG_SZ, (BYTE*)n.c_str(), (DWORD)(n.length() + 1) * sizeof(wchar_t));
        RegSetValueExW(hKey, L"StudentClass", 0, REG_SZ, (BYTE*)c.c_str(), (DWORD)(c.length() + 1) * sizeof(wchar_t));
        RegSetValueExW(hKey, L"SystemID", 0, REG_SZ, (BYTE*)wsid.c_str(), (DWORD)(wsid.length() + 1) * sizeof(wchar_t));
        RegCloseKey(hKey);
    }

    std::wcout << L"[PROSES] Mendaftarkan ke Windows Task Scheduler...\n";
    if (SetupPersistence(finalExePath.wstring())) {
        ShellExecuteW(NULL, L"open", finalExePath.wstring().c_str(), L"--background", NULL, SW_HIDE);
        std::wcout << L"\n==========================================================\n";
        std::wcout << L"[SUKSES] Guardian telah aktif di latar belakang.\n";
        std::wcout << L"==========================================================\n";
        for (int i = 5; i > 0; i--) {
            std::wcout << L"\rMenutup installer dalam: " << i << L" detik...   " << std::flush;
            Sleep(1000);
        }
        exit(0); 
    } else {
        std::wcerr << L"\n[ERROR FATAL] Gagal mendaftarkan sistem persisten!\n";
        // Cleanup jika gagal
        SetFileAttributesW(finalExePath.wstring().c_str(), FILE_ATTRIBUTE_NORMAL);
        std::filesystem::remove(finalExePath, ec);
        Sleep(7000);
    }
}