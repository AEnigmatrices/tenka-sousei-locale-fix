typedef struct ForwardFile {
    BY_HANDLE_FILE_INFORMATION identity;
    DWORD timestamp, image_bytes, close_error;
    WORD machine, magic;
    BOOL metadata_available, valid;
} ForwardFile;
typedef struct ForwardDiagnostic {
    WCHAR expected[MAX_PATH], actual[MAX_PATH], self[MAX_PATH];
    ForwardFile expected_file, actual_file, self_file, expected_after;
    DWORD sequence, return_error, api_error, export_ordinal;
    const char *stage, *detail;
    BOOL api_error_available;
    DWORD status; /* 0=pending,1=success,2=refused */
} ForwardDiagnostic;
static ForwardDiagnostic g_forwardDiagnostic;
static Text g_forwardText;

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
static void ForwardFailure(const char *stage, const char *detail,
                           DWORD return_error, BOOL api, DWORD api_error)
{
    g_forwardDiagnostic.status = 2;
    g_forwardDiagnostic.stage = stage;
    g_forwardDiagnostic.detail = detail;
    g_forwardDiagnostic.return_error = return_error ? return_error : ERROR_DLL_INIT_FAILED;
    g_forwardDiagnostic.api_error_available = api;
    g_forwardDiagnostic.api_error = api_error;
}
static BOOL InspectForwardFile(const WCHAR *path, ForwardFile *out,
                               const char **detail, BOOL *api, DWORD *error)
{
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION after;
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS32 nt;
    LARGE_INTEGER position;
    DWORD count;
    BOOL ok = FALSE;
    ZeroBytes(out, sizeof(*out)); *api = FALSE; *error = ERROR_BAD_EXE_FORMAT;
    *detail = "CreateFileW";
    /* Shared read-only inspection: never lock out servicing or integrity checks. */
    file = CreateFileW(path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) { *error = GetLastError(); *api = TRUE; return FALSE; }
    *detail = "GetFileInformationByHandle.before";
    if (!GetFileInformationByHandle(file, &out->identity)) {
        *error = GetLastError(); *api = TRUE; goto done;
    }
    out->metadata_available = TRUE;
    *detail = "ordinary_bounded_file";
    if ((out->identity.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        (!out->identity.nFileIndexHigh && !out->identity.nFileIndexLow) ||
        out->identity.nFileSizeHigh || out->identity.nFileSizeLow < sizeof(nt) + sizeof(dos) ||
        out->identity.nFileSizeLow > 256u * 1024u * 1024u) goto done;
    *detail = "ReadFile.DOS";
    if (!ReadFile(file, &dos, sizeof(dos), &count, NULL)) {
        *error = GetLastError(); *api = TRUE; goto done;
    }
    *detail = "DOS_header";
    if (count != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
        (DWORD)dos.e_lfanew > 4096u - sizeof(nt) ||
        (DWORD)dos.e_lfanew > out->identity.nFileSizeLow - sizeof(nt)) goto done;
    position.QuadPart = dos.e_lfanew; *detail = "SetFilePointerEx";
    if (!SetFilePointerEx(file, position, NULL, FILE_BEGIN)) {
        *error = GetLastError(); *api = TRUE; goto done;
    }
    *detail = "ReadFile.NT";
    if (!ReadFile(file, &nt, sizeof(nt), &count, NULL)) {
        *error = GetLastError(); *api = TRUE; goto done;
    }
    *detail = "PE32_I386_DLL";
    if (count != sizeof(nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        !(nt.FileHeader.Characteristics & IMAGE_FILE_DLL) || !nt.OptionalHeader.SizeOfImage) goto done;
    out->machine = nt.FileHeader.Machine; out->magic = nt.OptionalHeader.Magic;
    out->timestamp = nt.FileHeader.TimeDateStamp; out->image_bytes = nt.OptionalHeader.SizeOfImage;
    *detail = "GetFileInformationByHandle.after";
    if (!GetFileInformationByHandle(file, &after)) {
        *error = GetLastError(); *api = TRUE; goto done;
    }
    *detail = "file_changed_during_read";
    if (!StableForwardFile(&out->identity, &after)) { *error = ERROR_REVISION_MISMATCH; goto done; }
    ok = TRUE;
done:
    if (!CloseHandle(file)) {
        out->close_error = GetLastError();
        if (ok) { *detail = "CloseHandle"; *api = TRUE; *error = out->close_error; }
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

/* All file, mapped-image, export name and ordinal checks remain enabled.
 * Only the diagnostic serialization/file I/O path is removed.
 */
static BOOL CALLBACK InitializeForwarding(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    ForwardDiagnostic *d = &g_forwardDiagnostic;
    WCHAR directory[MAX_PATH];
    BOOL wow64 = FALSE, api;
    DWORD length, error, i;
    const char *detail;
    FARPROC by_ordinal;
    (void)once; (void)parameter; (void)context;
    ZeroBytes(d, sizeof(*d));
    d->stage = "begin";
    d->detail = "initialization";
    if (!IsWow64Process(GetCurrentProcess(), &wow64)) {
        error = GetLastError(); ForwardFailure("architecture_query", "IsWow64Process", error, TRUE, error); goto finish;
    }
    length = wow64 ? GetSystemWow64DirectoryW(directory, MAX_PATH) : GetSystemDirectoryW(directory, MAX_PATH);
    if (!length) {
        error = GetLastError(); ForwardFailure("system_directory", "GetSystemDirectory_API", error, TRUE, error); goto finish;
    }
    if (length >= MAX_PATH || !AppendWide(d->expected, MAX_PATH, directory, L"\\d3d8.dll")) {
        ForwardFailure("system_directory", "path_capacity", ERROR_BUFFER_OVERFLOW, FALSE, 0); goto finish;
    }
    length = GetModuleFileNameW(g_self, d->self, MAX_PATH);
    if (!length) {
        error = GetLastError(); ForwardFailure("self_path", "GetModuleFileNameW", error, TRUE, error); goto finish;
    }
    if (length >= MAX_PATH) {
        d->self[MAX_PATH - 1] = 0; ForwardFailure("self_path", "truncated", ERROR_BUFFER_OVERFLOW, FALSE, 0); goto finish;
    }
    if (!InspectForwardFile(d->self, &d->self_file, &detail, &api, &error)) {
        ForwardFailure("self_file", detail, error, api, api ? error : 0); goto finish;
    }
    if (!MatchForwardImage(g_self, &d->self_file)) {
        ForwardFailure("self_image", "PE_headers_disagree", ERROR_REVISION_MISMATCH, FALSE, 0); goto finish;
    }
    if (!InspectForwardFile(d->expected, &d->expected_file, &detail, &api, &error)) {
        ForwardFailure("expected_file", detail, error, api, api ? error : 0); goto finish;
    }
    if (SameForwardFileId(&d->expected_file.identity, &d->self_file.identity)) {
        ForwardFailure("self_identity", "expected_file_is_proxy", ERROR_REVISION_MISMATCH, FALSE, 0); goto finish;
    }
    g_system = LoadLibraryExW(d->expected, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!g_system) {
        error = GetLastError(); ForwardFailure("system_load", "LoadLibraryExW", error, TRUE, error); goto finish;
    }
    if (g_system == g_self) {
        ForwardFailure("self_module", "loaded_proxy_handle", ERROR_REVISION_MISMATCH, FALSE, 0); goto finish;
    }
    length = GetModuleFileNameW(g_system, d->actual, MAX_PATH);
    if (!length) {
        error = GetLastError(); ForwardFailure("reported_path", "GetModuleFileNameW", error, TRUE, error); goto finish;
    }
    if (length >= MAX_PATH) {
        d->actual[MAX_PATH - 1] = 0; ForwardFailure("reported_path", "truncated", ERROR_BUFFER_OVERFLOW, FALSE, 0); goto finish;
    }
    if (!InspectForwardFile(d->actual, &d->actual_file, &detail, &api, &error)) {
        ForwardFailure("reported_file", detail, error, api, api ? error : 0); goto finish;
    }
    if (!SameForwardFileId(&d->expected_file.identity, &d->actual_file.identity) ||
        SameForwardFileId(&d->actual_file.identity, &d->self_file.identity) ||
        !StableForwardFile(&d->expected_file.identity, &d->actual_file.identity)) {
        ForwardFailure("identity_compare", "unexpected_or_changed_file", ERROR_REVISION_MISMATCH, FALSE, 0); goto finish;
    }
    if (!MatchForwardImage(g_system, &d->actual_file)) {
        ForwardFailure("mapped_image", "PE_headers_disagree", ERROR_REVISION_MISMATCH, FALSE, 0); goto finish;
    }
    if (!InspectForwardFile(d->expected, &d->expected_after, &detail, &api, &error)) {
        ForwardFailure("expected_recheck", detail, error, api, api ? error : 0); goto finish;
    }
    if (!StableForwardFile(&d->expected_file.identity, &d->expected_after.identity) ||
        !MatchForwardImage(g_system, &d->expected_after)) {
        ForwardFailure("expected_recheck", "file_changed_since_preload", ERROR_REVISION_MISMATCH, FALSE, 0); goto finish;
    }
    for (i = 0; i < EXP_COUNT; ++i) {
        d->export_ordinal = i + 1;
        g_real[i] = GetProcAddress(g_system, kExports[i]);
        if (!g_real[i]) {
            error = GetLastError(); ForwardFailure("export_name", kExports[i], error, TRUE, error); goto finish;
        }
        by_ordinal = GetProcAddress(g_system, (LPCSTR)(ULONG_PTR)(i + 1));
        if (!by_ordinal) {
            error = GetLastError(); ForwardFailure("export_ordinal", kExports[i], error, TRUE, error); goto finish;
        }
        if (g_real[i] != by_ordinal) {
            ForwardFailure("export_compare", kExports[i], ERROR_REVISION_MISMATCH, FALSE, 0); goto finish;
        }
    }
    d->status = 1; d->stage = "forward_ready"; d->detail = "identity_PE_and_exports_verified";
finish:
    g_forwardOK = d->status == 1;
    g_forwardError = g_forwardOK ? ERROR_SUCCESS : d->return_error;
    return TRUE;
}
