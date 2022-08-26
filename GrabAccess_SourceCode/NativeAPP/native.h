//Environment information, which includes command line and image file name
//#define NtCurrentProcess() ( (HANDLE) -1 )

typedef struct 
{
    ULONG            Unknown[21];     
    UNICODE_STRING   CommandLine;
    UNICODE_STRING   ImageFile;
} ENVIRONMENT_INFORMATION, *PENVIRONMENT_INFORMATION;

 
// This structure is passed as NtProcessStartup's parameter
typedef struct 
{
    ULONG                     Unknown[3];
    PENVIRONMENT_INFORMATION  Environment;
} STARTUP_ARGUMENT, *PSTARTUP_ARGUMENT;

// Data structure for heap definition. 
// This includes various sizing parameters and callback routines, 
// which, if left NULL, result in default behavior

typedef struct 
{
    ULONG     Length;
    ULONG     Unknown[11];
} RTL_HEAP_DEFINITION, *PRTL_HEAP_DEFINITION;

 
// Native NT api function to write something to the boot-time
// blue screen
NTSTATUS NTAPI NtDisplayString(
    PUNICODE_STRING String
    );


// Native applications must kill themselves when done - 
// the job of this native API
NTSTATUS NTAPI NtTerminateProcess(
    HANDLE ProcessHandle, 
    LONG ExitStatus 
    );

 

// Definition to represent current process
//fox #define NtCurrentProcess() ( (HANDLE) -1 )

// Heap creation routine
HANDLE NTAPI RtlCreateHeap(
    ULONG Flags, 
    PVOID BaseAddress, 
    ULONG SizeToReserve, 
    ULONG SizeToCommit, 
    PVOID Unknown,
    PRTL_HEAP_DEFINITION Definition
    );

	
// Heap allocation function (ala "malloc")
PVOID NTAPI RtlAllocateHeap(
    HANDLE Heap, 
    ULONG Flags, 
    ULONG Size 
    );

// Heap free function (ala "free")
BOOLEAN NTAPI RtlFreeHeap(
    HANDLE Heap, 
    ULONG Flags, 
    PVOID Address 
    );

//FOX 
PVOID
NTAPI
RtlDestroyHeap(
  HANDLE               Heap 
  );
//FOX   
    
NTSTATUS NTAPI NtDelayExecution(BOOLEAN Alertable, PLARGE_INTEGER Interval);    

//FOX-20190520 - File Access

//
// NT Function calls, renamed NtXXXX from their ZwXXXX counterparts in NTDDK.H
//
NTSYSAPI
NTSTATUS
NTAPI
NtReadFile(HANDLE FileHandle,
    HANDLE Event OPTIONAL,
    PIO_APC_ROUTINE ApcRoutine OPTIONAL,
    PVOID ApcContext OPTIONAL,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID Buffer,
    ULONG Length,
    PLARGE_INTEGER ByteOffset OPTIONAL,
    PULONG Key OPTIONAL);

NTSYSAPI
NTSTATUS
NTAPI
NtWriteFile(HANDLE FileHandle,
    HANDLE Event OPTIONAL,
    PIO_APC_ROUTINE ApcRoutine OPTIONAL,
    PVOID ApcContext OPTIONAL,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID Buffer,
    ULONG Length,
    PLARGE_INTEGER ByteOffset OPTIONAL,
    PULONG Key OPTIONAL);

NTSYSAPI
NTSTATUS
NTAPI
NtClose(HANDLE Handle);

NTSYSAPI
NTSTATUS
NTAPI
NtCreateFile(PHANDLE FileHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PIO_STATUS_BLOCK IoStatusBlock,
    PLARGE_INTEGER AllocationSize OPTIONAL,
    ULONG FileAttributes,
    ULONG ShareAccess,
    ULONG CreateDisposition,
    ULONG CreateOptions,
    PVOID EaBuffer OPTIONAL,
    ULONG EaLength);

NTSYSAPI
NTSTATUS
NTAPI
NtQueryInformationFile(HANDLE FileHandle, 
	PIO_STATUS_BLOCK IoStatusBlock, 
	PVOID FileInformation,
	ULONG Length, 
	FILE_INFORMATION_CLASS FileInformationClass);
//FOX-20190520 - File Access