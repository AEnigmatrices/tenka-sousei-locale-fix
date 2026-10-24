
static void HashClose(HashContext *c)
{
    if (c->hash) BCryptDestroyHash(c->hash);
    if (c->algorithm) BCryptCloseAlgorithmProvider(c->algorithm, 0);
    if (c->object) HeapFree(GetProcessHeap(), 0, c->object);
    ZeroBytes(c, sizeof(*c));
}
static BOOL HashOpen(HashContext *c)
{
    DWORD object_length = 0, used = 0;
    ZeroBytes(c, sizeof(*c));
    if (BCryptOpenAlgorithmProvider(&c->algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0)
        return FALSE;
    if (BCryptGetProperty(c->algorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&object_length,
                         sizeof(object_length), &used, 0) < 0 ||
        used != sizeof(object_length) || !object_length || object_length > 8192) {
        HashClose(c); return FALSE;
    }
    c->object = (PUCHAR)HeapAlloc(GetProcessHeap(), 0, object_length);
    if (!c->object || BCryptCreateHash(c->algorithm, &c->hash, c->object,
                                      object_length, NULL, 0, 0) < 0) {
        HashClose(c); return FALSE;
    }
    return TRUE;
}
static BOOL HashMemory(const BYTE *memory, DWORD bytes, BYTE digest[32])
{
    HashContext c;
    BOOL ok;
    if (!HashOpen(&c)) return FALSE;
    ok = BCryptHashData(c.hash, (PUCHAR)memory, bytes, 0) >= 0 &&
         BCryptFinishHash(c.hash, digest, 32, 0) >= 0;
    HashClose(&c);
    return ok;
}
static BOOL HashOriginalFile(const WCHAR *path, BYTE digest[32])
{
    HANDLE f;
    BY_HANDLE_FILE_INFORMATION first, last;
    HashContext c;
    BYTE *buffer;
    DWORD count = 0, total = 0;
    BOOL ok = FALSE;
    /* Do not lock out Steam writers or integrity enforcement. Identity is
       checked before/after this bounded read; this is not an atomic snapshot. */
    f = CreateFileW(path, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    if (!GetFileInformationByHandle(f, &first) || first.nFileSizeHigh ||
        first.nFileSizeLow != 4968544u || !HashOpen(&c)) {
        CloseHandle(f); return FALSE;
    }
    buffer = (BYTE *)HeapAlloc(GetProcessHeap(), 0, 16384);
    if (buffer) {
        for (;;) {
            if (!ReadFile(f, buffer, 16384, &count, NULL)) break;
            if (!count) {
                if (total == 4968544u && GetFileInformationByHandle(f, &last) &&
                    first.dwVolumeSerialNumber == last.dwVolumeSerialNumber &&
                    first.nFileIndexHigh == last.nFileIndexHigh &&
                    first.nFileIndexLow == last.nFileIndexLow &&
                    first.nFileSizeLow == last.nFileSizeLow && !last.nFileSizeHigh &&
                    first.ftLastWriteTime.dwHighDateTime == last.ftLastWriteTime.dwHighDateTime &&
                    first.ftLastWriteTime.dwLowDateTime == last.ftLastWriteTime.dwLowDateTime)
                    ok = BCryptFinishHash(c.hash, digest, 32, 0) >= 0;
                break;
            }
            if (count > 4968544u - total || BCryptHashData(c.hash, buffer, count, 0) < 0)
                break;
            total += count;
        }
        HeapFree(GetProcessHeap(), 0, buffer);
    }
    HashClose(&c);
    CloseHandle(f);
    return ok;
}

static BOOL ImageRange(ULONG_PTR base, DWORD rva, DWORD bytes, BOOL executable)
{
    ULONG_PTR cursor, end, next;
    MEMORY_BASIC_INFORMATION m;
    DWORD p;
    if (!bytes || rva > TENKA_IMAGE_SIZE || bytes > TENKA_IMAGE_SIZE - rva)
        return FALSE;
    cursor = base + rva;
    end = cursor + bytes;
    if (cursor < base || end < cursor) return FALSE;
    while (cursor < end) {
        if (VirtualQuery((const void *)cursor, &m, sizeof(m)) != sizeof(m) ||
            m.State != MEM_COMMIT || m.Type != MEM_IMAGE ||
            (ULONG_PTR)m.AllocationBase != base || (m.Protect & PAGE_GUARD)) return FALSE;
        p = m.Protect & 0xff;
        if (p != PAGE_READONLY && p != PAGE_READWRITE && p != PAGE_WRITECOPY &&
            p != PAGE_EXECUTE_READ && p != PAGE_EXECUTE_READWRITE &&
            p != PAGE_EXECUTE_WRITECOPY) return FALSE;
        if (executable && p != PAGE_EXECUTE_READ && p != PAGE_EXECUTE_READWRITE &&
            p != PAGE_EXECUTE_WRITECOPY) return FALSE;
        next = (ULONG_PTR)m.BaseAddress + m.RegionSize;
        if (next <= cursor) return FALSE;
        cursor = next;
    }
    return TRUE;
}
static BOOL CheckImage(ULONG_PTR base)
{
    const IMAGE_DOS_HEADER *dos;
    const IMAGE_NT_HEADERS32 *nt;
    if (base != TENKA_IMAGE_BASE || !ImageRange(base, 0, sizeof(*dos), FALSE)) return FALSE;
    dos = (const IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        (DWORD)dos->e_lfanew > 4096u - sizeof(*nt) ||
        !ImageRange(base, (DWORD)dos->e_lfanew, sizeof(*nt), FALSE)) return FALSE;
    nt = (const IMAGE_NT_HEADERS32 *)(base + (DWORD)dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE &&
           nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
           nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
           nt->OptionalHeader.SizeOfImage == TENKA_IMAGE_SIZE;
}
static BOOL CheckCode(ULONG_PTR base, DWORD rva, DWORD size, const BYTE digest[32])
{
    BYTE found[32];
    return ImageRange(base, rva, size, TRUE) &&
           HashMemory((const BYTE *)(base + rva), size, found) &&
           EqualBytes(found, digest, 32);
}
static BOOL WritableRange(ULONG_PTR address, DWORD bytes, ULONG_PTR image_base)
{
    MEMORY_BASIC_INFORMATION m;
    ULONG_PTR end = address + bytes, next;
    DWORD protection;
    if (!address || !bytes || end < address) return FALSE;
    while (address < end) {
        if (VirtualQuery((const void *)address, &m, sizeof(m)) != sizeof(m) ||
            m.State != MEM_COMMIT || (m.Protect & PAGE_GUARD)) return FALSE;
        if (m.Type != MEM_PRIVATE &&
            !(m.Type == MEM_IMAGE && (ULONG_PTR)m.AllocationBase == image_base)) return FALSE;
        protection = m.Protect & 0xff;
        /* Refuse read-only and executable state pages; never change protection. */
        if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY) return FALSE;
        next = (ULONG_PTR)m.BaseAddress + m.RegionSize;
        if (next <= address) return FALSE;
        address = next;
    }
    return TRUE;
}
static BOOL MutableCrtRanges(ULONG_PTR base, const State *state)
{
    /* Known selector flag, global multibyte fields/table/case map, and the
       existing 0x220-byte multibyte-info object that the setter may reuse. */
    return WritableRange(base + 0x14a5514, 4, base) &&
           WritableRange(base + 0x14a6540, 0x240, base) &&
           (!state->info_pointer || WritableRange(state->info_pointer, 0x220, base));
}
static BOOL CaptureState(ULONG_PTR base, State *state)
{
    DWORD i, page_after, guard_after, flag_after, pointer_after;
    ZeroBytes(state, sizeof(*state));
    if (!ImageRange(base, 0x14a6664, 4, FALSE) ||
        !ImageRange(base, 0x14a67b8, 4, FALSE) ||
        !ImageRange(base, 0x14a5514, 4, FALSE) ||
        !ImageRange(base, 0x14a6544, 4, FALSE) ||
        !ImageRange(base, 0x14a6560, 257, FALSE)) return FALSE;
    state->page = *(volatile DWORD *)(base + 0x14a6664);
    state->guard = *(volatile DWORD *)(base + 0x14a67b8);
    state->selector_flag = *(volatile DWORD *)(base + 0x14a5514);
    state->info_pointer = *(volatile DWORD *)(base + 0x14a6544);
    for (i = 0; i < 257; ++i) state->table[i] = *(volatile BYTE *)(base + 0x14a6560 + i);
    page_after = *(volatile DWORD *)(base + 0x14a6664);
    guard_after = *(volatile DWORD *)(base + 0x14a67b8);
    flag_after = *(volatile DWORD *)(base + 0x14a5514);
    pointer_after = *(volatile DWORD *)(base + 0x14a6544);
    state->available = TRUE;
    state->stable = page_after == state->page && guard_after == state->guard &&
                    flag_after == state->selector_flag && pointer_after == state->info_pointer;
    return state->stable;
}
static BOOL JapanState(const State *s)
{
    return s->available && s->stable && s->guard == 1 && s->page == 932 &&
           EqualBytes(s->table, kJapanTable, sizeof(kJapanTable));
}

