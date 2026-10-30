# Selection download preview

File Manager now previews the current selection before extraction in a remote
archive panel's status bar:

- **Unpacked:** total file bytes after extraction.
- **Download:** approximate additional HTTP body bytes needed after accounting
  for the session cache. `~0 B` means the planned ranges are already cached.
- **[Solid]:** the archive or selected items report solid compression; other
  files in the same block may have to be read.

For example, the controlled solid 7z fixture previews **3.6 KiB unpacked** and
**3.7 MiB download** for `target.txt`. Its exact missing-byte estimate is
3,932,160 bytes, matching the extraction transfer in that run. After extraction,
the same selection previews zero additional download with its cache retained.
These are fixture measurements, not general compression ratios.

The status bar uses three proportional panes for remote folders and returns to
the usual four-pane layout for local folders. Selecting nothing clears the size
preview. Tooltips provide full byte counts and explain solid dependencies or an
unavailable estimate. The preview updates on selection changes, panel resizing,
folder navigation and after copy/extract/test/hash operations. The original
local selected-size/focused-size/date display remains available on local panels.
The new labels are English, matching the existing remote feature strings.

## Implementation

`IRemoteSelection` is an optional extension interface with a separate GUID. It
does not alter the upstream folder interfaces or existing progress interface.
The Agent maps selected panel entries to engine item indices with the same
defaults as File Manager extraction, including alternate streams. Folder descendants are included in normal folder view;
flat mode follows the existing extraction behavior. Duplicated engine items are
counted once in the unpacked total, and overlapping physical ranges are merged
by the existing cache estimator.

Both preview and operation progress use `EstimateRemoteDownload`, which asks the
existing ZIP/7z/RAR/RAR5/ISO metadata planners for physical ranges and subtracts
cached blocks. Preview queries never read the archive stream, fetch missing
blocks, install callbacks or alter the cache's LRU order. A new query sees the
current cache contents; zero transfer is approximate, and retries may increase
actual bytes beyond the preview.

Nested/SFX offsets and unsupported layouts do not receive a physical-download
estimate. Unknown file sizes and overflowing size totals are reported as
unavailable. For automatic UI updates, selections with more than 4,096 panel or
expanded items are left unavailable. Download planning also requires
`engine item count * expanded selection count <= 2,000,000`, conservatively
bounding repeated solid-predecessor scans. These limits do not block extraction
or its operation-time estimate. The solid marker is advisory for the archive,
not a claim that every selected file depends on another file.

Panel copy/extract/test/hash operations suppress status notifications while the
worker owns the archive and its cache. A status refresh is queued after the worker
returns, with notifications restored before that queued message is handled.
This avoids reading a single-consumer cache concurrently from the GUI thread.

## Validation

```powershell
python Tests/RemoteArchive/build_msvc.py --http --rebuild --agent-check --bin-dir .build/msvc/selection-preview
python Tests/RemoteArchive/test_agent.py --exe .build/msvc/selection-preview/agentcheck.exe
python Tests/RemoteArchive/test_cancel.py --exe .build/msvc/selection-preview/agentcheck.exe
python Tests/RemoteArchive/test_redirect.py --exe .build/msvc/selection-preview/7z.exe --agent .build/msvc/selection-preview/agentcheck.exe
python Tests/RemoteArchive/test_retry.py --exe .build/msvc/selection-preview/7z.exe --agent .build/msvc/selection-preview/agentcheck.exe
python Tests/RemoteArchive/build_msvc.py --target filemanager --rebuild --bin-dir .build/msvc/selection-preview-local
```

The 60-scenario Agent suite now checks selection previews for ZIP, solid/non-solid
and encrypted 7z, solid/non-solid RAR/RAR5, ISO and multi-extent ISO, plus
unavailable TAR/WIM download estimates and preserved local behavior. A WIM fixture
stores a real NTFS alternate stream: the preview includes its bytes, and the
extracted stream contents are checked. It verifies directory,
flat, duplicated, empty, invalid and oversized selections. Each valid remote
preview is repeated 20 times while checking that HTTP request/body counters and
cache-hit counters do not change. It compares preview sizes with known extracted
file sizes, checks solid flags and confirms cached post-extraction ranges need
zero additional transfer. The same text formatter used by the panel is exercised
by the harness, including zero and large binary units. Fresh sources are tested.

Full builds are required after the Agent class layout change. An initial test
against an incremental build with stale objects crashed; a complete rebuild
resolved it before the successful regression runs.

Desktop visual acceptance is still pending because the native Computer Use
connection was unavailable and the user chose to continue development. Before
publishing, verify status text and tooltips in single/two-panel mode, narrow and
wide windows, cleared selections, returning to a local folder, Refresh, and
completion/cancellation of an extraction. No screenshot-based UI verification
is claimed by these headless checks.

The eight cancellation, 35 redirect and 71 retry regression scenarios also pass.
MSVC builds succeed with HTTP enabled and for the ordinary local-only File Manager.
