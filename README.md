# Tenka Sousei Locale Fix

**Fix garbled Japanese text in *Nobunaga's Ambition: Tenka Sousei* (天下創世) and launch directly through Steam, without Locale Emulator.**

For Windows and the Steam game that uses `Nobunaga11WPK.exe`. This unofficial, drop-in fix leaves your Windows system locale and the game's executable on disk unchanged.

## TL;DR

[**Download d3d8.dll**](https://github.com/AEnigmatrices/tenka-sousei-locale-fix/releases/latest/download/d3d8.dll), put it beside `Nobunaga11WPK.exe` in the game folder, then launch through Steam. Find the folder with **Steam → right-click the game → Manage → Browse local files**.

## Download

[**Download the fix for Windows**](https://github.com/AEnigmatrices/tenka-sousei-locale-fix/releases/latest/download/tenka-sousei-locale-fix-windows-x86.zip)

The ZIP contains `d3d8.dll` and installation instructions. Only the DLL needs to go in your game folder.

[All releases](https://github.com/AEnigmatrices/tenka-sousei-locale-fix/releases) · [Changelog](CHANGELOG.md)

## Before and after

The red circles highlight Japanese help text in the game's start menu. Click either screenshot to view it at full size.

| Without the fix | With the fix |
| :---: | :---: |
| [![Before: Japanese help text is garbled](images/before.webp)](images/before.webp) | [![After: Japanese help text is readable](images/after.webp)](images/after.webp) |

## Installation

Close the game before installing. If the game folder already contains `d3d8.dll` from another mod, keep that file: this fix cannot be installed alongside another Direct3D 8 proxy.

1. Download and extract **tenka-sousei-locale-fix-windows-x86.zip** using the link above.
2. In Steam, right-click the game and select **Manage → Browse local files**.
3. Copy the extracted **`d3d8.dll`** into the folder containing **`Nobunaga11WPK.exe`**.
4. Launch the game normally through Steam, without Locale Emulator.

```text
Game installation folder/
├── Nobunaga11WPK.exe
├── d3d8.dll             ← Copy into this folder
└── ...
```

Japanese menu help text should appear as shown in the “With the fix” screenshot.

## Uninstallation

Close the game, then remove the `d3d8.dll` you installed from the game folder.

## Known limitations

- This fix restores Japanese text; it does not translate the game into English.
- Some Windows-managed strings, including the window title, may remain garbled.
- Another graphics mod that also uses `d3d8.dll` cannot be installed alongside this fix.

## Troubleshooting

**Text is still garbled:** Make sure the ZIP has been extracted and `d3d8.dll` is directly beside `Nobunaga11WPK.exe`, then restart the game through Steam.

**The game will not launch:** Remove this fix's `d3d8.dll` and try launching again. If removing it restores startup, leave the fix uninstalled. The source documentation describes the game-build and runtime conditions the DLL recognizes.

**Using a different game version:** [Locale Emulator](https://github.com/xupefei/Locale-Emulator) remains an alternative way to launch older Japanese games in a Japanese locale environment.

## How it works

The game expects Japanese **CP932** text encoding. Its older text-processing runtime can initialize with a different code page, causing Japanese bytes to be interpreted incorrectly.

The game loads this fix as a **Direct3D 8 proxy**. It forwards graphics calls to the Windows Direct3D 8 library and uses the recognized game's existing multibyte initialization routine to select CP932. It does not replace graphics assets or create runtime diagnostic logs.

[Source code](source/README.md) · [Build instructions](source/BUILDING.md) · [Release process](RELEASING.md)

## About this project

This is an **unofficial fan-made compatibility fix**, unaffiliated with Koei Tecmo, Steam, or Microsoft. The repository contains no proprietary game executable or game resources.

The fix's code is available under the [MIT License](LICENSE). Game content shown in the screenshots belongs to its respective owners.
