# Building the add-on

Use an existing x86 MSVC Native Tools environment with a Windows SDK. The
recorded offline build used MSVC14.44.35207 and SDK10.0.26100.0. No download or
installation is performed by this source package.

Copy these sources into a dedicated build directory outside the game folder.
From its x86 Native Tools command prompt:

```bat
cl.exe /nologo /c /TC /O1 /Oi- /GS- /Zl /W4 /WX /D_WIN32_WINNT=0x0602 /FoD3D8Proxy.obj D3D8Proxy.c
link.exe /NOLOGO /DLL /NODEFAULTLIB /ENTRY:DllMain@12 /SUBSYSTEM:WINDOWS /MACHINE:X86 /DYNAMICBASE /NXCOMPAT /SAFESEH /INCREMENTAL:NO /MANIFEST:NO /OPT:REF /OPT:ICF /DEF:d3d8.def /MAP:d3d8.map /OUT:d3d8.dll D3D8Proxy.obj kernel32.lib bcrypt.lib shell32.lib
```

The DLL deliberately has no imported C runtime. Its game CRT access is the
verified existing private routine, not a new CRT library or a remote call.
The opaque exports retain their assembly tail-forwarding ABI. General registers
and EFLAGS are preserved; no new x87/XMM transparency guarantee is made.

The constants/digests and multibyte tables in VerifiedInputs.h correspond to the
single supported original EXE and prior verified CRT observations. Do not adapt
them automatically to another game build. PortablePaths.h removes personal
directory assumptions; it does not weaken executable/code/caller/state gates.

Compile only. Do not automatically install the result or load it as a test.
Rebuilding changes file identity; linker timestamps also mean byte-identical
reproducibility is not claimed. A new build requires its own identity record,
static verification and authorized live trial. The supplied preview archive
and its pinned verifier describe the supplied binary, not an arbitrary rebuild.
