# Compatibility and forwarding contracts

This document records the invariants of the **32-bit Windows Direct3D 8 proxy** in
`d3d8.dll`. It describes the *existing* implementation; it is not an invitation
to broaden the supported executable or weaken validation for other game builds.

## Where the fix runs

1. Windows loads the game-local `d3d8.dll` in place of resolving the usual
   Direct3D 8 dependency directly to the system DLL.
2. `DllMain` only remembers the proxy module handle on process attach. Do
   not move file I/O, hashing, or runtime initialization under the loader lock.
3. The exported `ProxyDirect3DCreate8` checks graphics forwarding before
   deciding whether a game-locale correction is applicable.
4. The caller address is captured at the exported entry point using
   `_ReturnAddress()`. Only the two verified return addresses inside the
   expected image, combined with the expected image base, may enter the
   locale-initialization path. Other callers pass through to Windows Direct3D.

The DLL **does not set the Windows system locale** and **does not modify the
game executable on disk**.

## The locale initialization contract

For a recognized caller, `InitializeGameLocale` checks the following in
order; each later operation depends on the earlier checks succeeding.

| Stage | Required invariant |
| --- | --- |
| Location | Game executable is `Nobunaga11WPK.exe` and the proxy is the sibling `d3d8.dll` at a validated absolute path |
| Startup | The caller passes the known Direct3D 8 SDK version |
| Image | Executable is the expected x86 image, at its verified image base and image size |
| Disk | Executable file size and SHA-256 match the recorded build; the file identity is stable during the bounded read |
| Code | Three executable-memory regions match the recorded setter and call-site fingerprints |
| Initial CRT | Captured state is readable and stable; either CP932 with the expected Japanese character table, or CP65001 with the known initial table and ready guard |
| Before mutation | CRT state is re-read; guard, code page, selector, info pointer, and table still match the expected preconditions |
| Writable memory | Known mutable CRT ranges have suitable writable protections; protections are not changed by the proxy |
| Setter | Call the **game's existing** CRT multibyte-code-page setter with `932` exactly once |
| Postcondition | Setter returns success and another CRT state capture confirms CP932 and the expected Japanese character table |

If the initial CRT state is already verified CP932, the setter is not called.
The setter is **never** invoked for other initial code pages, malformed tables,
unrecognized executables, unstable state, or unsupported call sites.

The offsets, region sizes, hashes, CP numbers, and recorded CRT tables are
build-specific values in `compat/verified_inputs.h` (with table data in `compat/crt_tables.h`); they are not generic
Windows addresses or signatures for arbitrary game revisions.

## One-time outcomes and errors

`InitOnceExecuteOnce` protects *both* the graphics forwarding initializer and
the locale initializer. The callbacks return TRUE even when their checks
refuse initialization, recording the failed outcome. This is deliberate:
**a failed or partially completed CRT setter call must never be retried.**

For a recognized game caller, locale refusal causes
`ProxyDirect3DCreate8` to return NULL with its cached error. Unrecognized
callers do not enter that correction path. Successful forwarding restores
the calling thread's prior Win32 last-error value.

Do not reorder the state capture, writable-range validation, setter call, or
postcondition verification. Do not replace fail-closed behavior with a
best-effort write.

## Direct3D forwarding and x86 ABI

`graphics/direct3d8_forwarding.inc` locates the genuine system `d3d8.dll`
using the Windows system directory (including SysWOW64 where appropriate).
It checks image identity, verifies all five exports by **name and ordinal**,
and caches the forwarding result.

`d3d8.def` fixes export ordinals **1 through 5**. The four opaque exports use
x86 assembly tail-jump stubs instead of guessed C prototypes. Their original
stack, general-purpose registers, and EFLAGS reach the real export; these
stubs do not promise x87/XMM preservation. The native forwarding smoke test
also checks their expected instruction layout.

Do not introduce additional stack adjustments, invent signatures for opaque
exports, replace the tail jumps with ordinary C calls, or change the x86 build
target without separately proving ABI compatibility.

## Compilation and testing

`D3D8Proxy.c` includes `.inc` implementation components into a **single C
translation unit**. This is intentional, including the absence of a separate
C runtime link dependency. It should not be replaced with C++ or multiple
link units merely for cosmetic organization.

Use `source\build.cmd` and `tests\run.cmd` in a Visual Studio x86 Native
Tools environment. The native suite currently exercises **28**
locale-initialization scenarios, one-time results, path validation,
real memory-protection and code-fingerprint guards, export names/ordinals,
opaque thunk bytes, and Direct3D 8 forwarding. Most locale inputs are
controlled test doubles; passing them is not proof of compatibility with an
unrecognized executable or Windows environment.

Changes to symbol names and comments should keep the above contracts
unchanged. Always run the native checks before accepting code changes.
