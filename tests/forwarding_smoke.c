/* Check real DLL loading, export thunks, and Direct3D 8 forwarding. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL line %d: %s (Win32 %lu)\n", __LINE__, #condition, GetLastError()); return 1; \
} } while (0)

int wmain(int argc, WCHAR **argv)
{
    static const char *const names[] = {
        "Direct3D8EnableMaximizedWindowedModeShim", "ValidatePixelShader",
        "ValidateVertexShader", "DebugSetMute", "Direct3DCreate8"
    };
    typedef void *(WINAPI *CreateFunction)(UINT);
    typedef ULONG (WINAPI *ReleaseFunction)(void *);
    WCHAR full_path[MAX_PATH], directory[MAX_PATH], system_path[MAX_PATH];
    HMODULE proxy, system;
    CreateFunction create;
    FARPROC *first_slot = NULL;
    BOOL wow64 = FALSE;
    DWORD length;
    unsigned i;
    CHECK(argc == 2);
    length = GetFullPathNameW(argv[1], MAX_PATH, full_path, NULL);
    CHECK(length && length < MAX_PATH);
    proxy = LoadLibraryExW(full_path, NULL, 0);
    CHECK(proxy != NULL);
    for (i = 0; i < 5; ++i) {
        FARPROC function = GetProcAddress(proxy, names[i]);
        CHECK(function != NULL);
        CHECK(function == GetProcAddress(proxy, (LPCSTR)(ULONG_PTR)(i + 1)));
        if (i < 4) {
            const BYTE *code = (const BYTE *)(ULONG_PTR)function;
            FARPROC *slot;
            /* pushfd; pushad; call rel32; popad; popfd; jmp [absolute slot]. */
            CHECK(code[0] == 0x9c && code[1] == 0x60 && code[2] == 0xe8 &&
                  code[7] == 0x61 && code[8] == 0x9d && code[9] == 0xff && code[10] == 0x25);
            slot = *(FARPROC *const *)(code + 11);
            if (!i) first_slot = slot;
            CHECK(slot == first_slot + i);
            CHECK(*slot == NULL);
        }
    }
    create = (CreateFunction)GetProcAddress(proxy, names[4]);
    for (i = 0; i < 2; ++i) {
        void *object = create(0xdc);
        void **vtable;
        CHECK(object != NULL);
        vtable = *(void ***)object;
        CHECK(((ReleaseFunction)(ULONG_PTR)vtable[2])(object) == 0);
    }
    CHECK(IsWow64Process(GetCurrentProcess(), &wow64));
    length = wow64 ? GetSystemWow64DirectoryW(directory, MAX_PATH) : GetSystemDirectoryW(directory, MAX_PATH);
    CHECK(length && length + 10 < MAX_PATH);
    lstrcpyW(system_path, directory);
    lstrcatW(system_path, L"\\d3d8.dll");
    system = GetModuleHandleW(system_path);
    CHECK(system && system != proxy);
    for (i = 0; i < 4; ++i) {
        CHECK(first_slot[i] == GetProcAddress(system, names[i]));
    }
    CHECK(FreeLibrary(proxy));
    printf("Forwarding: five exports, four x86 thunks, and two Direct3DCreate8 calls passed\n");
    return 0;
}
