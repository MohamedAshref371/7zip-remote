# File names while scanning archive headers

TAR, RAR, RAR5 and ISO handlers now report filenames as headers are parsed. File
Manager shows **Reading archive headers**, the last discovered path and the
discovered-item count in its existing progress dialog. This applies to local
and remote opening, including reads from persistent cache. Updates do not
perform extra stream reads or HTTP requests.

The name belongs to the last header successfully parsed, not a prediction of
the next HTTP request. For encrypted headers it is available only after
decryption. Other formats retain their existing progress behavior. Many very
fast updates may be coalesced by the dialog's existing timer; no scrolling list
or filename history is retained by the production UI.

`IArchiveOpenFileProgress` is an optional COM extension with its own IID. The
upstream `IArchiveOpenCallback` ABI is unchanged; handlers query the extension
once per open and retain it only for that synchronous operation. The common UI
bridge and offset callback forward it, including reopen callbacks. File Manager
updates its synchronized progress state and propagates stop/pause checks. RAR5
service records are not presented as filenames.

ISO reports each non-system directory entry as soon as it is parsed, instead
of waiting for the existing 16 MiB metadata-progress threshold. Paths use the
same Joliet/Rock Ridge decoding and name cleanup as the final file list. Parent
links are established while scanning so nested paths are available immediately.
Consecutive extents of a multi-extent file are reported/count once. The optional
callback is scoped to the open operation and adds no archive reads.

## Validation

Build with ISO progress: `.build/msvc/iso-file-progress/` (keep its matching
`7z.dll` beside the executable).

The 63-scenario suite covers TAR/PAX with an Arabic filename, RAR4/RAR5 and ISO
primary/Joliet/Rock Ridge trees, including Arabic nested names and multi-extent
files. It tests local and
remote reads, both fetch modes, cancellation at the fifth header and disk-cache
reopens. Names/counts are compared in order with the input fixtures. Recorded
HTTP ranges are identical with name logging enabled/disabled, and strict
cached reopens need only the one-byte source probe. The callback bridge is also
checked against the actual dialog state, including Unicode, cancellation and
detachment. Interactive desktop display has not been verified in this session.
The existing 62 Agent scenarios also pass on this build, including ISO selected
extraction and download estimates.

```powershell
python Tests/RemoteArchive/build_msvc.py --http --rebuild --agent-check --bin-dir .build/msvc/iso-file-progress
python Tests/RemoteArchive/test_open_file_progress.py --exe .build/msvc/iso-file-progress/agentcheck.exe
python Tests/RemoteArchive/test_agent.py --exe .build/msvc/iso-file-progress/agentcheck.exe
```
