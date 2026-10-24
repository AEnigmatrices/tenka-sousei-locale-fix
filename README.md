# Tenka Sousei Locale Fix

**Fix garbled Japanese text in *Nobunaga's Ambition: Tenka Sousei* (天下創世), without Locale Emulator, on the supported Steam build.**

This unofficial, drop-in compatibility fix does not change Windows' system locale or modify the game's executable.

## Installation

1. Download [**d3d8.dll**](d3d8.dll) from this repository (open the file and select **Download raw file**).
2. In Steam, right-click the game and select **Manage → Browse local files**.
3. **Copy `d3d8.dll` into the game's installation folder** (the folder containing `Nobunaga11WPK.exe`).
4. Launch the game normally through Steam, without Locale Emulator.

```text
Game installation folder/
├── Nobunaga11WPK.exe
├── d3d8.dll             ← Copy into this folder
└── ...
```

**To uninstall:** Close the game, then remove the add-on's `d3d8.dll` from that folder.

## Why does Japanese text become gibberish?

The game expects the Japanese **CP932** character encoding (closely related to Shift-JIS). On some Windows setups, its older text-processing runtime initializes using a different code page—on the tested computer, **UTF-8 / code page 65001**. This causes Japanese bytes to be interpreted incorrectly.

### Previous workaround: Locale Emulator

[Locale Emulator](https://github.com/xupefei/Locale-Emulator) has long provided a useful way to launch older Japanese games in a Japanese locale environment without changing Windows' system locale. It remains an alternative for versions our DLL doesn't support. This fix allows the supported build to start normally from Steam.

## How does `d3d8.dll` work?

The game uses Microsoft's **Direct3D 8** graphics interface. Our `d3d8.dll` is a proxy that forwards graphics calls to the real Windows Direct3D 8 library. When it recognizes the exact supported game build and a known safe startup state, it calls the game's **existing** multibyte initialization routine to select Japanese CP932.

It does **not** modify the EXE on disk, translate the game into English, change Windows' global locale, or replace graphics assets. Some Windows-managed strings, including the window title, may remain garbled.

The [source code](source/README.md) and [build instructions](source/BUILDING.md) are available for inspection.

## Compatibility

| | Tested or supported |
| --- | --- |
| Game | Steam PC version, `Nobunaga11WPK.exe` |
| Original EXE SHA-256 | `b835b6a50129f7bb07e6310490d7a259cbab65e6396ee61c735e1ea4b413c14d` |
| Tested platform | Windows 11 build 26300, x86 game under WOW64 |
| Recognized initial CRT state | Code page 65001, or verified correct 932 |
| Published DLL SHA-256 | `487afc6cbaece7d56892ab44871549c11466dcf50a07ffd776bcddc35f231495` |

The **exact published 17,920-byte DLL** passed a normal Steam launch without Locale Emulator. Opening English tutorial dialogue and adjacent Japanese UI were legible, and the game exited normally. This does **not** certify every screen, save behavior, other game builds, or other Windows configurations.

## Troubleshooting

**The game still shows gibberish, or won't launch:** Verify the DLL is in the game installation folder and your EXE matches the supported build. Unrecognized code pages or builds are deliberately refused.

**There is already a `d3d8.dll` in the folder:** It may be part of another graphics mod. **Do not overwrite it.** The current fix cannot chain with another Direct3D 8 proxy.

**Diagnostics:** The DLL may write JSONL logs to `%LOCALAPPDATA%\TenkaSouseiCP932\Logs`. These may remain after uninstalling.

**Security:** The preview DLL is unsigned. Verify its checksum and inspect the source if desired; only run binaries you trust.

## About this project

This is an **unofficial fan-made compatibility fix** and is not affiliated with Koei Tecmo, Steam, or Microsoft. The repository contains no proprietary game executable or game resources. The fix is supplied **as-is**.
