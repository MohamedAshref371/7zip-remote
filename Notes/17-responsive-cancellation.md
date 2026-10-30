# Responsive remote cancellation

HTTP send, response-header and body-read operations now use asynchronous WinHTTP.
The caller still sees a synchronous seekable stream, but waiting polls the
existing cancellation callback/event at intervals of at most 50 ms. Application
callbacks and progress notifications run on the original operation thread;
WinHTTP callbacks only publish completion/error information and signal events.
Configured timeouts and the full-request deadline remain active.

Cancelling returns `E_ABORT` with `Cancelled.` and closes the outstanding request.
The request destructor waits for its final `HANDLE_CLOSING` notification before
releasing its callback context, events or any read/header buffers. Buffers are
declared before the request guard so exception/early-return unwinding has the
same lifetime guarantee. An incomplete range never reaches the cache or caller.
This follows Microsoft's [request-close contract](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle)
and [asynchronous read-buffer requirements](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpreaddata).

File Manager extraction/testing now attaches the current extraction callback to
the source's cancellation check for that operation. It is detached before the
callback can expire, just as the opening callback already was. Existing console
and prototype cancellation hooks use the new wait path without moving callbacks
to network threads. Local archive behavior is unchanged.

Validation:

```powershell
python Tests/RemoteArchive/build_msvc.py --http --rebuild --agent-check --bin-dir .build/msvc/fast-cancel
python Tests/RemoteArchive/test_cancel.py --exe .build/msvc/fast-cancel/agentcheck.exe
python Tests/RemoteArchive/test_cli.py --exe .build/msvc/fast-cancel/7z.exe
python Tests/RemoteArchive/test_agent.py --exe .build/msvc/fast-cancel/agentcheck.exe
python Tests/RemoteArchive/build.py
python Tests/RemoteArchive/test_remote.py --dll "$PWD/.build/msvc/fast-cancel/7z.dll" --sevenzip "$PWD/.build/msvc/fast-cancel/7z.exe"
python Tests/RemoteArchive/build_msvc.py --target filemanager --rebuild --bin-dir .build/msvc/fast-cancel-local
```

Windows x64 MSVC builds with/without HTTP and the independent Zig prototype build
succeeded. All 99 CLI, 58 Agent, and 72 stream/prototype regression scenarios
passed. The new cancellation suite passed eight scenarios:

- Open with the server withholding headers, or stopping partway through a body.
- Extract an uncached ZIP member with headers/body withheld after opening.
- Repeat stalled-header and stalled-body cancellation 17 times each, using both
  callback and event cancellation (68 repeated operations total).

Every blocked connection was observed closing at the fixture server. Each
repeated operation returned `E_ABORT` in under 1.5 seconds despite a configured
10-second timeout, published no partial range, and kept cancellation callbacks on
the originating thread. The four frontend cases took about 0.3-0.6 seconds from
the start of their timed phase, with cancellation requested after 300 ms. These
are measured loopback results, not a universal real-time latency guarantee.

Existing output files were preserved; newly opened extraction output was closed
and could be renamed after abort. No transport temporary files remained. This
does not add a deletion policy for partially extracted files: the existing 7-Zip
output policy remains in effect.

Resource diagnostic limitation: process-wide event-handle counts grew across
repeated aborts and did not immediately return to baseline. A separate minimal
WinHTTP-only client reproduced the growth without the project wrapper, with some
events released during subsequent settling. Therefore those counts are recorded
as diagnostics, not treated as proof that all WinHTTP-internal retained state
has disappeared. Pass criteria check every blocked socket closing, final request
callback drainage, callback-thread ownership and buffer/output lifetime instead.
This change does not claim to resolve that WinHTTP-internal resource behavior.

Desktop button interaction and Internet/proxy cancellation latency still need
interactive acceptance testing. No UI layout change was made.
