# Tenka Sousei CP932 compatibility add-on — 0.1.0-preview.1

**Offline-validated preview. This revised portable DLL has not yet been tested
in the game.** Its earlier research build passed a normal Steam launch and the
opening English tutorial with readable Japanese UI. Use this archive for the
next supervised preview trial, not as a claimed general release.

This add-on keeps the original Steam executable unchanged. An installation-local
`d3d8.dll` forwards graphics calls to Windows and, at verified startup calls,
asks the game's existing CRT routine to select CP932. It does not change Windows
locale settings or the process's Win32 ANSI/OEM code pages.

## Supported build

- Native x86 `Nobunaga11WPK.exe`, exactly4,968,544 bytes.
- Required executable SHA-256:
  `b835b6a50129f7bb07e6310490d7a259cbab65e6396ee61c735e1ea4b413c14d`.
- Preview DLL SHA-256: `487afc6cbaece7d56892ab44871549c11466dcf50a07ffd776bcddc35f231495` (17920 bytes).
- The narrow runtime guards accept the verified UTF-8/65001 CRT state or an
  already-correct932 state. Other states and other executable builds are refused.
- Windows10/11 is the intended host; offline testing was on Windows x64. Other
  platforms and broader locale/gameplay behavior have not been validated.

Game and DLL must be in the same ordinary local directory, with those filenames.
Unicode and alternate drive-letter paths are supported within the bounded
MAX_PATH limits; UNC/device paths and very long paths are not supported.
No other installation-local `d3d8.dll` may already be present. Chaining with
graphics wrappers, overlays implemented as the same DLL, or other d3d8 proxies
is not implemented. Never overwrite an existing wrapper.

## File verification

Extract this package outside the game installation. The included script is
read-only: it checks file hashes/PE32 identities, and never copies, removes,
loads or launches anything. It does not prove that the game is closed.

In PowerShell, substitute your actual Steam game directory:

```powershell
.\Verify-Compatibility.ps1 -GameDirectory 'D:\SteamLibrary\steamapps\common\Nobunaga11WPK' -Mode PreInstall
```

If a check fails or script execution is blocked, stop and report the output.
Do not change security settings, enable a bypass, restore an older EXE over a
Steam update, or force an installation. Use existing permissions; the package
does not request elevation. The DLL and script are unsigned.

## Install for the supervised preview trial

1. Close the game and launcher. Confirm Steam has no update, download or
   verification active for this title. Keep the existing options unchanged.
2. Run the PreInstall check above. It requires the supported original EXE,
   an intact package DLL, and **no existing game-directory d3d8.dll**.
3. Copy **only this package's `d3d8.dll`** next to the verified
   `Nobunaga11WPK.exe`. If Windows asks to replace a file, cancel and stop.
   Do not replace the executable, launcher, tutorial, fonts or system d3d8.dll.
4. Verify the added file:

```powershell
.\Verify-Compatibility.ps1 -GameDirectory 'D:\SteamLibrary\steamapps\common\Nobunaga11WPK' -Mode Installed
```

The proposed trial uses the normal Steam -> existing launcher -> Start route,
without Locale Emulator. Observe the first two opening tutorial blocks and
exit normally. Stop on refusal, error, freeze or crash; do not retry or switch
instrumentation automatically. New live execution requires the trial's separate
approval; this archive's creation did not install or run it.

## Remove

Close the game and launcher and wait for idle Steam. Run the Installed check.
If it confirms the exact preview DLL, delete **only the added game-directory
`d3d8.dll`**, then check:

```powershell
.\Verify-Compatibility.ps1 -GameDirectory 'D:\SteamLibrary\steamapps\common\Nobunaga11WPK' -Mode Removed
```

No original game file needs restoration: the add-on never replaces one. If a
hash is unexpected, the game updated, or a copy/removal was interrupted, stop
and inspect the physical files before choosing recovery. Do not delete an
unknown DLL, change permissions, or use Steam verification as an automatic fix.

## Diagnostics and limits

The DLL creates its own two product folders under the current user's Windows
local application-data folder:

```text
%LOCALAPPDATA%\TenkaSouseiCP932\Logs\
```

Forwarding and CRT JSONL logs use unique timestamp/process/thread filenames.
Existing files are not overwritten. This replaces the old research-machine
log path. Diagnostic folders/files are retained when the DLL is removed; they
are separate from saves and game resources.

The existing fail-closed behavior is preserved: if diagnostics cannot be
created/flushed, identity checks fail, or CRT state is unexpected, initialization
may be refused. There is no fallback that silently applies the setter without
its required checks/intent record. Report the exact error and available logs.

The previous prototype demonstrated selected dialogue and Japanese UI; it did
not prove every interface, text layout, filename conversion, save path or
Windows CP_ACP call works. The portable revision needs its own live trial.

The `source` folder contains this add-on's implementation and build instructions.
No game executable, decrypted game code or game resource is included. No Steam
authentication, integrity or security enforcement is disabled.
