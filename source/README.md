# Source code

This is the implementation of the **x86 Direct3D 8 proxy** shipped as [`../d3d8.dll`](../d3d8.dll). The published DLL is the separately game-tested build; the reorganized source has **not** yet been rebuilt or tested in-game.

The component files are deliberately **included into one C translation unit** by `D3D8Proxy.c`. The `.inc` extension distinguishes implementation fragments from ordinary public headers.

| File | Responsibility |
| --- | --- |
| `D3D8Proxy.c` | Entry point, initialization orchestration, and ABI-sensitive x86 export thunks |
| `compat/verified_inputs.h` | Exact game hashes, code fingerprints, and known CRT classification tables |
| `compat/integrity.inc` | File hashing, image-memory checks, CRT state validation |
| `diagnostics/crt_log.inc` | Structured JSONL logging and audit trail |
| `graphics/direct3d8_forwarding.inc` | Verified loading and forwarding of real Windows Direct3D 8 |
| `platform/portable_paths.inc` | Game-local path checks and diagnostic directories |
| `d3d8.def` | Export names and ordinal assignments |

The organizational refactor preserved implementation fragments verbatim, with only include paths and comments changed in the main C file. It does **not** establish binary equivalence or runtime compatibility of future builds.

See [BUILDING.md](BUILDING.md) for the recorded MSVC toolchain and command lines.
