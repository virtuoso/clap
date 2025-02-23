#include <windows.h>
// #include <winternl.h>
#include <stdlib.h>
#include <stdio.h>

// You may need to define NTSTATUS if it's not defined.
typedef LONG NTSTATUS;
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)

typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _ANSI_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PSTR   Buffer;
} ANSI_STRING, *PANSI_STRING;

// NT API functions - declare them as extern.
#if 0
__declspec(dllimport)
NTSTATUS NTAPI RtlInitUnicodeString(
    PUNICODE_STRING DestinationString,
    PCWSTR SourceString
);

__declspec(dllimport)
NTSTATUS NTAPI RtlUnicodeStringToUTF8String(
    PANSI_STRING DestinationString,
    PUNICODE_STRING SourceString,
    ULONG AllocateFlags
);

__declspec(dllimport)
NTSTATUS NTAPI RtlFreeUTF8String(
    PANSI_STRING UnicodeString
);
#endif

int main(int argc, char **argv, char **envp)
{
    puts("Hello, world!\n");
    return EXIT_SUCCESS;
}

#if 0
extern NTSTATUS NTAPI RtlInitUnicodeString(PUNICODE_STRING DestinationString, PCWSTR SourceString);
extern NTSTATUS NTAPI RtlUnicodeStringToUTF8String(PANSI_STRING DestinationString, PUNICODE_STRING SourceString, ULONG AllocateFlags);
extern NTSTATUS NTAPI RtlFreeUTF8String(PANSI_STRING UnicodeString);

int wmain(int argc, wchar_t **argv, wchar_t **envp)
{
    UNICODE_STRING ustr;
    ANSI_STRING astr;
    NTSTATUS status;

    // Initialize the UNICODE_STRING
    RtlInitUnicodeString(&ustr, L"Hello, world!");

    // Try converting to UTF8
    status = RtlUnicodeStringToUTF8String(&astr, &ustr, 0);
    if (status != 0) {
        printf("RtlUnicodeStringToUTF8String failed with status 0x%lx\n", status);
        return EXIT_FAILURE;
    } else {
        printf("Converted string: %.*s\n", astr.Length, astr.Buffer);
    }

    // Free the allocated UTF8 string
    status = RtlFreeUTF8String(&astr);
    if (status != 0) {
        printf("RtlFreeUTF8String failed with status 0x%lx\n", status);
        return EXIT_FAILURE;
    }

    return main(0, NULL, NULL);
}
#endif
