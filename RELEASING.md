# Releases

The version in [VERSION](VERSION) controls release tags. Publishing a change to that file on `main` runs the [release workflow](.github/workflows/release.yml), which creates `v<VERSION>` and publishes the download assets.

## Prepare a release

1. Run `source\build.cmd` and `tests\run.cmd` from an x86 Native Tools Command Prompt.
2. Copy the DLL to distribute from `source/d3d8.dll` to the repository root as `d3d8.dll`.
3. Update `VERSION` using `major.minor.patch` format.
4. Add a matching `## [major.minor.patch] - YYYY-MM-DD` entry to [CHANGELOG.md](CHANGELOG.md).
5. Commit the DLL, source, version, and documentation together, then push to `main`.

The release workflow builds the source and runs the native checks before publication. It packages the checked-in root DLL from the release commit, keeping its recorded checksum intact. The separate build workflow uses the same build and check scripts.

## Package locally

From a clean checkout, run:

```powershell
powershell -NoProfile -File scripts/package-release.ps1
```

Files are written to the ignored `dist/` directory. For a local documentation or packaging check with uncommitted changes, add `-AllowDirty`; the manifest records that the working tree has changes. Publication requires a clean checkout.

## Download assets

| Asset | Contents |
| --- | --- |
| `tenka-sousei-locale-fix-windows-x86.zip` | DLL, installation guide, license, changelog, manifest, and package checksums |
| `d3d8.dll` | Standalone copy of the same DLL |
| `SHA256SUMS.txt` | SHA-256 values for the ZIP, standalone DLL, and manifest |
| `release.json` | Version, tag, repository revision, DLL size, and SHA-256 |

The ZIP and DLL names stay consistent so the README's latest-release download links work across versions. Each release tag provides version-specific download URLs.

The manifest's repository commit identifies the source snapshot and checked-in DLL used for that release. It does not claim that a separate rebuild produces byte-identical output.

## Publication

The workflow uses the repository's built-in GitHub Actions token with `contents: write`. It can also be started manually from the Actions tab on `main`.

Existing releases are not overwritten. If a tag already exists, it must resolve to the workflow's commit. Publish a new version to distribute changed files.
