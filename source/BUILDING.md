# Building

Use the **x86 Native Tools Command Prompt for Visual Studio** with Microsoft Visual C++ and a Windows SDK. The original preview was built using MSVC **14.44.35207** and Windows SDK **10.0.26100.0**.

From the `source/` folder, run:

```bat
cl.exe /nologo /c /TC /O1 /Oi- /GS- /Zl /W4 /WX /D_WIN32_WINNT=0x0602 /FoD3D8Proxy.obj D3D8Proxy.c
link.exe /NOLOGO /DLL /NODEFAULTLIB /ENTRY:DllMain@12 /SUBSYSTEM:WINDOWS /MACHINE:X86 /DYNAMICBASE /NXCOMPAT /SAFESEH /INCREMENTAL:NO /MANIFEST:NO /OPT:REF /OPT:ICF /DEF:d3d8.def /MAP:d3d8.map /OUT:d3d8.dll D3D8Proxy.obj kernel32.lib bcrypt.lib
```

The experimental lean variant removes logging and the Shell API dependency, so `shell32.lib` is no longer linked. This candidate is not a tested release.

The implementation intentionally avoids introducing a new C runtime. It uses the game's verified internal CRT setter under strict game-identity, caller, and state checks. The Direct3D exports include x86 assembly forwarding stubs with ABI-sensitive behavior.

**Important:** A successful rebuild is not automatically a validated release. The checked-in [`../d3d8.dll`](../d3d8.dll) is the exact previously tested binary. Build outputs may differ due to linker metadata, and the reorganized source has not yet been compiled in the recorded Windows environment. Before substituting a rebuilt DLL, check its exports/ordinals, all compatibility guards and error paths, then separately test it in-game. Never silently overwrite the published tested binary.

Do not compile or test by writing to the original game installation directory.
