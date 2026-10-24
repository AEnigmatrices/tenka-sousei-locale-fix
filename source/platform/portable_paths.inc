#ifndef TENKA_PORTABLE_PATHS_H
#define TENKA_PORTABLE_PATHS_H
#include <shlobj.h>

static INIT_ONCE g_logPathOnce = INIT_ONCE_STATIC_INIT;
static WCHAR g_logDirectory[MAX_PATH];
static BOOL g_logPathOK;
static DWORD g_logPathError = ERROR_PATH_NOT_FOUND;

static DWORD PortableLength(const WCHAR *s)
{
    DWORD n = 0;
    if (!s) return MAX_PATH;
    while (n < MAX_PATH && s[n]) ++n;
    return n;
}
static BOOL PortableAbsolute(const WCHAR *s)
{
    DWORD n = PortableLength(s), i, start;
    if (n < 3 || n >= MAX_PATH ||
        !((s[0] >= L'A' && s[0] <= L'Z') || (s[0] >= L'a' && s[0] <= L'z')) ||
        s[1] != L':' || s[2] != L'\\') return FALSE;
    start = 3;
    for (i = 3; i <= n; ++i) {
        if (s[i] == L'/' || (i < n && (s[i] == L':' || s[i] == L'"'))) return FALSE;
        if (!s[i] || s[i] == L'\\') {
            DWORD count = i - start;
            if ((count == 1 && s[start] == L'.') ||
                (count == 2 && s[start] == L'.' && s[start + 1] == L'.') ||
                (i < n && count == 0)) return FALSE;
            start = i + 1;
        }
    }
    return TRUE;
}
static BOOL PortableSiblingPaths(const WCHAR *image, const WCHAR *self)
{
    DWORD a = PortableLength(image), b = PortableLength(self), ia, ib;
    if (!PortableAbsolute(image) || !PortableAbsolute(self)) return FALSE;
    ia = a; ib = b;
    while (ia && image[ia - 1] != L'\\') --ia;
    while (ib && self[ib - 1] != L'\\') --ib;
    return ia == ib && ia >= 3 &&
        CompareStringOrdinal(image, (int)ia, self, (int)ib, TRUE) == CSTR_EQUAL &&
        EqualWide(image + ia, L"Nobunaga11WPK.exe") && EqualWide(self + ib, L"d3d8.dll");
}
static BOOL PortableGameLocation(const WCHAR *image)
{
    WCHAR self[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_self, self, MAX_PATH);
    if (!n || n >= MAX_PATH) return FALSE;
    return PortableSiblingPaths(image, self);
}
static BOOL PortableLogLayout(const WCHAR *profile, WCHAR *directory, DWORD capacity)
{
    WCHAR base[MAX_PATH];
    DWORD i, n = PortableLength(profile);
    if (!PortableAbsolute(profile) || n >= MAX_PATH) {
        SetLastError(ERROR_INVALID_NAME); return FALSE;
    }
    while (n > 3 && profile[n - 1] == L'\\') --n;
    for (i = 0; i < n; ++i) base[i] = profile[i];
    base[n] = 0;
    /* Reserve64 UTF-16 units for the largest CREATE_NEW diagnostic filename. */
    if (!AppendWide(directory, capacity, base,
                    n == 3 ? L"TenkaSouseiCP932\\Logs\\" : L"\\TenkaSouseiCP932\\Logs\\") ||
        PortableLength(directory) >= MAX_PATH - 64) {
        SetLastError(ERROR_BUFFER_OVERFLOW); return FALSE;
    }
    return TRUE;
}
static BOOL PortableEnsureDirectory(const WCHAR *directory)
{
    DWORD attributes, error;
    if (!CreateDirectoryW(directory, NULL)) {
        error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) { SetLastError(error); return FALSE; }
    }
    attributes = GetFileAttributesW(directory);
    if (attributes == INVALID_FILE_ATTRIBUTES) return FALSE;
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        SetLastError(ERROR_INVALID_NAME); return FALSE;
    }
    return TRUE;
}
static BOOL PortablePrepareLogDirectory(const WCHAR *profile, WCHAR *directory, DWORD capacity)
{
    WCHAR product[MAX_PATH];
    DWORD i, n, attributes;
    if (!PortableLogLayout(profile, directory, capacity)) return FALSE;
    attributes = GetFileAttributesW(profile);
    if (attributes == INVALID_FILE_ATTRIBUTES) return FALSE;
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        SetLastError(ERROR_INVALID_NAME); return FALSE;
    }
    n = PortableLength(directory);
    /* Strip the final Logs\\ component to create only the two product folders. */
    if (n < 6) { SetLastError(ERROR_INVALID_NAME); return FALSE; }
    for (i = 0; i < n - 5; ++i) product[i] = directory[i];
    product[n - 5] = 0;
    if (!PortableEnsureDirectory(product) || !PortableEnsureDirectory(directory)) return FALSE;
    return TRUE;
}
static BOOL CALLBACK PortableInitializeLogPath(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    WCHAR profile[MAX_PATH];
    HRESULT result;
    (void)once; (void)parameter; (void)context;
    result = SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, profile);
    if (FAILED(result)) {
        g_logPathError = ERROR_PATH_NOT_FOUND; return TRUE;
    }
    if (!PortablePrepareLogDirectory(profile, g_logDirectory, MAX_PATH)) {
        g_logPathError = GetLastError(); return TRUE;
    }
    g_logPathOK = TRUE; g_logPathError = ERROR_SUCCESS;
    return TRUE;
}
static const WCHAR *PortableRuntimeDirectory(void)
{
    if (!InitOnceExecuteOnce(&g_logPathOnce, PortableInitializeLogPath, NULL, NULL) || !g_logPathOK) {
        if (g_logPathError) SetLastError(g_logPathError);
        return NULL;
    }
    return g_logDirectory;
}
#endif
