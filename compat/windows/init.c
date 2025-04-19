// SPDX-License-Identifier: Apache-2.0
#include <windows.h>

const char *program_invocation_name;

static void program_invocation_name_init(void)
{
    static char exe_path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    if (len && len < MAX_PATH) {
        program_invocation_name = exe_path;
    } else {
        program_invocation_name = "<unknown>";
    }
}

__attribute__((constructor))
static void compat_windows_init(void)
{
    program_invocation_name_init();
}
