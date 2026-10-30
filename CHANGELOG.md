# Changelog

## [1.0.2] - 2026-10-25

- Document what Windows DLLs are, how application-local Direct3D 8 proxying works, and the security risks of loading untrusted DLLs.
- Add source compatibility contracts for the verified executable, one-time Japanese CP932 initialization, and x86 Direct3D forwarding behavior.
- Improve internal naming and separate the verified CRT character tables from executable fingerprints without changing runtime behavior.
- Expand native regression checks for game paths, traversal rejection, memory protections, and code fingerprints.
- Test both the freshly compiled proxy and the checked-in distribution DLL in CI.
- Verify release ZIP contents, both layers of SHA-256 checksums, and the binary provenance manifest before publication.
- Keep the supported executable and distributed `d3d8.dll` unchanged; this is a documentation and maintainability release.

## [1.0.1] - 2026-10-24

- Add a direct DLL download and quick installation TL;DR to the README.
- Separate locale initialization, CRT state, and graphics forwarding into focused components.
- Remove unused diagnostic state and the unused Shell header.
- Centralize the game's offsets, sizes, caller addresses, and code pages.
- Use one build script for local builds, CI, and release verification.
- Add native initialization and forwarding regression checks.
- Reduce the compiled DLL from 11,776 to 9,728 bytes while preserving validation order and export stubs.

## [1.0.0] - 2026-10-24

- Restore Japanese CP932 text handling through the game's existing runtime routine.
- Forward Direct3D 8 calls to the Windows library.
- Install by placing a single DLL beside `Nobunaga11WPK.exe`; uninstall by removing it.
- Preserve the existing DLL's executable, memory-state, and forwarding guards.
- Add a versioned Windows ZIP, standalone DLL, checksums, and source-revision manifest.
- Reorganize installation and troubleshooting documentation.
- Add the MIT License and an automated release workflow.

[1.0.2]: https://github.com/AEnigmatrices/tenka-sousei-locale-fix/releases/tag/v1.0.2
[1.0.1]: https://github.com/AEnigmatrices/tenka-sousei-locale-fix/releases/tag/v1.0.1
[1.0.0]: https://github.com/AEnigmatrices/tenka-sousei-locale-fix/releases/tag/v1.0.0
