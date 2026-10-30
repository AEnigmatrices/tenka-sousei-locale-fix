/* Exercise production image, writable-range and code-fingerprint guards. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include "../source/support.inc"
#include "../source/compat/verified_inputs.h"
#include "../source/compat/integrity.inc"

#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL line %d: %s (Win32 %lu)\n", __LINE__, #condition, GetLastError()); return 1; \
} } while (0)

__declspec(noinline) static DWORD CodeProbe(DWORD value)
{
    return value ^ 0x5a5a5a5au;
}

int main(void)
{
    SYSTEM_INFO info;
    ULONG_PTR image = (ULONG_PTR)GetModuleHandleW(NULL);
    ULONG_PTR code = (ULONG_PTR)CodeProbe;
    DWORD code_rva, old_protect;
    BYTE original_hash[32], wrong_hash[32];
    BYTE *pages;
    DWORD size;

    CHECK(image != 0 && code > image && code - image < TENKA_IMAGE_SIZE);
    code_rva = (DWORD)(code - image);
    CHECK(ImageRange(image, 0, 1, FALSE));
    CHECK(ImageRange(image, code_rva, 8, TRUE));
    CHECK(!ImageRange(image, TENKA_IMAGE_SIZE - 4, 8, FALSE));
    CHECK(!ImageRange(0, 0, 8, FALSE));
    CHECK(HashMemory((const BYTE *)code, 8, original_hash));
    CHECK(MatchesCodeFingerprint(image, code_rva, 8, original_hash));
    CopyMemory(wrong_hash, original_hash, sizeof(wrong_hash));
    wrong_hash[0] ^= 1;
    CHECK(!MatchesCodeFingerprint(image, code_rva, 8, wrong_hash));
    CHECK(!CheckImage(image)); /* Test binary is not the verified game build. */

    GetSystemInfo(&info);
    size = info.dwPageSize;
    CHECK(size >= 4096 && size <= 65536);
    pages = (BYTE *)VirtualAlloc(NULL, size * 2, MEM_RESERVE, PAGE_NOACCESS);
    CHECK(pages != NULL);
    CHECK(VirtualAlloc(pages, size, MEM_COMMIT, PAGE_READWRITE) == pages);
    CHECK(WritableRange((ULONG_PTR)pages, 16, image));
    CHECK(WritableRange((ULONG_PTR)pages, size, image));
    CHECK(!WritableRange((ULONG_PTR)pages, size + 1, image));
    CHECK(!ImageRange((ULONG_PTR)pages, 0, 8, FALSE)); /* MEM_PRIVATE, not MEM_IMAGE. */

    CHECK(VirtualProtect(pages, size, PAGE_READONLY, &old_protect));
    CHECK(!WritableRange((ULONG_PTR)pages, 16, image));
    CHECK(VirtualProtect(pages, size, PAGE_EXECUTE_READ, &old_protect));
    CHECK(!WritableRange((ULONG_PTR)pages, 16, image));
    CHECK(VirtualProtect(pages, size, PAGE_READWRITE | PAGE_GUARD, &old_protect));
    CHECK(!WritableRange((ULONG_PTR)pages, 16, image));
    CHECK(VirtualProtect(pages, size, PAGE_READWRITE, &old_protect));
    CHECK(WritableRange((ULONG_PTR)pages, 16, image));
    CHECK(!WritableRange((ULONG_PTR)-1, 8, image));
    CHECK(!WritableRange((ULONG_PTR)pages, 0, image));
    CHECK(VirtualFree(pages, 0, MEM_RELEASE));

    printf("Integrity: executable ranges, fingerprints and page protections passed\n");
    return 0;
}
