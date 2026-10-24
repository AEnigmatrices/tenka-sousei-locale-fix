# Building

Use the **x86 Native Tools Command Prompt for Visual Studio** with Microsoft Visual C++ and a Windows SDK.

The [Windows workflow](../.github/workflows/build.yml) uses the hosted Visual Studio 2022 x86 toolchain on `windows-2022`. The recorded local toolchain is MSVC **14.44.35207** and Windows SDK **10.0.26100.0**.

## Compile and link

From the repository root, run:

```bat
source\build.cmd
```

The script also works from another directory. It writes `source/d3d8.dll`, checks that the output is x86, and calls [verify-exports.ps1](../scripts/verify-exports.ps1) to validate all five exported names and ordinals.

Compiler and linker flags are maintained in [build.cmd](build.cmd), which is also used by CI and the release workflow. The DLL links `kernel32.lib` and `bcrypt.lib`, with no separate C runtime or Shell library.

## Native checks

After building, run from the same x86 tools prompt:

```bat
tests\run.cmd
```

The checks exercise 28 locale-initialization scenarios, cached successes and refusals, path matching and traversal rejection, real Windows memory-page protections and code fingerprints, export names and ordinals, the four x86 assembly thunk instruction sequences, and repeated forwarding to Windows Direct3D 8.

The locale checks supply controlled game and CRT inputs to the production initializer; they do not require game files. The path and memory checks exercise production helper functions directly against synthetic inputs and Windows-managed pages, without loading proprietary game data. The forwarding check loads the built proxy and creates and releases Direct3D 8 interface objects without creating a window or graphics device.

To run the forwarding check against an additional DLL, pass its path to `tests\run.cmd`. Outputs stay under the ignored `dist/tests/` directory.

## Build outputs and releases

Release packaging uses the separate, checked-in `../d3d8.dll`. Copy the DLL to distribute from `source/d3d8.dll` to the repository root before following [RELEASING.md](../RELEASING.md).

Rebuilding with another toolchain or linker metadata can produce a different checksum. Each release's `release.json` records the packaged DLL's size, SHA-256, and repository commit.
