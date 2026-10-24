/* Exercise the actual production game-path validation helpers. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include "../source/support.inc"

static HMODULE g_self;
#include "../source/platform/game_paths.inc"

#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

int main(void)
{
    static const struct {
        const WCHAR *exe;
        const WCHAR *proxy;
        BOOL allowed;
    } cases[] = {
        { L"C:\\Games\\Tenka\\Nobunaga11WPK.exe", L"C:\\Games\\Tenka\\d3d8.dll", TRUE },
        { L"c:\\GAMES\\Tenka\\NOBUNAGA11WPK.EXE", L"C:\\games\\TENKA\\D3D8.DLL", TRUE },
        { L"D:\\Nobunaga11WPK.exe", L"D:\\d3d8.dll", TRUE },
        { L"C:\\Games\\Tenka\\Nobunaga11WPK.exe", L"C:\\Other\\d3d8.dll", FALSE },
        { L"C:\\Games\\Tenka\\Nobunaga11WPK.exe", L"D:\\Games\\Tenka\\d3d8.dll", FALSE },
        { L"C:\\Games\\Tenka\\Nobunaga11WPK.exe", L"C:\\Games\\Tenka\\d3d9.dll", FALSE },
        { L"C:\\Games\\Tenka\\Nobunaga11.exe", L"C:\\Games\\Tenka\\d3d8.dll", FALSE },
        { L"Games\\Tenka\\Nobunaga11WPK.exe", L"C:\\Games\\Tenka\\d3d8.dll", FALSE },
        { L"\\server\\share\\Nobunaga11WPK.exe", L"\\server\\share\\d3d8.dll", FALSE },
        { L"C:/Games/Tenka/Nobunaga11WPK.exe", L"C:\\Games\\Tenka\\d3d8.dll", FALSE },
        { L"C:\\Games\\..\\Tenka\\Nobunaga11WPK.exe", L"C:\\Tenka\\d3d8.dll", FALSE },
        { L"C:\\Games\\.\\Tenka\\Nobunaga11WPK.exe", L"C:\\Games\\Tenka\\d3d8.dll", FALSE },
        { L"C:\\Games\\\\Tenka\\Nobunaga11WPK.exe", L"C:\\Games\\Tenka\\d3d8.dll", FALSE },
        { L"C:\\Games\\Tenka\\Nobunaga11WPK.exe", L"C:\\Games\\Tenka\\..\\d3d8.dll", FALSE },
        { L"C:\\Games\\Tenka\\Nobunaga11WPK.exe", L"C:\\Games\\Tenka\\d3d8.dll:stream", FALSE },
        { L"C:\\Games\\Tenka\\Nobunaga11WPK.exe", L"C:\\Games\\Tenka\\\"d3d8.dll", FALSE },
        { L"", L"C:\\Games\\Tenka\\d3d8.dll", FALSE },
        { L"C:\\Games\\Tenka\\Nobunaga11WPK.exe", L"", FALSE }
    };
    size_t i;
    WCHAR self[MAX_PATH];
    DWORD n;

    CHECK(BoundedPathLength(NULL) == MAX_PATH);
    CHECK(!IsAbsoluteGamePath(L".\\Nobunaga11WPK.exe"));
    CHECK(IsAbsoluteGamePath(L"C:\\Games\\Tenka\\Nobunaga11WPK.exe"));
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        CHECK(AreGameSiblingPaths(cases[i].exe, cases[i].proxy) == cases[i].allowed);
    }
    g_self = GetModuleHandleW(NULL);
    CHECK(g_self != NULL);
    n = GetModuleFileNameW(g_self, self, MAX_PATH);
    CHECK(n > 0 && n < MAX_PATH);
    CHECK(!IsGameLocation(self)); /* This test executable is not the game. */
    printf("Game paths: %u sibling and traversal cases passed\n",
           (unsigned)(sizeof(cases) / sizeof(cases[0])));
    return 0;
}
