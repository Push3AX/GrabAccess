#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <string>

#define IDC_TITLE       3001
#define IDC_FILES       3004
#define IDC_CMD         3005
#define IDC_ACCOUNTS    3006

enum UiLanguage {
    LANG_ZH = 0,
    LANG_EN = 1
};

static HINSTANCE g_instance = NULL;
static HWND g_main = NULL;
static HWND g_title = NULL;
static HWND g_files = NULL;
static HWND g_cmd = NULL;
static HWND g_accounts = NULL;
static UiLanguage g_language = LANG_EN;
static std::wstring g_rawReason;
static HBRUSH g_backgroundBrush = NULL;

static bool EndsWithSlash(const std::wstring& value) {
    if (value.empty()) return false;
    wchar_t ch = value[value.size() - 1];
    return ch == L'\\' || ch == L'/';
}

static std::wstring AddSlash(const std::wstring& value) {
    if (value.empty() || EndsWithSlash(value)) return value;
    return value + L"\\";
}

static std::wstring System32Path(const wchar_t* fileName) {
    wchar_t systemDir[MAX_PATH];
    if (!GetSystemDirectoryW(systemDir, MAX_PATH)) {
        return fileName;
    }
    return AddSlash(systemDir) + fileName;
}

static std::wstring Trim(std::wstring value) {
    while (!value.empty() && (value[0] == L' ' || value[0] == L'\t' || value[0] == L'\r' || value[0] == L'\n')) {
        value.erase(value.begin());
    }
    while (!value.empty()) {
        wchar_t ch = value[value.size() - 1];
        if (ch != L' ' && ch != L'\t' && ch != L'\r' && ch != L'\n') break;
        value.erase(value.end() - 1);
    }
    return value;
}

static std::wstring ReadTextFileUtf8OrAnsi(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return L"";
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 32768) {
        CloseHandle(file);
        return L"";
    }

    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    BOOL ok = ReadFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &read, NULL);
    CloseHandle(file);
    if (!ok || read == 0) {
        return L"";
    }
    bytes.resize(read);

    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), NULL, 0);
    UINT codePage = CP_UTF8;
    if (needed <= 0) {
        codePage = CP_ACP;
        needed = MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), NULL, 0);
    }
    if (needed <= 0) {
        return L"";
    }

    std::wstring text(needed, L'\0');
    MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), &text[0], needed);
    return Trim(text);
}

static void LoadReason() {
    g_rawReason = ReadTextFileUtf8OrAnsi(System32Path(L"GrabAccessReason.txt"));
    if (g_rawReason.empty()) {
        g_rawReason = L"Reason: unavailable.";
    }
}

static UiLanguage DetectLanguage() {
    LANGID lang = GetUserDefaultUILanguage();
    return PRIMARYLANGID(lang) == LANG_CHINESE ? LANG_ZH : LANG_EN;
}

static std::wstring CleanReasonForTitle() {
    std::wstring reason = g_rawReason;
    const wchar_t* prefix = L"Reason:";
    if (_wcsnicmp(reason.c_str(), prefix, 7) == 0) {
        reason = Trim(reason.substr(7));
    }
    return reason.empty() ? L"unavailable" : reason;
}

static std::wstring PromptText() {
    if (g_language == LANG_ZH) {
        return std::wstring(L"当前登陆方式不可绕过，原因：\r\n") +
               CleanReasonForTitle() +
               L"\r\n但你可以执行以下操作:";
    }

    return std::wstring(L"This sign-in method cannot be bypassed. Reason:\r\n") +
           CleanReasonForTitle() +
           L"\r\nYou can perform the following actions:";
}

static void ShowLastErrorMessage(const wchar_t* action) {
    DWORD error = GetLastError();
    wchar_t* message = NULL;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, error, 0, reinterpret_cast<wchar_t*>(&message), 0, NULL);

    std::wstring text = action;
    text += L" failed.";
    if (message) {
        text += L"\n\n";
        text += message;
        LocalFree(message);
    }
    MessageBoxW(g_main, text.c_str(), L"GrabAccess", MB_ICONERROR | MB_OK);
}

static bool StartProcess(const std::wstring& commandLine,
                         const std::wstring& workingDirectory,
                         WORD showWindow,
                         DWORD creationFlags) {
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = showWindow;

    std::wstring mutableCommand = commandLine;
    BOOL ok = CreateProcessW(NULL,
                             &mutableCommand[0],
                             NULL,
                             NULL,
                             FALSE,
                             creationFlags,
                             NULL,
                             workingDirectory.empty() ? NULL : workingDirectory.c_str(),
                             &startup,
                             &process);
    if (!ok) {
        return false;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

static void LaunchFiles() {
    std::wstring exe = System32Path(L"GrabAccessExplorerHost.exe");
    std::wstring command = L"\"" + exe + L"\"";
    if (!StartProcess(command, L"", SW_SHOWMAXIMIZED, 0)) {
        ShowLastErrorMessage(L"Open file manager");
    }
}

static void LaunchCmd() {
    std::wstring command = L"cmd.exe";
    if (!StartProcess(command, System32Path(L""), SW_SHOWMAXIMIZED, CREATE_NEW_CONSOLE)) {
        ShowLastErrorMessage(L"Open cmd");
    }
}

static void LaunchAccounts() {
    std::wstring exe = System32Path(L"netplwiz.exe");
    std::wstring command = L"\"" + exe + L"\"";
    if (!StartProcess(command, L"", SW_SHOWNORMAL, 0)) {
        ShowLastErrorMessage(L"Open account manager");
    }
}

static void ScheduleCleanupAndSelfDeleteIfInstalled() {
    wchar_t modulePath[MAX_PATH];
    wchar_t systemDir[MAX_PATH];
    if (!GetModuleFileNameW(NULL, modulePath, MAX_PATH) ||
        !GetSystemDirectoryW(systemDir, MAX_PATH)) {
        return;
    }

    std::wstring installedPath = AddSlash(systemDir) + L"GrabAccessFallback.exe";
    if (_wcsicmp(modulePath, installedPath.c_str()) != 0) {
        return;
    }

    std::wstring cleanup = AddSlash(systemDir) + L"GrabAccessCleanup.bat";
    std::wstring command = L"cmd.exe /c ping 127.0.0.1 -n 2 > nul";
    command += L" & if exist \"" + cleanup + L"\" \"" + cleanup + L"\"";
    command += L" & del /f /q \"" + std::wstring(modulePath) + L"\" > nul 2>&1";

    StartProcess(command, L"", SW_HIDE, CREATE_NO_WINDOW);
}

static HMENU ControlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

static HWND CreateLabel(HWND parent, int id, const wchar_t* text, DWORD style) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | style,
                           0, 0, 0, 0, parent, ControlId(id), g_instance, NULL);
}

static HWND CreateButton(HWND parent, int id, const wchar_t* text) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                           0, 0, 0, 0, parent, ControlId(id), g_instance, NULL);
}

static void ApplyFont(HWND hwnd, int points, bool bold) {
    HDC dc = GetDC(hwnd);
    int height = -MulDiv(points, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(hwnd, dc);

    HFONT font = CreateFontW(height, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL,
                             FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                             L"Segoe UI");
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

static void CenterWindow(HWND hwnd) {
    RECT rc;
    RECT work;
    if (!GetWindowRect(hwnd, &rc)) return;
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) return;

    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    int x = work.left + ((work.right - work.left) - width) / 2;
    int y = work.top + ((work.bottom - work.top) - height) / 2;
    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}

static void ShowWindowForeground(HWND hwnd) {
    CenterWindow(hwnd);
    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(hwnd);
    SetActiveWindow(hwnd);
    SetForegroundWindow(hwnd);
    UpdateWindow(hwnd);
}

static void UpdateTexts() {
    if (g_language == LANG_ZH) {
        SetWindowTextW(g_main, L"GrabAccess");
        SetWindowTextW(g_files, L"管理文件");
        SetWindowTextW(g_cmd, L"命令行");
        SetWindowTextW(g_accounts, L"管理账户");
    } else {
        SetWindowTextW(g_main, L"GrabAccess");
        SetWindowTextW(g_files, L"Manage Files");
        SetWindowTextW(g_cmd, L"Command Prompt");
        SetWindowTextW(g_accounts, L"Manage Accounts");
    }

    std::wstring prompt = PromptText();
    SetWindowTextW(g_title, prompt.c_str());
}

static void LayoutControls(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    int margin = 22;
    int top = 18;

    MoveWindow(g_title, margin, top, width - margin * 2, 78, TRUE);
    top += 94;

    int buttonWidth = (width - margin * 2 - 24) / 3;
    int buttonHeight = 36;
    MoveWindow(g_files, margin, top, buttonWidth, buttonHeight, TRUE);
    MoveWindow(g_cmd, margin + buttonWidth + 12, top, buttonWidth, buttonHeight, TRUE);
    MoveWindow(g_accounts, margin + (buttonWidth + 12) * 2, top, buttonWidth, buttonHeight, TRUE);
}

static void CreateControls(HWND hwnd) {
    g_title = CreateLabel(hwnd, IDC_TITLE, L"", SS_LEFT);
    g_files = CreateButton(hwnd, IDC_FILES, L"");
    g_cmd = CreateButton(hwnd, IDC_CMD, L"");
    g_accounts = CreateButton(hwnd, IDC_ACCOUNTS, L"");

    ApplyFont(g_title, 12, true);
    ApplyFont(g_files, 11, true);
    ApplyFont(g_cmd, 11, true);
    ApplyFont(g_accounts, 11, true);
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_main = hwnd;
        g_language = DetectLanguage();
        LoadReason();
        CreateControls(hwnd);
        UpdateTexts();
        LayoutControls(hwnd);
        return 0;
    case WM_SIZE:
        LayoutControls(hwnd);
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(32, 33, 36));
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(g_backgroundBrush);
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_FILES:
            LaunchFiles();
            return 0;
        case IDC_CMD:
            LaunchCmd();
            return 0;
        case IDC_ACCOUNTS:
            LaunchAccounts();
            return 0;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        ScheduleCleanupAndSelfDeleteIfInstalled();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    g_instance = instance;
    g_backgroundBrush = CreateSolidBrush(RGB(245, 246, 248));

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = g_backgroundBrush;
    wc.lpszClassName = L"GrabAccessFallbackWindow";
    if (!RegisterClassExW(&wc)) {
        return 1;
    }

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW,
                                wc.lpszClassName,
                                L"GrabAccess",
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                620, 220,
                                NULL, NULL, instance, NULL);
    if (!hwnd) {
        return 1;
    }

    ShowWindowForeground(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_backgroundBrush) DeleteObject(g_backgroundBrush);
    return static_cast<int>(msg.wParam);
}
