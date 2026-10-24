# Tenka Sousei Locale Fix

**Fix garbled Japanese text in *Nobunaga's Ambition: Tenka Sousei* (天下創世) on supported Windows installations—without Locale Emulator.**

This free, unofficial compatibility add-on requires no changes to the game's executable or to your Windows system locale.

## Before and after

| Without the fix | With the fix |
| :---: | :---: |
| ![Corrupted Japanese menu help text before the fix](images/before.webp) | ![Readable Japanese menu help text after the fix](images/after.webp) |

The red circles highlight the in-game help text. Note that the **window title bar may remain garbled**: this fix targets the game's internal text processing, not every Windows UI string.

## Installation

1. **Download the preview ZIP from [GitHub Releases](https://github.com/AEnigmatrices/tenka-sousei-locale-fix/releases) when published**, then extract it. The first public binary release is not uploaded yet.
2. In **Steam**, right-click the game and choose **Manage → Browse local files**. Open the folder containing `Nobunaga11WPK.exe`.
3. **Copy `d3d8.dll` into the game's installation folder.**
4. Start the game **normally through Steam**, without Locale Emulator.

That's it! There's no installer and no Windows configuration to change.

```text
Game installation folder/
├── Nobunaga11WPK.exe
├── d3d8.dll                 ← Copy this file into this folder
└── ...
```

### Uninstall

Close the game, then remove **the compatibility add-on's** `d3d8.dll` from the game installation folder. Do not remove an unrelated DLL. Your original executable and game resources have not been replaced.

## Why does this happen?

Tenka Sousei is an older Japanese game that expects Japanese **CP932 (Shift-JIS-compatible)** text processing. On some Windows installations, the game instead initializes its multibyte-text functions from another system code page. Japanese bytes can then be mistaken for different characters, producing garbled text.

On the system used for testing, a normal launch initialized the game's text-processing runtime with **UTF-8 code page 65001**; launching through Locale Emulator Japan initialized it with **Japanese code page 932**.

### The previous workaround: Locale Emulator

[Locale Emulator](https://github.com/xupefei/Locale-Emulator) can launch older Japanese software with a Japanese locale environment without requiring a Windows-wide locale change. It remains a useful option, especially for game versions this add-on does not support.

This fix is an alternative for the supported game build: install one DLL and start the game through Steam as usual.

## What is `d3d8.dll` and how does it work?

Tenka Sousei uses **Direct3D 8** for graphics. The included `d3d8.dll` is a small **Direct3D 8 proxy**: it passes the game's usual graphics calls through to the genuine Windows Direct3D library.

For the **exact supported game executable**, the proxy also checks its loaded code and text-processing state, then asks the game's **existing** multibyte initialization routine to select **CP932**. It does not replace game text, translate anything, modify the EXE on disk, or change the Windows-wide code page.

The program includes strict checks so it does not blindly alter an unknown game version. The implementation and build notes will be added to this repository alongside the binary release.

## Compatibility

This is an **early preview tested on one Windows 11 / Steam installation**, not a universal fix.

| Requirement | Tested scope |
| --- | --- |
| Game | *Nobunaga's Ambition: Tenka Sousei* (Steam PC version), `Nobunaga11WPK.exe` |
| Supported game EXE SHA-256 | `b835b6a50129f7bb07e6310490d7a259cbab65e6396ee61c735e1ea4b413c14d` |
| Windows | Windows 11 build 26300 (tested), native x86 game under WOW64 |
| Initial game CRT code page | 65001, or a verified already-correct 932 state |
| Tested DLL SHA-256 | `487afc6cbaece7d56892ab44871549c11466dcf50a07ffd776bcddc35f231495` |

Other game builds, Windows versions, and code pages have **not** been confirmed compatible. The fix may refuse to activate for an unsupported configuration.

**Testing to date:** the exact DLL in this preview successfully ran through a normal Steam launch without Locale Emulator. The opening English tutorial and nearby Japanese interface text displayed legibly. A complete playthrough, save-game behavior, and all other UI screens have not yet been verified.

## Troubleshooting

**Nothing changes or the game fails to launch?** Confirm you have the supported game build, the DLL is in the same installation folder as `Nobunaga11WPK.exe`, and you are not launching through Locale Emulator at the same time. The add-on may refuse unknown versions or unexpected startup states.

**Already have a `d3d8.dll` in that folder?** It may belong to another graphics mod. **Do not overwrite it**; this preview does not support chaining with other Direct3D 8 wrappers.

**Want to verify compatibility?** A read-only PowerShell checker is available at [`scripts/Verify-Compatibility.ps1`](scripts/Verify-Compatibility.ps1). Running it is optional for the basic copy-and-play instructions. Do not weaken Windows security settings to run scripts.

**Where are the logs?** The add-on can write diagnostics under `%LOCALAPPDATA%\TenkaSouseiCP932\Logs`. These logs may remain after removing the DLL.

**Why is the window title still garbled?** The game's internal CRT code page is corrected, but Windows' broader ANSI/UI behavior is not changed.

## Source, builds, and checksums

The DLL's source and build instructions are planned for this repository; they have not yet been uploaded. The preview binary is **unsigned**, so use it only if you trust this repository and its source.

The tested `d3d8.dll` is **17,920 bytes**, SHA-256:

```text
487afc6cbaece7d56892ab44871549c11466dcf50a07ffd776bcddc35f231495
```

The planned downloadable ZIP will contain the same tested DLL together with this README. A rebuilt DLL or differently packaged ZIP can have a different checksum and requires separate verification.

## Disclaimer

This is an **unofficial fan-made compatibility project**. It is not affiliated with or endorsed by Koei Tecmo, Steam, or Microsoft. It does not include any copyrighted game executable, decrypted game code, or game resources. You must own the game separately.

The fix is supplied **as-is**, without a guarantee of compatibility with every system or game version.
