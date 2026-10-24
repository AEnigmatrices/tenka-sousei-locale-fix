typedef struct GameInitContext {
    ULONG_PTR base;
    UINT sdk;
} GameInitContext;

static INIT_ONCE g_gameOnce = INIT_ONCE_STATIC_INIT;
static BOOL g_gameOK;
static DWORD g_gameError = ERROR_DLL_INIT_FAILED;

static BOOL CALLBACK InitializeGame(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    const GameInitContext *init = (const GameInitContext *)parameter;
    ULONG_PTR base = init->base;
    CrtState before, current;
    BYTE disk_hash[32];
    WCHAR image_path[MAX_PATH];
    DWORD length, error = ERROR_REVISION_MISMATCH;
    int setter_result;
    typedef int (__cdecl *GameSetMbcp)(int);
    (void)once; (void)context;
    length = GetModuleFileNameW(NULL, image_path, MAX_PATH);
    image_path[MAX_PATH - 1] = 0;
    if (!length || length >= MAX_PATH || !IsGameLocation(image_path) ||
        init->sdk != TENKA_D3D_SDK_VERSION || !CheckImage(base)) goto done;
    if (!HashOriginalFile(image_path, disk_hash)) goto done;
    if (!EqualBytes(disk_hash, kOriginalSha256, 32)) goto done;
    if (!CheckCode(base, TENKA_SETTER_RVA, TENKA_SETTER_SIZE, kSetterSha256) ||
        !CheckCode(base, TENKA_CALLSITE_A_RVA, TENKA_CALLSITE_A_SIZE, kCallSiteASha256) ||
        !CheckCode(base, TENKA_CALLSITE_B_RVA, TENKA_CALLSITE_B_SIZE, kCallSiteBSha256)) goto done;
    if (!CaptureCrtState(base, &before)) goto done;
    if (IsJapaneseCrtState(&before)) {
        error = ERROR_SUCCESS;
        g_gameOK = TRUE;
        goto done;
    }
    if (before.guard != TENKA_CRT_READY || before.page != TENKA_UTF8_CODEPAGE ||
        !EqualBytes(before.table, kNormalTable, sizeof(kNormalTable))) goto done;
    /* The verified CRT state must still hold immediately before mutation. */
    if (!CaptureCrtState(base, &current) || current.guard != TENKA_CRT_READY ||
        current.page != TENKA_UTF8_CODEPAGE ||
        current.selector_flag != before.selector_flag ||
        current.info_pointer != before.info_pointer ||
        !EqualBytes(current.table, kNormalTable, sizeof(kNormalTable))) {
        error = ERROR_INVALID_DATA;
        goto done;
    }
    if (!MutableCrtRanges(base, &current)) {
        error = ERROR_NOACCESS;
        goto done;
    }
    setter_result = ((GameSetMbcp)(base + TENKA_SETTER_RVA))(TENKA_JAPANESE_CODEPAGE);
    if (!CaptureCrtState(base, &current) || setter_result != 0 ||
        !IsJapaneseCrtState(&current)) {
        error = ERROR_INVALID_DATA;
        goto done;
    }
    error = ERROR_SUCCESS;
    g_gameOK = TRUE;
done:
    g_gameError = error;
    return TRUE; /* Seal success/refusal; never retry a partial setter call. */
}

static BOOL EnsureGameLocale(ULONG_PTR base, UINT sdk)
{
    GameInitContext init;
    init.base = base;
    init.sdk = sdk;
    if (!InitOnceExecuteOnce(&g_gameOnce, InitializeGame, &init, NULL)) return FALSE;
    return g_gameOK;
}
