static void ZeroBytes(void *destination, SIZE_T length)
{
    volatile BYTE *p = (volatile BYTE *)destination;
    while (length--) *p++ = 0;
}
static BOOL EqualBytes(const void *left, const void *right, SIZE_T length)
{
    const volatile BYTE *a = (const volatile BYTE *)left;
    const BYTE *b = (const BYTE *)right;
    SIZE_T i;
    for (i = 0; i < length; ++i) if (a[i] != b[i]) return FALSE;
    return TRUE;
}
static BOOL EqualWide(const WCHAR *left, const WCHAR *right)
{
    return CompareStringOrdinal(left, -1, right, -1, TRUE) == CSTR_EQUAL;
}
static BOOL AppendWide(WCHAR *out, DWORD capacity, const WCHAR *a, const WCHAR *b)
{
    DWORD n = 0;
    while (*a) { if (n + 1 >= capacity) return FALSE; out[n++] = *a++; }
    while (*b) { if (n + 1 >= capacity) return FALSE; out[n++] = *b++; }
    out[n] = 0;
    return TRUE;
}
