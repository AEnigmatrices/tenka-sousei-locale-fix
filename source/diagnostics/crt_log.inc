static void PutChar(char c)
{
    if (g_text.length >= sizeof(g_text.bytes)) g_text.ok = FALSE;
    else g_text.bytes[g_text.length++] = c;
}
static void PutText(const char *s) { while (*s) PutChar(*s++); }
static void PutNumber(DWORD v)
{
    char reverse[10]; DWORD n = 0;
    do { reverse[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) PutChar(reverse[--n]);
}
static char HexDigit(DWORD x) { return "0123456789abcdef"[x & 15]; }
static void PutHex(const BYTE *bytes, DWORD count, BOOL masked)
{
    DWORD i; BYTE b;
    PutChar('"');
    for (i = 0; i < count; ++i) {
        b = masked ? bytes[i + 1] & 0x0c : bytes[i];
        PutChar(HexDigit(b >> 4)); PutChar(HexDigit(b));
    }
    PutChar('"');
}
static void PutWide(const WCHAR *s)
{
    WCHAR c;
    PutChar('"');
    while ((c = *s++) != 0) {
        if (c == '\\' || c == '"') { PutChar('\\'); PutChar((char)c); }
        else if (c >= 32 && c < 127) PutChar((char)c);
        else {
            PutText("\\u"); PutChar(HexDigit(c >> 12)); PutChar(HexDigit(c >> 8));
            PutChar(HexDigit(c >> 4)); PutChar(HexDigit(c));
        }
    }
    PutChar('"');
}
static void PutState(const State *s)
{
    if (!s->available) { PutText("null"); return; }
    PutText("{\"page\":"); PutNumber(s->page);
    PutText(",\"guard\":"); PutNumber(s->guard);
    PutText(",\"selector_flag\":"); PutNumber(s->selector_flag);
    PutText(",\"info_pointer\":"); PutNumber(s->info_pointer);
    PutText(",\"stable_bookends\":"); PutText(s->stable ? "true" : "false");
    PutText(",\"raw_257_hex\":"); PutHex(s->table, 257, FALSE);
    PutText(",\"byte_plus_one_mask_0c_hex\":"); PutHex(s->table, 256, TRUE);
    PutChar('}');
}
static BOOL WriteRecord(HANDLE file, const Trial *trial, const char *phase)
{
    DWORD written;
    g_text.length = 0; g_text.ok = TRUE;
    PutText("{\"schema\":\"Tenka.CP932.D3D8.Runtime.v2\",\"phase\":\"");
    PutText(phase); PutText("\",\"status\":\""); PutText(trial->status);
    PutText("\",\"pid\":"); PutNumber(GetCurrentProcessId());
    PutText(",\"tid\":"); PutNumber(GetCurrentThreadId());
    PutText(",\"image_base\":"); PutNumber((DWORD)trial->base);
    PutText(",\"caller_rva\":"); PutNumber((DWORD)(trial->caller - trial->base));
    PutText(",\"sdk\":"); PutNumber(trial->sdk);
    PutText(",\"image_path\":"); PutWide(trial->image_path);
    PutText(",\"disk_sha256\":");
    if (trial->disk_hash_available) PutHex(trial->disk_hash, 32, FALSE); else PutText("null");
    PutText(",\"win32_acp\":"); PutNumber(GetACP());
    PutText(",\"win32_oemcp\":"); PutNumber(GetOEMCP());
    PutText(",\"setter_called\":"); PutText(trial->setter_called ? "true" : "false");
    PutText(",\"precall_rechecked\":"); PutText(trial->precall_rechecked ? "true" : "false");
    PutText(",\"mutable_ranges_verified\":"); PutText(trial->mutable_ranges_verified ? "true" : "false");
    PutText(",\"setter_result\":");
    if (trial->setter_called) PutNumber((DWORD)trial->setter_result); else PutText("null");
    PutText(",\"error\":"); PutNumber(trial->error);
    PutText(",\"before\":"); PutState(&trial->before);
    PutText(",\"after\":"); PutState(&trial->after);
    PutText("}\n");
    return g_text.ok && WriteFile(file, g_text.bytes, g_text.length, &written, NULL) &&
           written == g_text.length && FlushFileBuffers(file);
}
static HANDLE OpenTrialLog(void)
{
    WCHAR name[96], path[MAX_PATH];
    const WCHAR *directory = PortableRuntimeDirectory();
    if (!directory) return INVALID_HANDLE_VALUE;
    FILETIME time;
    DWORD values[4], i, j, n = 0;
    const WCHAR prefix[] = L"crt-";
    const WCHAR suffix[] = L".jsonl";
    GetSystemTimeAsFileTime(&time);
    values[0] = time.dwHighDateTime; values[1] = time.dwLowDateTime;
    values[2] = GetCurrentProcessId(); values[3] = GetCurrentThreadId();
    for (i = 0; prefix[i]; ++i) name[n++] = prefix[i];
    for (i = 0; i < 4; ++i) {
        if (i) name[n++] = '-';
        for (j = 0; j < 8; ++j) name[n++] = (WCHAR)HexDigit(values[i] >> (28 - 4 * j));
    }
    for (i = 0; suffix[i]; ++i) name[n++] = suffix[i];
    name[n] = 0;
    if (!AppendWide(path, MAX_PATH, directory, name)) { SetLastError(ERROR_BUFFER_OVERFLOW); return INVALID_HANDLE_VALUE; }
    /* Product folders are created once in LocalAppData; no overwrite/retry/fallback. */
    return CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL, NULL);
}

