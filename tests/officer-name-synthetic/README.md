# Synthetic x86 officer-name adapter

This directory contains a standalone Windows test executable. It does not launch
the game, inspect another process, load game code, install a DLL or overwrite
instructions. Production locale-proxy source and exports are unchanged.

The implementation was developed against locale-fix source revision
`4b03eefa4c8f52594fa991cba2764f13aa875a0e` (v1.0.0). It is test-only code, not
an enabled name-localization feature.

## Run

Use an existing Visual Studio 2022 installation with the x86 C++ tools and
Windows SDK. From a checkout containing this directory:

```powershell
./tests/officer-name-synthetic/Run-Tests.ps1
# Also test AddressSanitizer when its existing x86 runtime is installed:
./tests/officer-name-synthetic/Run-Tests.ps1 -IncludeAsan
```

The script discovers the installed tools with `vswhere` and enters the x86
developer environment. It never installs tools or requests elevation. The
executable embeds the explicit test-only `synthetic-tests.manifest` with
asInvoker permissions and no code-page override.

Outputs are ignored under `out/Debug`, `out/Release` and optionally `out/Asan`:
build logs, executable, PDB, compiler assembly, PE disassembly and
machine-readable `results.json`. `out/summary.json` records executable hashes.
Generated files can contain compiler paths and must remain outside Git.

Current acceptance: **91 cases / 61,885 assertions / zero failures per
configuration**, including 10,000 repeated calls. Debug enables `/RTC1` and
`/analyze`; Release uses `/O2`; the optional build uses `/fsanitize=address`.
All use `/W4 /WX /sdl /GS` and a static test CRT. The test-only workflow runs
all three configurations on Windows.

## Architecture

| File                  | Purpose                                                                                              |
| --------------------- | ---------------------------------------------------------------------------------------------------- |
| `model.h`             | Measured 28-byte string layout, synthetic officer/context offsets, test interfaces                   |
| `model.cpp`           | Guarded arena/registry, codecs, owned assignment, evaluator and guarded adapter                      |
| `abi.cpp`             | Authored ECX/stack entry stubs, direct evaluator and assignment probes, deliberately broken controls |
| `tests.cpp`           | Positive cases, baseline comparisons, negative guards, ownership and formatting experiments          |
| `Verify-Assembly.ps1` | Checks compiler-emitted RET12/RET8 and explicit ECX bridges                                          |
| `Run-Tests.ps1`       | Existing MSVC build convention; no additional build system                                           |
| `.clang-format`       | Test-only C++ formatting                                                                             |

The three authored assembly entries model:

- Evaluator/adapter: ECX manager, three stack arguments, `RET 12`, EAX status.
- Assignment: ECX output, source/length on stack, `RET 8`, EAX output pointer.

The probes measure ESP, EBX, ESI, EDI, EBP, DF and stack canaries directly.
Deliberately incorrect register, cleanup and DF functions verify that each probe
detects violations. DF is recorded before the probe clears it to protect the C++
test caller. Valid inputs use the normal x86 DF-clear convention. All four
possible four-byte-aligned entry residues modulo 16 are exercised; a universal
16-byte entry-alignment requirement is not assumed.

## Adapter policy

The adapter snapshots registered context, invokes the original mock exactly
once, then revalidates it. Only the expected success status and exact synthetic
build, event, resource, command, speaker, current name and inline output can
produce `Nobunaga`. Reentry causes both nested and outer substitutions to be
refused while each invocation still evaluates its original once.

The fixture uses `spk` at manager `+0x58`; a different `bp` officer at `+0x48`
guards against the superseded mapping. Identity/fingerprint and
historical/custom provenance are explicitly supplied fixture inputs. No real
module memory or game file supplies these values.

Negative cases compare complete modeled outcomes with a separately run original
mock, including intentional mock mutations. Invalid/unregistered pointers are
refused before dereferencing. This registry is a test facility, not a proposed
way to recognize arbitrary game pointers.

## Ownership and safety

`NameString` has an opaque leading word, inline/heap union at `+4`, byte length
at `+0x14` and capacity at `+0x18`. It is not a host `std::string`. The adapter
requires capacity 15. The eight-byte replacement is copied into existing inline
storage without allocation or officer-field writes.

Separate tests cover owned heap growth, overlap, empty assignment, allocation
failure, stale/foreign pointers and refused double frees. All modeled
allocations use one arena; temporary external source buffers use scoped read
registrations. Red zones and reserved guard pages supplement compiler checks.
AddressSanitizer does not instrument naked assembly or provide object-bound
poisoning for every custom VirtualAlloc region; canaries and registry checks
cover those specific test boundaries.

Long-name tests distinguish dynamic-string storage from nine-byte officer fields
and an independent eleven-byte display-trimming model. Storage success does not
certify glyph layout. Full-name spacing experiments are separate from the
adapter and do not change production formatting.

## Limits and next integration work

These tests establish the authored synthetic normal-return contract. They do not
validate actual game pointers, allocator behavior, custom-officer provenance,
stack depth, caller timing, Steam compatibility, rendering, concurrency, SEH
unwinding or non-local exits.

The harness uses its own static C++ CRT. The production proxy deliberately links
without a CRT; the arena, mock registry and fixtures must not be copied into
production. A live implementation needs a separately reviewed capture layer,
no-CRT-compatible adapter, instruction-installation and rollback plan, and
independent authorization. Preserve the existing five exports and all locale
compatibility guards.

See the sibling translation checkout's
[research report](../../../tenka-sousei-translation/research/officer-name-synthetic-prototype.md).
This relative link requires both repositories as sibling directories. The harness
is test-only; its sources and generated outputs are excluded from the DLL release
package.
