/* Tenka Sousei Direct3D 8 compatibility proxy.
 * Component implementations remain in a single translation unit to preserve
 * the verified x86 forwarding ABI and initialization ordering.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>
#include "compat/verified_inputs.h"
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

#include "platform/portable_paths.inc"
#include "compat/integrity.inc"
#include "diagnostics/crt_log.inc"
#include "graphics/direct3d8_forwarding.inc"
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
