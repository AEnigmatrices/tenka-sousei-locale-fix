static DWORD BoundedPathLength(const WCHAR *s)
{
    DWORD n = 0;
    if (!s) return MAX_PATH;
    while (n < MAX_PATH && s[n]) ++n;
    return n;
}
static BOOL IsAbsoluteGamePath(const WCHAR *s)
{
    DWORD n = BoundedPathLength(s), i, start;
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
static BOOL AreGameSiblingPaths(const WCHAR *image, const WCHAR *self)
{
    DWORD a = BoundedPathLength(image), b = BoundedPathLength(self), ia, ib;
    if (!IsAbsoluteGamePath(image) || !IsAbsoluteGamePath(self)) return FALSE;
    ia = a; ib = b;
    while (ia && image[ia - 1] != L'\\') --ia;
    while (ib && self[ib - 1] != L'\\') --ib;
    return ia == ib && ia >= 3 &&
        CompareStringOrdinal(image, (int)ia, self, (int)ib, TRUE) == CSTR_EQUAL &&
        EqualWide(image + ia, L"Nobunaga11WPK.exe") && EqualWide(self + ib, L"d3d8.dll");
}
static BOOL IsGameLocation(const WCHAR *image)
{
    WCHAR self[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_self, self, MAX_PATH);
    if (!n || n >= MAX_PATH) return FALSE;
    return AreGameSiblingPaths(image, self);
}
