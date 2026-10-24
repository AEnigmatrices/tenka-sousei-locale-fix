enum { EXP_SHIM, EXP_PIXEL, EXP_VERTEX, EXP_MUTE, EXP_CREATE, EXP_COUNT };
static const char *const kExports[EXP_COUNT] = {
    "Direct3D8EnableMaximizedWindowedModeShim", "ValidatePixelShader",
    "ValidateVertexShader", "DebugSetMute", "Direct3DCreate8"
};
static FARPROC g_real[EXP_COUNT];
static INIT_ONCE g_forwardOnce = INIT_ONCE_STATIC_INIT;
static BOOL g_forwardOK;
static DWORD g_forwardError = ERROR_DLL_INIT_FAILED;

typedef struct ForwardFile {
    BY_HANDLE_FILE_INFORMATION identity;
    DWORD timestamp, image_bytes;
    WORD machine, magic;
    BOOL valid;
} ForwardFile;

static BOOL SameForwardFileId(const BY_HANDLE_FILE_INFORMATION *a,
                              const BY_HANDLE_FILE_INFORMATION *b)
{
    return a->dwVolumeSerialNumber == b->dwVolumeSerialNumber &&
           a->nFileIndexHigh == b->nFileIndexHigh && a->nFileIndexLow == b->nFileIndexLow;
}
static BOOL StableForwardFile(const BY_HANDLE_FILE_INFORMATION *a,
                              const BY_HANDLE_FILE_INFORMATION *b)
{
    return SameForwardFileId(a, b) && a->nFileSizeHigh == b->nFileSizeHigh &&
           a->nFileSizeLow == b->nFileSizeLow &&
           a->ftLastWriteTime.dwHighDateTime == b->ftLastWriteTime.dwHighDateTime &&
           a->ftLastWriteTime.dwLowDateTime == b->ftLastWriteTime.dwLowDateTime;
}
static BOOL InspectForwardFile(const WCHAR *path, ForwardFile *out, DWORD *error)
{
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION after;
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS32 nt;
    LARGE_INTEGER position;
    DWORD count, close_error;
    BOOL ok = FALSE;
    ZeroBytes(out, sizeof(*out)); *error = ERROR_BAD_EXE_FORMAT;
    /* Shared read-only inspection: never lock out servicing or integrity checks. */
    file = CreateFileW(path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) { *error = GetLastError(); return FALSE; }
    if (!GetFileInformationByHandle(file, &out->identity)) {
        *error = GetLastError(); goto done;
    }
    if ((out->identity.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        (!out->identity.nFileIndexHigh && !out->identity.nFileIndexLow) ||
        out->identity.nFileSizeHigh || out->identity.nFileSizeLow < sizeof(nt) + sizeof(dos) ||
        out->identity.nFileSizeLow > 256u * 1024u * 1024u) goto done;
    if (!ReadFile(file, &dos, sizeof(dos), &count, NULL)) {
        *error = GetLastError(); goto done;
    }
    if (count != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
        (DWORD)dos.e_lfanew > 4096u - sizeof(nt) ||
        (DWORD)dos.e_lfanew > out->identity.nFileSizeLow - sizeof(nt)) goto done;
    position.QuadPart = dos.e_lfanew;
    if (!SetFilePointerEx(file, position, NULL, FILE_BEGIN)) {
        *error = GetLastError(); goto done;
    }
    if (!ReadFile(file, &nt, sizeof(nt), &count, NULL)) {
        *error = GetLastError(); goto done;
    }
    if (count != sizeof(nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        !(nt.FileHeader.Characteristics & IMAGE_FILE_DLL) || !nt.OptionalHeader.SizeOfImage) goto done;
    out->machine = nt.FileHeader.Machine; out->magic = nt.OptionalHeader.Magic;
    out->timestamp = nt.FileHeader.TimeDateStamp; out->image_bytes = nt.OptionalHeader.SizeOfImage;
    if (!GetFileInformationByHandle(file, &after)) {
        *error = GetLastError(); goto done;
    }
    if (!StableForwardFile(&out->identity, &after)) { *error = ERROR_REVISION_MISMATCH; goto done; }
    ok = TRUE;
done:
    if (!CloseHandle(file)) {
        close_error = GetLastError();
        if (ok) *error = close_error;
        ok = FALSE;
    }
    out->valid = ok;
    return ok;
}
static BOOL MatchForwardImage(HMODULE module, const ForwardFile *file)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)module;
    const IMAGE_NT_HEADERS32 *nt;
    if (!module || !file->valid || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        (DWORD)dos->e_lfanew > 4096u - sizeof(*nt)) return FALSE;
    nt = (const IMAGE_NT_HEADERS32 *)((const BYTE *)module + (DWORD)dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == file->machine &&
           nt->OptionalHeader.Magic == file->magic &&
           nt->FileHeader.TimeDateStamp == file->timestamp &&
           nt->OptionalHeader.SizeOfImage == file->image_bytes &&
           (nt->FileHeader.Characteristics & IMAGE_FILE_DLL);
}

static BOOL CALLBACK InitializeForwarding(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    WCHAR directory[MAX_PATH], expected[MAX_PATH], actual[MAX_PATH], self[MAX_PATH];
    ForwardFile expected_file, actual_file, self_file, expected_after;
    HMODULE system;
    BOOL wow64 = FALSE;
    DWORD length, error = ERROR_DLL_INIT_FAILED, i;
    FARPROC by_ordinal;
    (void)once; (void)parameter; (void)context;
    if (!IsWow64Process(GetCurrentProcess(), &wow64)) {
        error = GetLastError(); goto finish;
    }
    length = wow64 ? GetSystemWow64DirectoryW(directory, MAX_PATH) : GetSystemDirectoryW(directory, MAX_PATH);
    if (!length) {
        error = GetLastError(); goto finish;
    }
    if (length >= MAX_PATH || !AppendWide(expected, MAX_PATH, directory, L"\\d3d8.dll")) {
        error = ERROR_BUFFER_OVERFLOW; goto finish;
    }
    length = GetModuleFileNameW(g_self, self, MAX_PATH);
    if (!length) {
        error = GetLastError(); goto finish;
    }
    if (length >= MAX_PATH) {
        self[MAX_PATH - 1] = 0; error = ERROR_BUFFER_OVERFLOW; goto finish;
    }
    if (!InspectForwardFile(self, &self_file, &error)) goto finish;
    if (!MatchForwardImage(g_self, &self_file)) {
        error = ERROR_REVISION_MISMATCH; goto finish;
    }
    if (!InspectForwardFile(expected, &expected_file, &error)) goto finish;
    if (SameForwardFileId(&expected_file.identity, &self_file.identity)) {
        error = ERROR_REVISION_MISMATCH; goto finish;
    }
    system = LoadLibraryExW(expected, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!system) {
        error = GetLastError(); goto finish;
    }
    if (system == g_self) {
        error = ERROR_REVISION_MISMATCH; goto finish;
    }
    length = GetModuleFileNameW(system, actual, MAX_PATH);
    if (!length) {
        error = GetLastError(); goto finish;
    }
    if (length >= MAX_PATH) {
        actual[MAX_PATH - 1] = 0; error = ERROR_BUFFER_OVERFLOW; goto finish;
    }
    if (!InspectForwardFile(actual, &actual_file, &error)) goto finish;
    if (!SameForwardFileId(&expected_file.identity, &actual_file.identity) ||
        SameForwardFileId(&actual_file.identity, &self_file.identity) ||
        !StableForwardFile(&expected_file.identity, &actual_file.identity)) {
        error = ERROR_REVISION_MISMATCH; goto finish;
    }
    if (!MatchForwardImage(system, &actual_file)) {
        error = ERROR_REVISION_MISMATCH; goto finish;
    }
    if (!InspectForwardFile(expected, &expected_after, &error)) goto finish;
    if (!StableForwardFile(&expected_file.identity, &expected_after.identity) ||
        !MatchForwardImage(system, &expected_after)) {
        error = ERROR_REVISION_MISMATCH; goto finish;
    }
    for (i = 0; i < EXP_COUNT; ++i) {
        g_real[i] = GetProcAddress(system, kExports[i]);
        if (!g_real[i]) {
            error = GetLastError(); goto finish;
        }
        by_ordinal = GetProcAddress(system, (LPCSTR)(ULONG_PTR)(i + 1));
        if (!by_ordinal) {
            error = GetLastError(); goto finish;
        }
        if (g_real[i] != by_ordinal) {
            error = ERROR_REVISION_MISMATCH; goto finish;
        }
    }
    g_forwardOK = TRUE;
finish:
    g_forwardError = g_forwardOK ? ERROR_SUCCESS : (error ? error : ERROR_DLL_INIT_FAILED);
    return TRUE; /* Cache forwarding success or refusal. */
}

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

void *WINAPI ProxyDirect3DCreate8(UINT sdk)
{
    typedef void *(WINAPI *CreateFunction)(UINT);
    DWORD saved = GetLastError();
    ULONG_PTR caller = (ULONG_PTR)_ReturnAddress();
    ULONG_PTR base = (ULONG_PTR)GetModuleHandleW(NULL);
    if (!EnsureForwarding()) { SetLastError(g_forwardError); return NULL; }
    /* Only the two verified EXE call sites can trigger a CRT correction.
       Other callers are transparent graphics passthrough, not patch targets. */
    if (base == TENKA_IMAGE_BASE &&
        (caller == base + TENKA_CALLSITE_A_RETURN_RVA ||
         caller == base + TENKA_CALLSITE_B_RETURN_RVA)) {
        if (!EnsureGameLocale(base, sdk)) {
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
