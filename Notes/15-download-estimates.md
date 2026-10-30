# Selected-download estimates

File Manager shows `Downloaded: ... / estimated total: ~...` for selected ZIP/7z
extraction and testing. Archive size remains a separate reference alongside the
request count. Opening, unsupported formats (including RAR and ISO), nested
archives, or unavailable/invalid metadata show `estimated total: unavailable`.
An old engine without the optional range interface also falls back gracefully.

The estimator does not read archive data or add HTTP requests. An optional
extension-specific handler interface exposes conservative physical ranges:

- 7z: full packed folders containing the selection, even if only an early file
  in a solid folder is selected. This can substantially overestimate the amount
  actually needed; compression ratios do not reliably map unpacked prefixes to
  packed-byte offsets. Multiple files in one folder share the same range.
- ZIP: compressed member data and its local header/descriptor. Before a local
  header is known, reserve both maximum 16-bit name/extra lengths (131070 bytes),
  the fixed 30-byte header, and a 24-byte descriptor. This deliberately favors a
  conservative estimate and can overestimate small members or unusual layouts.
  Multi-volume and incomplete/problematic ZIP metadata are not estimated.

The stream rounds ranges to its actual 256 KiB HTTP blocks, merges overlapping
and adjacent ranges, and subtracts blocks currently in the 8 MiB cache. Arithmetic
is checked against the validated source size; huge intervals are processed
without allocating an entry for every block. Empty selections need zero bytes.

This is an estimate of additional body bytes for this operation, excluding prior
browsing downloads and protocol overhead. Cache eviction, repeated reads, or
handler behavior may make actual traffic exceed the initial prediction. The UI
revises the displayed estimate to at least the number already downloaded. It is
not a guaranteed maximum or a precise remaining-byte forecast. Cached operations
may correctly show `~0 B`. Estimation failure never prevents normal extraction.

The progress extension uses a new IID when adding the estimate method, so a
callback built against the previous interface cannot be called with a different
vtable layout. Existing archive extraction/opening COM interfaces are unchanged.

Validation:

```powershell
python Tests/RemoteArchive/build_msvc.py --http --rebuild --agent-check --bin-dir .build/msvc/download-estimates
python Tests/RemoteArchive/test_cli.py --exe .build/msvc/download-estimates/7z.exe
python Tests/RemoteArchive/test_agent.py --exe .build/msvc/download-estimates/agentcheck.exe
python Tests/RemoteArchive/build_msvc.py --target filemanager --rebuild --bin-dir .build/msvc/download-estimates-local
python Tests/RemoteArchive/test_progress_layout.py --exe .build/msvc/download-estimates/7zFM.exe --local-exe .build/msvc/download-estimates-local/7zFM.exe
```

Windows x64 MSVC builds succeeded. All 99 CLI scenarios and 46 Agent scenarios
passed, including encrypted/non-solid/solid 7z, cached extraction/testing, an
uncached ZIP member, and unavailable TAR estimates. The harness also checks
deduplication/overlap, a partial final block at a size above 1 TiB, invalid ranges,
and propagation/reset of estimated values through the actual GUI callback state.
Existing byte/request totals continue to match the local HTTP server records.
In controlled fixtures, the uncached ZIP member estimated/downloaded 262144
bytes, and the solid 7z selection estimated/downloaded 3932160 bytes. These are
fixture results, not promises for arbitrary archives.

Compiled progress-dialog layout checks and the build without HTTP passed.
Interactive desktop verification remains pending because the Computer Use
native pipe was unavailable. Strings currently use English like the other remote
extension labels; DPI, localization and resized-window readability need desktop
acceptance testing.
