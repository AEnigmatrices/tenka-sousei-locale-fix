# Building

Use the **x86 Native Tools Command Prompt for Visual Studio** with Microsoft Visual C++ and a Windows SDK.

The [Windows build workflow](../.github/workflows/build.yml) builds the source on `windows-2022` using the hosted Visual Studio 2022 x86 toolchain. The recorded local toolchain is MSVC **14.44.35207** and Windows SDK **10.0.26100.0**.

## Compile and link

From the `source/` folder, run:

```bat
cl.exe /nologo /c /TC /O1 /Oi- /GS- /Zl /W4 /WX /D_WIN32_WINNT=0x0602 /FoD3D8Proxy.obj D3D8Proxy.c
link.exe /NOLOGO /DLL /NODEFAULTLIB /ENTRY:DllMain@12 /SUBSYSTEM:WINDOWS /MACHINE:X86 /DYNAMICBASE /NXCOMPAT /SAFESEH /INCREMENTAL:NO /MANIFEST:NO /OPT:REF /OPT:ICF /DEF:d3d8.def /MAP:d3d8.map /OUT:d3d8.dll D3D8Proxy.obj kernel32.lib bcrypt.lib
```

The implementation uses `kernel32.lib` and `bcrypt.lib`. It does not link `shell32.lib` or a separate C runtime.

The Direct3D exports include x86 assembly forwarding stubs with ABI-sensitive behavior. The workflow checks the output architecture and all five exported names and ordinals.

## Build outputs and releases

Local compilation writes `source/d3d8.dll`; CI uploads the compiled DLL as a workflow artifact.

Release packaging uses the separate, checked-in `../d3d8.dll`. Rebuilding with another toolchain or linker metadata can produce a different checksum. Each release's `release.json` records the packaged DLL's size, SHA-256, and repository commit.

Build in the repository's `source/` folder. See [RELEASING.md](../RELEASING.md) for versioning and packaging.
