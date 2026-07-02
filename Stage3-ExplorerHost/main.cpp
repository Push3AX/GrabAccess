#include <windows.h>
#include <commctrl.h>
#include <docobj.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Uuid.lib")

#define IDC_PATH           2001
#define IDC_GO             2002
#define IDC_UP             2003
#define IDC_REFRESH        2004
#define IDC_CMD            2005
#define IDC_STATUS         2006

#define IDM_EDIT_CUT       2101
#define IDM_EDIT_COPY      2102
#define IDM_EDIT_PASTE     2103
#define IDM_EDIT_SELECTALL 2104
#define IDM_EDIT_DELETE    2105

enum UiLanguage {
    LANG_ZH = 0,
    LANG_EN = 1
};

static HINSTANCE g_instance = NULL;
static HWND g_main = NULL;
static HWND g_pathEdit = NULL;
static HWND g_status = NULL;
static IExplorerBrowser* g_browser = NULL;
static IShellView* g_shellView = NULL;
static DWORD g_adviseCookie = 0;
static std::wstring g_currentPath;
static std::vector<std::wstring> g_localClipboardPaths;
static UiLanguage g_language = LANG_EN;

static bool NavigateToPath(const std::wstring& rawPath);

static bool EndsWithSlash(const std::wstring& value) {
    if (value.empty()) return false;
    wchar_t ch = value[value.size() - 1];
    return ch == L'\\' || ch == L'/';
}

static std::wstring AddSlash(const std::wstring& value) {
    if (value.empty() || EndsWithSlash(value)) return value;
    return value + L"\\";
}

static UiLanguage DetectLanguage() {
    LANGID lang = GetUserDefaultUILanguage();
    return PRIMARYLANGID(lang) == LANG_CHINESE ? LANG_ZH : LANG_EN;
}

static const wchar_t* TextZhEn(const wchar_t* zh, const wchar_t* en) {
    return g_language == LANG_ZH ? zh : en;
}

static const wchar_t* ThisPcPath() {
    return L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}";
}

static std::wstring ParentPath(const std::wstring& path) {
    std::wstring value = path;
    while (value.size() > 3 && EndsWithSlash(value)) {
        value.erase(value.end() - 1);
    }

    size_t slash = value.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return value;
    if (slash <= 2) return value.substr(0, slash + 1);
    return value.substr(0, slash);
}

static std::wstring TrimPathInput(std::wstring value) {
    while (!value.empty() && (value[0] == L' ' || value[0] == L'\t')) {
        value.erase(value.begin());
    }
    while (!value.empty() && (value[value.size() - 1] == L' ' || value[value.size() - 1] == L'\t')) {
        value.erase(value.end() - 1);
    }
    if (value.size() >= 2 && value[0] == L'"' && value[value.size() - 1] == L'"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

static bool IsDriveRootPath(const std::wstring& rawPath) {
    std::wstring path = TrimPathInput(rawPath);
    while (path.size() > 3 && EndsWithSlash(path)) {
        path.erase(path.end() - 1);
    }
    return path.size() == 3 &&
           path[1] == L':' &&
           (path[2] == L'\\' || path[2] == L'/');
}

static bool IsThisPcPath(const std::wstring& rawPath) {
    std::wstring path = TrimPathInput(rawPath);
    return _wcsicmp(path.c_str(), ThisPcPath()) == 0;
}

static void SetStatus(const wchar_t* text) {
    if (g_status) {
        SetWindowTextW(g_status, text ? text : L"");
    }
}

static std::wstring ShortcutOnlyHint() {
    return TextZhEn(L"提示：复制/粘贴请使用 Ctrl+C / Ctrl+V，右键菜单不可用。",
                    L"Tip: use Ctrl+C / Ctrl+V for copy/paste; the right-click menu is unavailable.");
}

static void SetStatusPath(const std::wstring& path) {
    std::wstring text = ShortcutOnlyHint();
    if (!path.empty()) {
        text += L" | ";
        text += path;
    }
    SetStatus(text.c_str());
}

static void ShowHresultError(HWND owner, const wchar_t* action, HRESULT hr) {
    wchar_t message[512];
    wsprintfW(message, L"%s failed.\n\nHRESULT: 0x%08lX", action, static_cast<unsigned long>(hr));
    MessageBoxW(owner, message, L"GrabAccess Explorer Host", MB_ICONERROR | MB_OK);
}

static void ShowLastError(HWND owner, const wchar_t* action) {
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
    MessageBoxW(owner, text.c_str(), L"GrabAccess Explorer Host", MB_ICONERROR | MB_OK);
}

static std::wstring GetEditText(HWND edit) {
    int length = GetWindowTextLengthW(edit);
    std::wstring value(length + 1, L'\0');
    GetWindowTextW(edit, &value[0], length + 1);
    value.resize(length);
    return value;
}

static HMENU CreateMainMenu() {
    HMENU menu = CreateMenu();
    HMENU edit = CreatePopupMenu();
    if (!menu || !edit) {
        return menu;
    }

    AppendMenuW(edit, MF_STRING, IDM_EDIT_CUT, TextZhEn(L"剪切(&T)", L"Cu&t"));
    AppendMenuW(edit, MF_STRING, IDM_EDIT_COPY, TextZhEn(L"复制(&C)", L"&Copy"));
    AppendMenuW(edit, MF_STRING, IDM_EDIT_PASTE, TextZhEn(L"粘贴(&P)", L"&Paste"));
    AppendMenuW(edit, MF_SEPARATOR, 0, NULL);
    AppendMenuW(edit, MF_STRING, IDM_EDIT_SELECTALL, TextZhEn(L"全选(&A)", L"Select &All"));
    AppendMenuW(edit, MF_STRING, IDM_EDIT_DELETE, TextZhEn(L"删除(&D)", L"&Delete"));
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), TextZhEn(L"编辑(&E)", L"&Edit"));
    return menu;
}

static bool PathEditHasFocus() {
    HWND focus = GetFocus();
    return focus == g_pathEdit || (g_pathEdit && IsChild(g_pathEdit, focus));
}

static bool TryExecCommandTarget(IOleCommandTarget* target, const GUID* group, ULONG commandId) {
    if (!target) return false;

    OLECMD command;
    ZeroMemory(&command, sizeof(command));
    command.cmdID = commandId;

    HRESULT status = target->QueryStatus(group, 1, &command, NULL);
    if (SUCCEEDED(status) && !(command.cmdf & OLECMDF_ENABLED)) {
        return false;
    }

    HRESULT hr = target->Exec(group, commandId, OLECMDEXECOPT_DODEFAULT, NULL, NULL);
    return SUCCEEDED(hr);
}

static bool ExecuteNativeShellCommand(ULONG commandId) {
    IOleCommandTarget* target = NULL;
    if (g_shellView && SUCCEEDED(g_shellView->QueryInterface(IID_PPV_ARGS(&target))) && target) {
        const GUID* groups[] = { NULL, &CGID_DefView, &CGID_Explorer, &CGID_ShellDocView };
        for (size_t i = 0; i < sizeof(groups) / sizeof(groups[0]); ++i) {
            if (TryExecCommandTarget(target, groups[i], commandId)) {
                target->Release();
                return true;
            }
        }
        target->Release();
    }

    if (g_browser && SUCCEEDED(g_browser->GetCurrentView(IID_PPV_ARGS(&target))) && target) {
        const GUID* groups[] = { NULL, &CGID_DefView, &CGID_Explorer, &CGID_ShellDocView };
        for (size_t i = 0; i < sizeof(groups) / sizeof(groups[0]); ++i) {
            if (TryExecCommandTarget(target, groups[i], commandId)) {
                target->Release();
                return true;
            }
        }
        target->Release();
    }

    return false;
}

static void NotifyDirectoryUpdated(const std::wstring& path) {
    if (path.empty()) {
        return;
    }
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return;
    }

    SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, path.c_str(), NULL);
}

static bool RefreshCurrentView() {
    NotifyDirectoryUpdated(g_currentPath);

    if (g_shellView && SUCCEEDED(g_shellView->Refresh())) {
        return true;
    }
    if (ExecuteNativeShellCommand(OLECMDID_REFRESH)) {
        return true;
    }
    if (!g_currentPath.empty()) {
        return NavigateToPath(g_currentPath);
    }
    return false;
}

static bool ExecuteEditControlCommand(UINT message) {
    if (!PathEditHasFocus()) {
        return false;
    }
    SendMessageW(g_pathEdit, message, 0, 0);
    return true;
}

static bool CurrentFolderPath(std::wstring* path) {
    if (!path) return false;
    if (g_currentPath.empty()) return false;

    DWORD attrs = GetFileAttributesW(g_currentPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;
    }

    *path = g_currentPath;
    return true;
}

static std::wstring BuildDoubleNullList(const std::vector<std::wstring>& paths) {
    std::wstring value;
    for (size_t i = 0; i < paths.size(); ++i) {
        value.append(paths[i]);
        value.push_back(L'\0');
    }
    value.push_back(L'\0');
    return value;
}

static std::wstring BuildDoubleNullPath(const std::wstring& path) {
    std::wstring value = path;
    value.push_back(L'\0');
    value.push_back(L'\0');
    return value;
}

static std::wstring TrimTrailingSlash(std::wstring value) {
    while (value.size() > 3 && EndsWithSlash(value)) {
        value.erase(value.end() - 1);
    }
    return value;
}

static std::wstring BaseName(const std::wstring& path) {
    std::wstring value = TrimTrailingSlash(path);
    size_t slash = value.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return value;
    }
    return value.substr(slash + 1);
}

static std::wstring JoinPath(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    if (EndsWithSlash(dir)) return dir + name;
    return dir + L"\\" + name;
}

static bool PathExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static bool IsSameOrChildPath(const std::wstring& childPath, const std::wstring& parentPath) {
    std::wstring child = TrimTrailingSlash(childPath);
    std::wstring parent = TrimTrailingSlash(parentPath);
    if (child.empty() || parent.empty()) return false;
    if (_wcsicmp(child.c_str(), parent.c_str()) == 0) return true;
    if (child.size() <= parent.size()) return false;
    if (_wcsnicmp(child.c_str(), parent.c_str(), parent.size()) != 0) return false;

    wchar_t separator = child[parent.size()];
    return separator == L'\\' || separator == L'/';
}

static std::wstring UniqueDestinationPath(const std::wstring& directory, const std::wstring& originalName) {
    std::wstring name = originalName.empty() ? L"Item" : originalName;
    std::wstring candidate = JoinPath(directory, name);
    if (!PathExists(candidate)) {
        return candidate;
    }

    size_t dot = name.find_last_of(L'.');
    std::wstring stem = name;
    std::wstring ext;
    if (dot != std::wstring::npos && dot != 0) {
        stem = name.substr(0, dot);
        ext = name.substr(dot);
    }

    candidate = JoinPath(directory, stem + L" - Copy" + ext);
    if (!PathExists(candidate)) {
        return candidate;
    }

    for (int i = 2; i < 1000; ++i) {
        wchar_t suffix[32];
        wsprintfW(suffix, L" - Copy (%d)", i);
        candidate = JoinPath(directory, stem + suffix + ext);
        if (!PathExists(candidate)) {
            return candidate;
        }
    }

    return JoinPath(directory, stem + L" - Copy" + ext);
}

static bool CopyPathRecursive(const std::wstring& source, const std::wstring& destination) {
    DWORD attrs = GetFileAttributesW(source.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return CopyFileW(source.c_str(), destination.c_str(), FALSE) != FALSE;
    }

    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
        return false;
    }

    if (!CreateDirectoryW(destination.c_str(), NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            return false;
        }
    }

    std::wstring search = JoinPath(source, L"*");
    WIN32_FIND_DATAW findData;
    HANDLE find = FindFirstFileW(search.c_str(), &findData);
    if (find == INVALID_HANDLE_VALUE) {
        SetFileAttributesW(destination.c_str(), attrs);
        return true;
    }

    bool ok = true;
    do {
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) {
            continue;
        }

        std::wstring childSource = JoinPath(source, findData.cFileName);
        std::wstring childDestination = JoinPath(destination, findData.cFileName);
        if (!CopyPathRecursive(childSource, childDestination)) {
            ok = false;
        }
    } while (FindNextFileW(find, &findData));

    FindClose(find);
    SetFileAttributesW(destination.c_str(), attrs);
    return ok;
}

static bool CopyFilesystemPathsToFolder(const std::vector<std::wstring>& sources,
                                        const std::wstring& destination) {
    if (sources.empty() || destination.empty()) {
        return false;
    }

    int copied = 0;
    int failed = 0;
    for (size_t i = 0; i < sources.size(); ++i) {
        std::wstring name = BaseName(sources[i]);
        std::wstring target = UniqueDestinationPath(destination, name);
        DWORD attrs = GetFileAttributesW(sources[i].c_str());
        if ((attrs != INVALID_FILE_ATTRIBUTES) &&
            (attrs & FILE_ATTRIBUTE_DIRECTORY) &&
            IsSameOrChildPath(target, sources[i])) {
            ++failed;
            continue;
        }
        if (CopyPathRecursive(sources[i], target)) {
            ++copied;
        } else {
            ++failed;
        }
    }

    if (copied > 0) {
        SetStatus(failed == 0 ? TextZhEn(L"粘贴完成。", L"Paste completed.")
                              : TextZhEn(L"部分项目粘贴失败。", L"Some items could not be pasted."));
        NotifyDirectoryUpdated(destination);
        RefreshCurrentView();
        return failed == 0;
    }

    SetStatus(TextZhEn(L"粘贴失败。", L"Paste failed."));
    return false;
}

static bool GetDropFilesFromDataObject(IDataObject* data, std::vector<std::wstring>* paths) {
    if (!paths) return false;
    paths->clear();
    if (!data) return false;

    FORMATETC format;
    ZeroMemory(&format, sizeof(format));
    format.cfFormat = CF_HDROP;
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = -1;
    format.tymed = TYMED_HGLOBAL;

    STGMEDIUM medium;
    ZeroMemory(&medium, sizeof(medium));
    HRESULT hr = data->GetData(&format, &medium);
    if (FAILED(hr)) {
        return false;
    }

    HDROP drop = reinterpret_cast<HDROP>(medium.hGlobal);
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
    for (UINT i = 0; i < count; ++i) {
        UINT chars = DragQueryFileW(drop, i, NULL, 0);
        if (chars == 0) continue;

        std::wstring file(chars + 1, L'\0');
        if (DragQueryFileW(drop, i, &file[0], chars + 1)) {
            file.resize(chars);
            paths->push_back(file);
        }
    }

    ReleaseStgMedium(&medium);
    return !paths->empty();
}

static bool AddShellItemFilesystemPath(IShellItem* item, std::vector<std::wstring>* paths) {
    if (!item || !paths) return false;

    PWSTR path = NULL;
    HRESULT hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    if (FAILED(hr) || !path || !path[0]) {
        if (path) {
            CoTaskMemFree(path);
        }
        return false;
    }

    DWORD attrs = GetFileAttributesW(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        CoTaskMemFree(path);
        return false;
    }
    if (IsDriveRootPath(path)) {
        CoTaskMemFree(path);
        return false;
    }

    paths->push_back(path);
    CoTaskMemFree(path);
    return true;
}

static bool GetSelectedFilesystemPaths(std::vector<std::wstring>* paths) {
    if (!paths) return false;
    paths->clear();

    IFolderView2* folderView = NULL;
    if (g_browser) {
        g_browser->GetCurrentView(IID_PPV_ARGS(&folderView));
    }
    if (!folderView && g_shellView) {
        g_shellView->QueryInterface(IID_PPV_ARGS(&folderView));
    }

    if (folderView) {
        IShellItemArray* selection = NULL;
        HRESULT hr = folderView->GetSelection(FALSE, &selection);
        folderView->Release();

        if (SUCCEEDED(hr) && selection) {
            DWORD count = 0;
            if (SUCCEEDED(selection->GetCount(&count))) {
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* item = NULL;
                    if (SUCCEEDED(selection->GetItemAt(i, &item)) && item) {
                        AddShellItemFilesystemPath(item, paths);
                        item->Release();
                    }
                }
            }
            selection->Release();
            if (!paths->empty()) {
                return true;
            }
        }
    }

    IDataObject* data = NULL;
    if (g_shellView && SUCCEEDED(g_shellView->GetItemObject(SVGIO_SELECTION, IID_IDataObject,
                                                           reinterpret_cast<void**>(&data))) && data) {
        bool ok = GetDropFilesFromDataObject(data, paths);
        data->Release();
        if (ok) {
            return true;
        }
    }

    return false;
}

static HRESULT CopyDataObjectToFolder(IDataObject* data, const std::wstring& destination, BOOL* aborted) {
    if (aborted) {
        *aborted = FALSE;
    }
    if (!data || destination.empty()) {
        return E_INVALIDARG;
    }

    IShellItem* folder = NULL;
    HRESULT hr = SHCreateItemFromParsingName(destination.c_str(), NULL, IID_PPV_ARGS(&folder));
    if (FAILED(hr)) {
        return hr;
    }

    IFileOperation* operation = NULL;
    hr = CoCreateInstance(CLSID_FileOperation, NULL, CLSCTX_ALL, IID_PPV_ARGS(&operation));
    if (SUCCEEDED(hr)) {
        operation->SetOperationFlags(FOF_NOCONFIRMMKDIR | FOF_RENAMEONCOLLISION);
        hr = operation->CopyItems(data, folder);
        if (SUCCEEDED(hr)) {
            hr = operation->PerformOperations();
            if (SUCCEEDED(hr) && aborted) {
                operation->GetAnyOperationsAborted(aborted);
            }
        }
        operation->Release();
    }

    folder->Release();
    return hr;
}

static void CopySelectionToClipboard() {
    std::vector<std::wstring> selectedPaths;
    if (GetSelectedFilesystemPaths(&selectedPaths)) {
        g_localClipboardPaths = selectedPaths;

        IDataObject* data = NULL;
        if (g_shellView && SUCCEEDED(g_shellView->GetItemObject(SVGIO_SELECTION, IID_IDataObject,
                                                               reinterpret_cast<void**>(&data))) && data) {
            if (SUCCEEDED(OleSetClipboard(data))) {
                OleFlushClipboard();
            }
            data->Release();
        }

        SetStatus(TextZhEn(L"已复制选中项。", L"Copied selected items."));
        return;
    }

    if (!g_shellView) {
        SetStatus(TextZhEn(L"没有可复制的选中项。", L"No selected items to copy."));
        return;
    }

    IDataObject* data = NULL;
    HRESULT hr = g_shellView->GetItemObject(SVGIO_SELECTION, IID_IDataObject,
                                            reinterpret_cast<void**>(&data));
    if (FAILED(hr) || !data) {
        SetStatus(TextZhEn(L"没有可复制的选中项。", L"No selected items to copy."));
        return;
    }

    std::vector<std::wstring> localPaths;
    bool hasLocalPaths = GetDropFilesFromDataObject(data, &localPaths);

    hr = OleSetClipboard(data);
    data->Release();
    if (FAILED(hr)) {
        if (hasLocalPaths) {
            g_localClipboardPaths = localPaths;
            SetStatus(TextZhEn(L"系统剪贴板不可用，已在此窗口内复制选中项。",
                               L"System clipboard is unavailable; copied selected items inside this window."));
            return;
        }
        SetStatus(TextZhEn(L"复制失败。", L"Copy failed."));
        return;
    }

    if (hasLocalPaths) {
        g_localClipboardPaths = localPaths;
    } else {
        g_localClipboardPaths.clear();
    }
    OleFlushClipboard();
    SetStatus(TextZhEn(L"已复制选中项。", L"Copied selected items."));
}

static void PasteClipboardToCurrentFolder() {
    std::wstring destination;
    if (!CurrentFolderPath(&destination)) {
        SetStatus(TextZhEn(L"当前位置不能粘贴文件。", L"Cannot paste files here."));
        return;
    }

    if (!g_localClipboardPaths.empty()) {
        CopyFilesystemPathsToFolder(g_localClipboardPaths, destination);
        return;
    }

    IDataObject* data = NULL;
    HRESULT hr = OleGetClipboard(&data);
    if (FAILED(hr) || !data) {
        if (!g_localClipboardPaths.empty()) {
            CopyFilesystemPathsToFolder(g_localClipboardPaths, destination);
            return;
        }
        SetStatus(TextZhEn(L"剪贴板中没有可粘贴的文件。", L"No files are available on the clipboard."));
        return;
    }

    BOOL aborted = FALSE;
    hr = CopyDataObjectToFolder(data, destination, &aborted);
    if (SUCCEEDED(hr)) {
        data->Release();
        if (aborted) {
            SetStatus(TextZhEn(L"粘贴已取消。", L"Paste canceled."));
            return;
        }
        SetStatus(TextZhEn(L"粘贴完成。", L"Paste completed."));
        RefreshCurrentView();
        return;
    }

    std::vector<std::wstring> sources;
    if (!GetDropFilesFromDataObject(data, &sources)) {
        data->Release();
        if (!g_localClipboardPaths.empty()) {
            CopyFilesystemPathsToFolder(g_localClipboardPaths, destination);
            return;
        }
        SetStatus(TextZhEn(L"剪贴板中没有可粘贴的文件。", L"No files are available on the clipboard."));
        return;
    }
    data->Release();

    CopyFilesystemPathsToFolder(sources, destination);
}

static bool HandleClipboardShortcut(MSG* msg) {
    if (!msg) return false;
    if (msg->message != WM_KEYDOWN && msg->message != WM_SYSKEYDOWN) {
        return false;
    }
    if (GetFocus() == g_pathEdit) {
        return false;
    }
    if ((GetKeyState(VK_CONTROL) & 0x8000) == 0) {
        return false;
    }

    if (msg->wParam == L'C') {
        CopySelectionToClipboard();
        return true;
    }
    if (msg->wParam == L'X') {
        return ExecuteNativeShellCommand(OLECMDID_CUT);
    }
    if (msg->wParam == L'V') {
        PasteClipboardToCurrentFolder();
        return true;
    }
    if (msg->wParam == L'A') {
        return ExecuteNativeShellCommand(OLECMDID_SELECTALL);
    }

    return false;
}

static bool NavigateToPath(const std::wstring& rawPath) {
    if (!g_browser) return false;

    std::wstring path = TrimPathInput(rawPath);
    if (path.empty()) return false;

    PIDLIST_ABSOLUTE pidl = NULL;
    SFGAOF attr = 0;
    HRESULT hr = SHParseDisplayName(path.c_str(), NULL, &pidl, 0, &attr);
    if (FAILED(hr) || !pidl) {
        ShowHresultError(g_main, L"Parse path", hr);
        return false;
    }

    hr = g_browser->BrowseToIDList(pidl, SBSP_ABSOLUTE);
    CoTaskMemFree(pidl);
    if (FAILED(hr)) {
        ShowHresultError(g_main, L"Browse", hr);
        return false;
    }

    return true;
}

static void OpenCmdHere() {
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWMAXIMIZED;

    std::wstring workdir = g_currentPath;
    if (workdir.empty() || GetFileAttributesW(workdir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        wchar_t windowsDir[MAX_PATH];
        GetWindowsDirectoryW(windowsDir, MAX_PATH);
        workdir.assign(windowsDir, 3);
    }

    wchar_t command[] = L"cmd.exe";
    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, CREATE_NEW_CONSOLE,
                        NULL, workdir.c_str(), &startup, &process)) {
        ShowLastError(g_main, L"Open cmd");
        return;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

static void ScheduleSelfDeleteIfInstalled() {
    wchar_t modulePath[MAX_PATH];
    wchar_t systemDir[MAX_PATH];
    if (!GetModuleFileNameW(NULL, modulePath, MAX_PATH) ||
        !GetSystemDirectoryW(systemDir, MAX_PATH)) {
        return;
    }

    std::wstring installedPath = AddSlash(systemDir) + L"GrabAccessExplorerHost.exe";
    if (_wcsicmp(modulePath, installedPath.c_str()) != 0) {
        return;
    }

    std::wstring command = L"cmd.exe /c ping 127.0.0.1 -n 2 > nul & del /f /q \"";
    command += modulePath;
    command += L"\" > nul 2>&1";

    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;

    if (CreateProcessW(NULL, &command[0], NULL, NULL, FALSE, CREATE_NO_WINDOW,
                       NULL, NULL, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

class BrowserEvents : public IExplorerBrowserEvents {
public:
    BrowserEvents() : refCount_(1) {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (!ppv) return E_POINTER;
        *ppv = NULL;
        if (riid == IID_IUnknown || riid == IID_IExplorerBrowserEvents) {
            *ppv = static_cast<IExplorerBrowserEvents*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() {
        return static_cast<ULONG>(InterlockedIncrement(&refCount_));
    }

    IFACEMETHODIMP_(ULONG) Release() {
        LONG count = InterlockedDecrement(&refCount_);
        if (count == 0) {
            delete this;
            return 0;
        }
        return static_cast<ULONG>(count);
    }

    IFACEMETHODIMP OnNavigationPending(PCIDLIST_ABSOLUTE) {
        SetStatus(TextZhEn(L"导航中...", L"Navigating..."));
        return S_OK;
    }

    IFACEMETHODIMP OnViewCreated(IShellView* shellView) {
        if (g_shellView) {
            g_shellView->Release();
            g_shellView = NULL;
        }
        if (shellView) {
            g_shellView = shellView;
            g_shellView->AddRef();
        }
        return S_OK;
    }

    IFACEMETHODIMP OnNavigationComplete(PCIDLIST_ABSOLUTE pidlFolder) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(pidlFolder, path)) {
            g_currentPath = path;
            SetWindowTextW(g_pathEdit, g_currentPath.c_str());
            SetStatusPath(g_currentPath);
            return S_OK;
        }

        PWSTR displayName = NULL;
        HRESULT hr = SHGetNameFromIDList(pidlFolder, SIGDN_DESKTOPABSOLUTEPARSING, &displayName);
        if (SUCCEEDED(hr) && displayName) {
            g_currentPath = displayName;
            SetWindowTextW(g_pathEdit, g_currentPath.c_str());
            SetStatusPath(g_currentPath);
            CoTaskMemFree(displayName);
        }
        return S_OK;
    }

    IFACEMETHODIMP OnNavigationFailed(PCIDLIST_ABSOLUTE) {
        SetStatus(TextZhEn(L"导航失败。", L"Navigation failed."));
        return S_OK;
    }

private:
    ~BrowserEvents() {}
    LONG refCount_;
};

static BrowserEvents* g_events = NULL;

static RECT BrowserRect(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    rc.left += 8;
    rc.right -= 8;
    rc.top += 42;
    rc.bottom -= 30;
    if (rc.bottom < rc.top) rc.bottom = rc.top;
    return rc;
}

static void LayoutControls(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int margin = 8;
    int top = margin;
    int buttonH = 26;
    int smallW = 70;
    int cmdW = 90;

    int rightButtons = (smallW * 3) + cmdW + (margin * 4);
    MoveWindow(g_pathEdit, margin, top, width - rightButtons - (margin * 2), buttonH, TRUE);

    int x = width - rightButtons;
    MoveWindow(GetDlgItem(hwnd, IDC_GO), x, top, smallW, buttonH, TRUE);
    x += smallW + margin;
    MoveWindow(GetDlgItem(hwnd, IDC_UP), x, top, smallW, buttonH, TRUE);
    x += smallW + margin;
    MoveWindow(GetDlgItem(hwnd, IDC_REFRESH), x, top, smallW, buttonH, TRUE);
    x += smallW + margin;
    MoveWindow(GetDlgItem(hwnd, IDC_CMD), x, top, cmdW, buttonH, TRUE);

    MoveWindow(g_status, margin, rc.bottom - 24, width - (margin * 2), 20, TRUE);

    if (g_browser) {
        RECT browser = BrowserRect(hwnd);
        g_browser->SetRect(NULL, browser);
    }
}

static HMENU ControlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

static HWND CreateButton(HWND parent, int id, const wchar_t* text) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                           0, 0, 0, 0, parent, ControlId(id), g_instance, NULL);
}

static bool CreateExplorerBrowser(HWND hwnd) {
    HRESULT hr = CoCreateInstance(CLSID_ExplorerBrowser, NULL, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&g_browser));
    if (FAILED(hr)) {
        ShowHresultError(hwnd, L"Create ExplorerBrowser", hr);
        return false;
    }

    FOLDERSETTINGS settings;
    ZeroMemory(&settings, sizeof(settings));
    settings.ViewMode = FVM_DETAILS;
    settings.fFlags = FWF_AUTOARRANGE | FWF_NOWEBVIEW;

    RECT browser = BrowserRect(hwnd);
    hr = g_browser->Initialize(hwnd, &browser, &settings);
    if (FAILED(hr)) {
        ShowHresultError(hwnd, L"Initialize ExplorerBrowser", hr);
        g_browser->Release();
        g_browser = NULL;
        return false;
    }

    g_events = new BrowserEvents();
    hr = g_browser->Advise(g_events, &g_adviseCookie);
    if (FAILED(hr)) {
        g_adviseCookie = 0;
    }

    return true;
}

static void CreateControls(HWND hwnd) {
    g_pathEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                 0, 0, 0, 0, hwnd, ControlId(IDC_PATH), g_instance, NULL);
    CreateButton(hwnd, IDC_GO, TextZhEn(L"转到", L"Go"));
    CreateButton(hwnd, IDC_UP, TextZhEn(L"向上", L"Up"));
    CreateButton(hwnd, IDC_REFRESH, TextZhEn(L"刷新", L"Refresh"));
    CreateButton(hwnd, IDC_CMD, TextZhEn(L"命令行", L"Cmd"));

    g_status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                               0, 0, 0, 0, hwnd, ControlId(IDC_STATUS), g_instance, NULL);
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_main = hwnd;
        g_language = DetectLanguage();
        SetWindowTextW(hwnd, TextZhEn(L"GrabAccess 文件管理", L"GrabAccess File Manager"));
        SetMenu(hwnd, CreateMainMenu());
        CreateControls(hwnd);
        LayoutControls(hwnd);
        CreateExplorerBrowser(hwnd);

        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::wstring startPath;
        if (argv && argc > 1) {
            startPath = argv[1];
        }
        if (argv) {
            LocalFree(argv);
        }
        if (startPath.empty()) {
            startPath = L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}";
        }
        SetWindowTextW(g_pathEdit, startPath.c_str());
        NavigateToPath(startPath);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        return 0;
    }
    case WM_SIZE:
        LayoutControls(hwnd);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_EDIT_CUT:
            if (!ExecuteEditControlCommand(WM_CUT)) {
                ExecuteNativeShellCommand(OLECMDID_CUT);
            }
            return 0;
        case IDM_EDIT_COPY:
            if (!ExecuteEditControlCommand(WM_COPY)) {
                CopySelectionToClipboard();
            }
            return 0;
        case IDM_EDIT_PASTE:
            if (!ExecuteEditControlCommand(WM_PASTE)) {
                PasteClipboardToCurrentFolder();
            }
            return 0;
        case IDM_EDIT_SELECTALL:
            if (PathEditHasFocus()) {
                SendMessageW(g_pathEdit, EM_SETSEL, 0, -1);
            } else {
                ExecuteNativeShellCommand(OLECMDID_SELECTALL);
            }
            return 0;
        case IDM_EDIT_DELETE:
            if (!ExecuteEditControlCommand(WM_CLEAR)) {
                ExecuteNativeShellCommand(OLECMDID_DELETE);
            }
            return 0;
        case IDC_GO:
            NavigateToPath(GetEditText(g_pathEdit));
            return 0;
        case IDC_UP:
            if (IsThisPcPath(g_currentPath)) {
                return 0;
            }
            if (IsDriveRootPath(g_currentPath)) {
                NavigateToPath(ThisPcPath());
                return 0;
            }
            if (!g_currentPath.empty() && GetFileAttributesW(g_currentPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                std::wstring parent = ParentPath(g_currentPath);
                if (parent == g_currentPath) {
                    NavigateToPath(ThisPcPath());
                } else {
                    NavigateToPath(parent);
                }
            } else {
                NavigateToPath(ThisPcPath());
            }
            return 0;
        case IDC_REFRESH:
            RefreshCurrentView();
            return 0;
        case IDC_CMD:
            OpenCmdHere();
            return 0;
        case IDC_PATH:
            if (HIWORD(wParam) == EN_MAXTEXT) {
                return 0;
            }
            return 0;
        default:
            break;
        }
        break;
    case WM_DESTROY:
        if (g_browser && g_adviseCookie) {
            g_browser->Unadvise(g_adviseCookie);
            g_adviseCookie = 0;
        }
        if (g_browser) {
            g_browser->Destroy();
            g_browser->Release();
            g_browser = NULL;
        }
        if (g_shellView) {
            g_shellView->Release();
            g_shellView = NULL;
        }
        if (g_events) {
            g_events->Release();
            g_events = NULL;
        }
        ScheduleSelfDeleteIfInstalled();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    g_instance = instance;

    HRESULT hr = OleInitialize(NULL);
    if (FAILED(hr)) {
        return 1;
    }

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"GrabAccessExplorerHostWindow";
    if (!RegisterClassExW(&wc)) {
        OleUninitialize();
        return 1;
    }

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST,
                                wc.lpszClassName,
                                L"GrabAccess Explorer Host",
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                1100, 720,
                                NULL, NULL, instance, NULL);
    if (!hwnd) {
        OleUninitialize();
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (HandleClipboardShortcut(&msg)) {
            continue;
        }
        if (g_shellView && g_shellView->TranslateAccelerator(&msg) == S_OK) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    OleUninitialize();
    return static_cast<int>(msg.wParam);
}
