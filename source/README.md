# Source code

This is the implementation of the **x86 Direct3D 8 proxy** distributed as `d3d8.dll`.

The component files are included into one C translation unit by `D3D8Proxy.c`. The `.inc` extension distinguishes implementation fragments from ordinary public headers.

| File | Responsibility |
| --- | --- |
| `D3D8Proxy.c` | Entry point, initialization orchestration, and ABI-sensitive x86 export thunks |
| `compat/verified_inputs.h` | Exact game hashes, code fingerprints, and known CRT classification tables |
| `compat/integrity.inc` | File hashing, image-memory checks, and CRT state validation |
| `graphics/direct3d8_forwarding.inc` | Verified loading and forwarding of Windows Direct3D 8 |
| `platform/portable_paths.inc` | Game-local path checks |
| `d3d8.def` | Export names and ordinal assignments |

The proxy recognizes a specific `Nobunaga11WPK.exe` build, verified caller locations, and the known CP65001 or CP932 CRT states. Executable fingerprints, memory-state checks, and Direct3D forwarding checks are part of the implementation.

Runtime JSONL logging and Shell-folder setup are omitted. The DLL uses the game's existing CRT routine and does not introduce a separate C runtime.

See [BUILDING.md](BUILDING.md) for the toolchain and build commands. [Release packages](../RELEASING.md) include the checked-in DLL, its SHA-256, and the repository revision containing the corresponding source snapshot.
