# File Manager network progress

HTTP-enabled progress dialogs show three separate rows while opening, extracting,
or testing a remote archive:

- Downloaded response-body bytes, with archive size as a reference.
- Average network speed since the operation's first network update, including
  request waits and decompression time. This is not instantaneous socket speed.
- Number of HTTP requests started by the operation, including the opening probe.

The downloaded count includes body bytes actually returned by WinHTTP, including
rejected data. It excludes HTTP/TLS overhead and bytes buffered internally by
WinHTTP. Repeated reads can exceed the archive size. Archive size is not a
remaining-download estimate. Cached reads can show zero bytes and zero requests
for an extraction or test after browsing. The opening probe's total is initially
unknown and is filled once validated.

Transport notifications run on the worker thread; the dialog reads a snapshot
under its existing critical section on the UI timer. Archive progress, speed,
and percentage retain their existing meanings. Operation-scoped adapters query
an optional extension-specific COM interface and detach callbacks before they
can outlive the opening/extraction callback. Existing callbacks need not support
the extension. Local operations receive no network notifications; builds without
HTTP retain the original dialog resource layout.

```powershell
python Tests/RemoteArchive/build_msvc.py --http --rebuild --agent-check --bin-dir .build/msvc/network-progress
python Tests/RemoteArchive/test_cli.py --exe .build/msvc/network-progress/7z.exe
python Tests/RemoteArchive/test_agent.py --exe .build/msvc/network-progress/agentcheck.exe
python Tests/RemoteArchive/build_msvc.py --target filemanager --rebuild --bin-dir .build/msvc/network-progress-local
python Tests/RemoteArchive/test_progress_layout.py --exe .build/msvc/network-progress/7zFM.exe --local-exe .build/msvc/network-progress-local/7zFM.exe
```

Validation: 99 CLI scenarios and 42 Agent scenarios pass. Agent checks compare
operation byte/request totals with the controlled HTTP server, check monotonic
updates and per-operation resets, cover cached and solid archive reads, and
verify local operations produce no network notifications. The harness also
exercises the actual File Manager opening and extraction callback adapters and
their synchronized state without changing decompression progress. Compiled
dialog-resource checks verify row bounds and separation from existing fields.

Interactive desktop verification is pending: the Computer Use native pipe was
unavailable in this session. Resource and backend checks do not establish
readability at every DPI, font, locale, or resized window width. Network labels
currently use English, consistent with other remote-extension UI text.
