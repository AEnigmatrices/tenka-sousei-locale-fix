/* Exercise the production initializer with controlled game and CRT inputs. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../source/support.inc"
#include "../source/compat/verified_inputs.h"

/* Real CRT-state helpers must reject unavailable memory before reading it. */
static BOOL ImageRange(ULONG_PTR base, DWORD rva, DWORD bytes, BOOL executable)
{
    (void)base; (void)rva; (void)bytes; (void)executable;
    return FALSE;
}
static BOOL WritableRange(ULONG_PTR address, DWORD bytes, ULONG_PTR base)
{
    (void)address; (void)bytes; (void)base;
    return FALSE;
}
#include "../source/compat/crt_state.inc"

enum Scenario {
    FIX_UTF8, ALREADY_JAPANESE, PATH_FAILED, PATH_TRUNCATED, WRONG_LOCATION,
    WRONG_SDK, WRONG_IMAGE, FILE_FAILED, WRONG_HASH, SETTER_CODE_CHANGED,
    CALLER_A_CHANGED, CALLER_B_CHANGED, BEFORE_UNREADABLE, BEFORE_GUARD,
    BEFORE_CODEPAGE, BEFORE_TABLE, CURRENT_UNREADABLE, CURRENT_GUARD,
    CURRENT_CODEPAGE, CURRENT_SELECTOR, CURRENT_POINTER, CURRENT_TABLE,
    NOT_WRITABLE, SETTER_FAILED, AFTER_UNREADABLE, AFTER_GUARD, AFTER_CODEPAGE,
    AFTER_TABLE
};
static enum Scenario scenario;
static unsigned image_calls, file_calls, code_calls, capture_calls, setter_calls;

static int __cdecl TestSetter(int page)
{
    ++setter_calls;
    if (page != TENKA_JAPANESE_CODEPAGE) return -1;
    return scenario == SETTER_FAILED ? -1 : 0;
}
static ULONG_PTR TestBase(void)
{
    return (ULONG_PTR)TestSetter - TENKA_SETTER_RVA;
}
static DWORD WINAPI TestModuleFileName(HMODULE module, WCHAR *path, DWORD capacity)
{
    (void)module;
    if (scenario == PATH_FAILED) return 0;
    if (scenario == PATH_TRUNCATED) return capacity;
    lstrcpynW(path, L"C:\\game\\Nobunaga11WPK.exe", (int)capacity);
    return (DWORD)lstrlenW(path);
}
static BOOL IsGameLocation(const WCHAR *path)
{
    (void)path;
    return scenario != WRONG_LOCATION;
}
static BOOL CheckImage(ULONG_PTR base)
{
    ++image_calls;
    return base == TestBase() && scenario != WRONG_IMAGE;
}
static BOOL HashOriginalFile(const WCHAR *path, BYTE digest[32])
{
    (void)path;
    ++file_calls;
    memcpy(digest, kOriginalSha256, 32);
    if (scenario == WRONG_HASH) digest[0] ^= 1;
    return scenario != FILE_FAILED;
}
static BOOL MatchesCodeFingerprint(ULONG_PTR base, DWORD rva, DWORD bytes, const BYTE digest[32])
{
    (void)base; (void)rva; (void)bytes; (void)digest;
    ++code_calls;
    return !((scenario == SETTER_CODE_CHANGED && code_calls == 1) ||
             (scenario == CALLER_A_CHANGED && code_calls == 2) ||
             (scenario == CALLER_B_CHANGED && code_calls == 3));
}
static BOOL TestCaptureState(ULONG_PTR base, CrtState *state)
{
    BOOL japanese;
    (void)base;
    ++capture_calls;
    if ((capture_calls == 1 && scenario == BEFORE_UNREADABLE) ||
        (capture_calls == 2 && scenario == CURRENT_UNREADABLE) ||
        (capture_calls == 3 && scenario == AFTER_UNREADABLE)) return FALSE;
    japanese = scenario == ALREADY_JAPANESE || capture_calls == 3;
    ZeroBytes(state, sizeof(*state));
    state->available = state->stable = TRUE;
    state->page = japanese ? TENKA_JAPANESE_CODEPAGE : TENKA_UTF8_CODEPAGE;
    state->guard = TENKA_CRT_READY;
    state->selector_flag = 4;
    state->info_pointer = 0x1234;
    memcpy(state->table, japanese ? kJapanTable : kNormalTable, sizeof(state->table));
    if ((capture_calls == 1 && scenario == BEFORE_GUARD) ||
        (capture_calls == 2 && scenario == CURRENT_GUARD) ||
        (capture_calls == 3 && scenario == AFTER_GUARD)) state->guard = 0;
    if ((capture_calls == 1 && scenario == BEFORE_CODEPAGE) ||
        (capture_calls == 2 && scenario == CURRENT_CODEPAGE) ||
        (capture_calls == 3 && scenario == AFTER_CODEPAGE)) state->page = 1252;
    if ((capture_calls == 1 && scenario == BEFORE_TABLE) ||
        (capture_calls == 2 && scenario == CURRENT_TABLE) ||
        (capture_calls == 3 && scenario == AFTER_TABLE)) state->table[1] ^= 1;
    if (capture_calls == 2 && scenario == CURRENT_SELECTOR) ++state->selector_flag;
    if (capture_calls == 2 && scenario == CURRENT_POINTER) ++state->info_pointer;
    return TRUE;
}
static BOOL TestMutableRanges(ULONG_PTR base, const CrtState *state)
{
    (void)base; (void)state;
    return scenario != NOT_WRITABLE;
}

#define GetModuleFileNameW TestModuleFileName
#define CaptureCrtState TestCaptureState
#define MutableCrtRanges TestMutableRanges
#include "../source/compat/locale_fix.inc"
#undef GetModuleFileNameW
#undef CaptureCrtState
#undef MutableCrtRanges

#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

int main(void)
{
    static const struct {
        enum Scenario scenario;
        DWORD error;
        unsigned setters;
    } cases[] = {
        { FIX_UTF8, ERROR_SUCCESS, 1 },
        { ALREADY_JAPANESE, ERROR_SUCCESS, 0 },
        { PATH_FAILED, ERROR_REVISION_MISMATCH, 0 },
        { PATH_TRUNCATED, ERROR_REVISION_MISMATCH, 0 },
        { WRONG_LOCATION, ERROR_REVISION_MISMATCH, 0 },
        { WRONG_SDK, ERROR_REVISION_MISMATCH, 0 },
        { WRONG_IMAGE, ERROR_REVISION_MISMATCH, 0 },
        { FILE_FAILED, ERROR_REVISION_MISMATCH, 0 },
        { WRONG_HASH, ERROR_REVISION_MISMATCH, 0 },
        { SETTER_CODE_CHANGED, ERROR_REVISION_MISMATCH, 0 },
        { CALLER_A_CHANGED, ERROR_REVISION_MISMATCH, 0 },
        { CALLER_B_CHANGED, ERROR_REVISION_MISMATCH, 0 },
        { BEFORE_UNREADABLE, ERROR_REVISION_MISMATCH, 0 },
        { BEFORE_GUARD, ERROR_REVISION_MISMATCH, 0 },
        { BEFORE_CODEPAGE, ERROR_REVISION_MISMATCH, 0 },
        { BEFORE_TABLE, ERROR_REVISION_MISMATCH, 0 },
        { CURRENT_UNREADABLE, ERROR_INVALID_DATA, 0 },
        { CURRENT_GUARD, ERROR_INVALID_DATA, 0 },
        { CURRENT_CODEPAGE, ERROR_INVALID_DATA, 0 },
        { CURRENT_SELECTOR, ERROR_INVALID_DATA, 0 },
        { CURRENT_POINTER, ERROR_INVALID_DATA, 0 },
        { CURRENT_TABLE, ERROR_INVALID_DATA, 0 },
        { NOT_WRITABLE, ERROR_NOACCESS, 0 },
        { SETTER_FAILED, ERROR_INVALID_DATA, 1 },
        { AFTER_UNREADABLE, ERROR_INVALID_DATA, 1 },
        { AFTER_GUARD, ERROR_INVALID_DATA, 1 },
        { AFTER_CODEPAGE, ERROR_INVALID_DATA, 1 },
        { AFTER_TABLE, ERROR_INVALID_DATA, 1 }
    };
    unsigned i;
    CrtState unreadable;
    CHECK(!CaptureCrtState(0, &unreadable));
    CHECK(!unreadable.available && !unreadable.stable);
    CHECK(!MutableCrtRanges(0, &unreadable));
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        BOOL expected = cases[i].error == ERROR_SUCCESS;
        unsigned images, files, codes, captures;
        scenario = cases[i].scenario;
        image_calls = file_calls = code_calls = capture_calls = setter_calls = 0;
        InitOnceInitialize(&g_localeInitOnce);
        g_localeInitSucceeded = FALSE;
        g_localeInitError = ERROR_DLL_INIT_FAILED;
        CHECK(EnsureGameLocale(TestBase(), scenario == WRONG_SDK ? 0 : TENKA_D3D_SDK_VERSION) == expected);
        CHECK(g_localeInitError == cases[i].error);
        CHECK(setter_calls == cases[i].setters);
        images = image_calls; files = file_calls; codes = code_calls; captures = capture_calls;
        /* Changing the inputs must not retry a completed success or refusal. */
        scenario = expected ? WRONG_IMAGE : FIX_UTF8;
        CHECK(EnsureGameLocale(TestBase(), TENKA_D3D_SDK_VERSION) == expected);
        CHECK(g_localeInitError == cases[i].error);
        CHECK(setter_calls == cases[i].setters);
        CHECK(image_calls == images && file_calls == files &&
              code_calls == codes && capture_calls == captures);
    }
    printf("Locale initialization: %u scenarios and cached results passed\n", i);
    return 0;
}
