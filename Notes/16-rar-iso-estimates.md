# RAR/RAR5 and ISO download estimates

Selected extraction and testing now estimate additional response-body downloads
for single-volume RAR/RAR5 and ISO, using the same cache-aware block accounting
as ZIP/7z. The UI continues to mark totals with `~` and revise them to at least
the downloaded count. Estimates remain approximate, especially if cache eviction
or repeated reads cause additional traffic. No archive reads are performed by
the metadata planner.

- ISO: enumerate each physical extent separately. Do not count unused gaps
  between extents. Validate that extent lengths sum to the file's total size;
  directory entries have no data ranges. Boot entries use their existing
  handler-reported position and size.
- RAR: walk backwards to the solid reset boundary, using the same `IsSolid`
  rules as extraction (including old archive-level solid flags), then report
  packed data ranges for preceding files and the selected file.
- RAR5: follow solid predecessors while ignoring service records as dictionary
  reset boundaries. Report main-file packed ranges separately. Copy/hard/symbolic
  link metadata that needs special handling produces an unavailable estimate.
- Split/multi-volume and archives with relevant parse errors also fall back to
  an unavailable estimate. Nested archives remain outside the estimator's scope.

A new optional multi-range interface and synchronous sink complement the original
single-range interface. Existing ZIP/7z implementations and interface IIDs remain
unchanged. The File Manager collects ranges, merges shared HTTP blocks, and
subtracts cached data. Partial plans are discarded on any failure.

Build and checks:

```powershell
python Tests/RemoteArchive/build_msvc.py --http --rebuild --agent-check --bin-dir .build/msvc/rar-iso-estimates
python Tests/RemoteArchive/test_agent.py --exe .build/msvc/rar-iso-estimates/agentcheck.exe
python Tests/RemoteArchive/test_cli.py --exe .build/msvc/rar-iso-estimates/7z.exe
python Tests/RemoteArchive/build_msvc.py --target filemanager --rebuild --bin-dir .build/msvc/rar-iso-estimates-local
python Tests/RemoteArchive/test_progress_layout.py --exe .build/msvc/rar-iso-estimates/7zFM.exe --local-exe .build/msvc/rar-iso-estimates-local/7zFM.exe
```

All 58 Agent scenarios and 99 CLI scenarios passed. Agent tests generate stored
RAR4/RAR5 (solid/non-solid) and ISO/Joliet images with one or two extents, compare
extracted contents, and compare traffic estimates/counters against actual HTTP
requests. The ISO gap is excluded; auto detection may cache the first extent,
which is correctly deducted from the extraction estimate. Local RAR/ISO are
inherently read-only, so the harness no longer assumes every local format can be
updated. Remote read-only checks remain in place.

Additional compressed-fixture checks:

```powershell
python Tests/RemoteArchive/test_rar_estimates.py --exe .build/msvc/rar-iso-estimates/agentcheck.exe --fixtures-dir .build/estimate-fixtures
```

This optional suite needs five decoded `.rar` files from the official
[libarchive test directory](https://github.com/libarchive/libarchive/tree/348e5a2e27503f5bf114e298e2bd9ddcd59ab48e/libarchive/test)
at commit `348e5a2e27503f5bf114e298e2bd9ddcd59ab48e`:

- `test_read_format_rar_compress_normal.rar.uu`
- `test_read_format_rar5_compressed.rar.uu`
- `test_read_format_rar5_multiple_files_solid.rar.uu`
- `test_read_format_rar4_solid_encrypted.rar.uu`
- `test_read_format_rar4_solid_encrypted_filenames.rar.uu`

The downloaded uuencoded fixtures and decoded archives stayed in ignored
`.build/estimate-fixtures/`; no third-party binary fixture is committed. The
suite performs no downloads. The public encrypted-fixture password is `password`,
as documented by libarchive's encryption tests. The normal legacy fixture also
contains a Unix symlink; that item is excluded from extraction to avoid requiring
a Windows symlink privilege unrelated to estimating data reads.

All 10 compressed-fixture cases passed with verified extracted hashes, including
strict/bypassed ETag sessions. Metadata checks prove the last solid file's plan
contains preceding packed ranges even when a small fixture is fully cached.
Reports record fixture SHA-256 hashes and HTTP request records.

Windows x64 MSVC builds with and without HTTP and compiled-dialog layout checks
passed. Desktop readability remains unverified in this session, as before.
Boot-image and exotic RAR service/link layouts need further dedicated fixtures;
the general single-file and ISO multi-extent paths have direct coverage.
