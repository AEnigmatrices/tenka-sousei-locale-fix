#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>
#include "VerifiedInputs.h"
#if !defined(_M_IX86)
#error This prototype requires the native x86 compiler.
#endif
#pragma intrinsic(_ReturnAddress)

enum { EXP_SHIM, EXP_PIXEL, EXP_VERTEX, EXP_MUTE, EXP_CREATE, EXP_COUNT };
static const char *const kExports[EXP_COUNT] = {
    "Direct3D8EnableMaximizedWindowedModeShim", "ValidatePixelShader",
    "ValidateVertexShader", "DebugSetMute", "Direct3DCreate8"
};
static HMODULE g_self;
static HMODULE g_system;
static FARPROC g_real[EXP_COUNT];
static INIT_ONCE g_forwardOnce = INIT_ONCE_STATIC_INIT;
static INIT_ONCE g_gameOnce = INIT_ONCE_STATIC_INIT;
static BOOL g_forwardOK;
static BOOL g_gameOK;
static DWORD g_forwardError = ERROR_DLL_INIT_FAILED;
static DWORD g_gameError = ERROR_DLL_INIT_FAILED;

typedef struct HashContext {
    BCRYPT_ALG_HANDLE algorithm;
    BCRYPT_HASH_HANDLE hash;
    PUCHAR object;
} HashContext;
typedef struct State {
    DWORD page, guard, selector_flag, info_pointer;
    BYTE table[257];
    BOOL available, stable;
} State;
typedef struct Trial {
    ULONG_PTR base, caller;
    UINT sdk;
    State before, after;
    BYTE disk_hash[32];
    BOOL disk_hash_available, setter_called, precall_rechecked, mutable_ranges_verified;
    int setter_result;
    DWORD error;
    WCHAR image_path[MAX_PATH];
    const char *status;
} Trial;
typedef struct Text { char bytes[8192]; DWORD length; BOOL ok; } Text;
static Text g_text; /* Accessed only by the single game-init callback. */

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

#include "PortablePaths.h"

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

#include "ForwardingV2.h"
static BOOL EnsureForwarding(void)
{
    if (!InitOnceExecuteOnce(&g_forwardOnce, InitializeForwarding, NULL, NULL)) return FALSE;
    return g_forwardOK;
}
static void __cdecl RequireForwarding(void)
{
    DWORD saved = GetLastError();
    if (!EnsureForwarding()) {
        ULONG_PTR error = g_forwardError;
        /* An opaque ABI cannot be given an invented stack cleanup/return.
           Non-continuable failure is explicit rather than a NULL jump. */
        RaiseException(0xe0544b01u, EXCEPTION_NONCONTINUABLE, 1, &error);
        __assume(0);
    }
    SetLastError(saved);
}

static BOOL CALLBACK InitializeGame(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    Trial *trial = (Trial *)parameter;
    State refreshed;
    HANDLE log;
    DWORD length;
    typedef int (__cdecl *GameSetMbcp)(int);
    (void)once; (void)context;
    trial->status = "refused";
    trial->error = ERROR_REVISION_MISMATCH;
    log = OpenTrialLog();
    if (log == INVALID_HANDLE_VALUE) {
        g_gameError = GetLastError();
        return TRUE;
    }
    length = GetModuleFileNameW(NULL, trial->image_path, MAX_PATH);
    trial->image_path[MAX_PATH - 1] = 0;
    if (!length || length >= MAX_PATH || !PortableGameLocation(trial->image_path) ||
        trial->sdk != 0xdc || !CheckImage(trial->base)) goto done;
    if (!HashOriginalFile(trial->image_path, trial->disk_hash)) goto done;
    trial->disk_hash_available = TRUE;
    if (!EqualBytes(trial->disk_hash, kOriginalSha256, 32)) goto done;
    if (!CheckCode(trial->base, TENKA_SETTER_RVA, 327, kSetterSha256) ||
        !CheckCode(trial->base, 0x30f674, 13, kCallSiteASha256) ||
        !CheckCode(trial->base, 0x2fce59, 10, kCallSiteBSha256)) goto done;
    if (!CaptureState(trial->base, &trial->before)) goto done;
    if (JapanState(&trial->before)) {
        trial->status = "already_cp932";
        trial->error = ERROR_SUCCESS;
        g_gameOK = TRUE;
        goto done;
    }
    if (trial->before.guard != 1 || trial->before.page != 65001 ||
        !EqualBytes(trial->before.table, kNormalTable, sizeof(kNormalTable))) goto done;
    trial->status = "validated_before_setter";
    trial->error = ERROR_SUCCESS;
    if (!WriteRecord(log, trial, "intent")) {
        trial->error = ERROR_WRITE_FAULT;
        trial->status = "intent_log_failed_no_setter";
        goto done;
    }
    if (!CaptureState(trial->base, &refreshed) || refreshed.guard != 1 ||
        refreshed.page != 65001 ||
        refreshed.selector_flag != trial->before.selector_flag ||
        refreshed.info_pointer != trial->before.info_pointer ||
        !EqualBytes(refreshed.table, kNormalTable, sizeof(kNormalTable))) {
        trial->status = "precall_state_changed_no_setter";
        trial->error = ERROR_INVALID_DATA;
        goto done;
    }
    trial->precall_rechecked = TRUE;
    if (!MutableCrtRanges(trial->base, &refreshed)) {
        trial->status = "mutable_range_refused_no_setter";
        trial->error = ERROR_NOACCESS;
        goto done;
    }
    trial->mutable_ranges_verified = TRUE;
    /* The only intended game-state-changing operation in this prototype.
       No game instruction bytes, IAT slots, or global integers are patched. */
    trial->setter_called = TRUE;
    trial->setter_result = ((GameSetMbcp)(trial->base + TENKA_SETTER_RVA))(932);
    if (!CaptureState(trial->base, &trial->after) || trial->setter_result != 0 ||
        !JapanState(&trial->after)) {
        trial->status = "setter_or_postcheck_failed";
        trial->error = ERROR_INVALID_DATA;
        goto done;
    }
    trial->status = "cp932_table_verified";
    trial->error = ERROR_SUCCESS;
    g_gameOK = TRUE;
done:
    if (!WriteRecord(log, trial, "result")) {
        g_gameOK = FALSE;
        trial->error = ERROR_WRITE_FAULT;
    }
    g_gameError = trial->error;
    CloseHandle(log);
    return TRUE; /* Seal success/refusal; never retry a partial setter call. */
}

void *WINAPI ProxyDirect3DCreate8(UINT sdk)
{
    typedef void *(WINAPI *CreateFunction)(UINT);
    DWORD saved = GetLastError();
    Trial trial;
    ULONG_PTR caller = (ULONG_PTR)_ReturnAddress();
    ULONG_PTR base = (ULONG_PTR)GetModuleHandleW(NULL);
    if (!EnsureForwarding()) { SetLastError(g_forwardError); return NULL; }
    /* Only the two verified EXE call sites can trigger a CRT correction.
       Other callers are transparent graphics passthrough, not patch targets. */
    if (base == TENKA_IMAGE_BASE &&
        (caller == base + 0x30f681 || caller == base + 0x2fce63)) {
        ZeroBytes(&trial, sizeof(trial));
        trial.base = base; trial.caller = caller; trial.sdk = sdk;
        if (!InitOnceExecuteOnce(&g_gameOnce, InitializeGame, &trial, NULL) || !g_gameOK) {
            SetLastError(g_gameError); return NULL;
        }
    }
    SetLastError(saved);
    return ((CreateFunction)g_real[EXP_CREATE])(sdk);
}

/* Opaque exports are tail forwards. The original stack/arguments, general
   registers and EFLAGS reach the real function; x87/XMM preservation is not
   promised. The real function supplies the return value and callee cleanup.
   This avoids inventing a prototype for the undocumented shim export. */
__declspec(naked) void __cdecl ProxyShim(void)
{
    __asm { pushfd }
    __asm { pushad }
    __asm { call RequireForwarding }
    __asm { popad }
    __asm { popfd }
    __asm { jmp dword ptr [g_real + 0] }
}
__declspec(naked) void __cdecl ProxyValidatePixelShader(void)
{
    __asm { pushfd }
    __asm { pushad }
    __asm { call RequireForwarding }
    __asm { popad }
    __asm { popfd }
    __asm { jmp dword ptr [g_real + 4] }
}
__declspec(naked) void __cdecl ProxyValidateVertexShader(void)
{
    __asm { pushfd }
    __asm { pushad }
    __asm { call RequireForwarding }
    __asm { popad }
    __asm { popfd }
    __asm { jmp dword ptr [g_real + 8] }
}
__declspec(naked) void __cdecl ProxyDebugSetMute(void)
{
    __asm { pushfd }
    __asm { pushad }
    __asm { call RequireForwarding }
    __asm { popad }
    __asm { popfd }
    __asm { jmp dword ptr [g_real + 12] }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) g_self = instance;
    return TRUE;
}
