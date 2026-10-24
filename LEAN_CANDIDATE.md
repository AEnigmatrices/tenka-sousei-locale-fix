# Lean DLL candidate (experimental)

This branch removes runtime JSONL logging, log-folder creation, Shell-folder lookup, and associated diagnostic serialization. It retains file and mapped-image verification, original executable hash, both caller fingerprints, exact CRT classification tables, writable-region checks, Direct3D export name/ordinal checks, and the original five exported x86 ABI wrappers.

**Important difference:** log creation and flush are no longer prerequisites. This changes fail-closed behavior compared with the tested `0.1.0-preview.1` DLL and requires a separately authorized in-game compatibility trial.

The checked-in root `d3d8.dll` remains the original tested binary, NOT this new candidate.

To build on Windows use the existing x86 MSVC commands in `source/BUILDING.md`, but **remove `shell32.lib` from the link command** (no Shell APIs remain). Validate PE32 architecture and all named/ordinal D3D8 exports before attempting a game run.

This branch has not yet passed MSVC compilation, export verification, or runtime testing. It must not be merged or released as a replacement binary until those checks succeed.
