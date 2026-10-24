# Source code

This is the implementation of the **x86 Direct3D 8 proxy** shipped as [`../d3d8.dll`](../d3d8.dll). The streamlined DLL is compiled and export-checked on Windows, but **has not yet been tested in-game**.

The component files are deliberately **included into one C translation unit** by `D3D8Proxy.c`. The `.inc` extension distinguishes implementation fragments from ordinary public headers.

| File | Responsibility |
| --- | --- |
| `D3D8Proxy.c` | Entry point, initialization orchestration, and ABI-sensitive x86 export thunks |
| `compat/verified_inputs.h` | Exact game hashes, code fingerprints, and known CRT classification tables |
| `compat/integrity.inc` | File hashing, image-memory checks, CRT state validation |
| `graphics/direct3d8_forwarding.inc` | Verified loading and forwarding of real Windows Direct3D 8 |
| `platform/portable_paths.inc` | Game-local path checks |
| `d3d8.def` | Export names and ordinal assignments |

The streamlined implementation removes runtime JSONL logging and Shell-folder setup while retaining executable fingerprint, memory-state and Direct3D forwarding checks. Its behavior differs from the previously game-tested logging-enabled build.

See [BUILDING.md](BUILDING.md) for the recorded MSVC toolchain and command lines.
