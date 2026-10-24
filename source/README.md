# Source code

This is the implementation of the **x86 Direct3D 8 proxy** distributed as `d3d8.dll`.

`D3D8Proxy.c` includes the implementation components into one C translation unit. The `.inc` extension distinguishes these implementation files from public headers.

| File | Responsibility |
| --- | --- |
| `D3D8Proxy.c` | Component include order and DLL entry point |
| `support.inc` | Byte and bounded string helpers without a separate C runtime |
| `compat/verified_inputs.h` | Named game offsets, sizes, code fingerprints, and CRT tables |
| `compat/integrity.inc` | File hashing, image-memory checks, and writable-range validation |
| `compat/crt_state.inc` | CRT state snapshots and multibyte-state validation |
| `compat/locale_fix.inc` | One-time CP932 initialization and cached results |
| `graphics/direct3d8_forwarding.inc` | Windows D3D8 loading, exports, and ABI-sensitive x86 forwarding |
| `platform/game_paths.inc` | Absolute paths and game-local DLL placement |
| `d3d8.def` | Export names and ordinal assignments |
| `build.cmd` | Shared compile, link, architecture, and export checks |

The proxy recognizes a specific `Nobunaga11WPK.exe` build, verified caller locations, and known CP65001 or CP932 CRT states. The offsets, byte counts, and code pages are named in `verified_inputs.h`.

Executable fingerprints, memory-state checks, and Direct3D forwarding checks run in their established order. Each one-time initializer caches success or refusal, including a failed setter postcheck, so the setter cannot be retried after a partial call.

The graphics module keeps the four opaque export stubs explicit. They preserve the original stack, general registers, and EFLAGS before tail-forwarding to Windows. `ProxyDirect3DCreate8` captures its caller directly at the exported entry point.

The implementation omits runtime diagnostic bookkeeping and logging. It uses the game's existing CRT routine without linking a separate C runtime.

Read [CONTRACTS.md](CONTRACTS.md) before modifying the locale initializer, game fingerprints, or the Direct3D forwarding boundary. It documents accepted states, ordering, failure policies, and ABI constraints.

See [BUILDING.md](BUILDING.md) for the shared build script and native checks. [Release packages](../RELEASING.md) include the checked-in DLL, its SHA-256, and the repository revision containing the source snapshot.
