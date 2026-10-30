# Bounded automatic HTTP retries

The shared WinHTTP range source now retries a failed GET up to **two additional
times**. This applies to the console, File Manager and standalone prototype.
Retries cover WinHTTP timeout, connection failure, reset/termination, resend and
unparseable-server-response errors, plus HTTP **408, 429, 500, 502, 503 and 504**.
An early disconnect before headers was observed as WinHTTP error 12152, so that
error is included; a persistent malformed HTTP response still exhausts the same
small budget. DNS, TLS certificate, cancellation, access errors and other HTTP
statuses do not trigger retries. Parsed range, representation, length and source
identity/size validation failures remain terminal. Premature body EOF is treated
as an interrupted transfer and may retry.

The first wait is at least 250 ms and the second at least 500 ms. A valid
`Retry-After` can increase either wait. Delay-seconds and round-trippable
IMF-fixdate values are accepted. Duplicate, malformed, overflowing or unsupported
date forms suppress retries conservatively. In particular, WinHTTP's date parser
was observed accepting an overflowing numeric value as a date; numeric input is
now parsed exclusively as a bounded integer, and dates must round-trip through
the canonical HTTP format. A requested wait that cannot fit the remaining range
deadline returns the original error immediately rather than retrying early.

One deadline covers request I/O, every redirect and every backoff interval. The
five-hop redirect budget is also shared across retries; neither budget restarts
after a failure. Backoff checks cancellation at intervals of at most 50 ms on
the operation thread. Callback and event cancellation return `E_ABORT` without
another attempt. As before, a stalled asynchronous request closes and drains its
final callback before its context or buffers are freed.

An interrupted body is discarded completely. The next attempt requests the same
full range from the resolved destination, keeping If-Match in strict mode and
validating the complete response before publication. Earlier cached ranges are
not combined with an unverified partial body. Successful recovery clears the
temporary error and restores source readiness; terminal failure invalidates it.
ETag bypass retains its existing limitation for changes that preserve file size.

Request counters include application retry attempts. Delivered body bytes from
discarded partial attempts still count as downloaded bytes, so a retry can make
actual download volume exceed the initial estimate. This uses the existing
progress counters and does not add a new dialog or on-disk cache. WinHTTP's own
internal connection behavior is outside the application request counter.

Policy references: [HTTP idempotent requests](https://www.rfc-editor.org/rfc/rfc9110.html#section-9.2.2),
[Retry-After](https://www.rfc-editor.org/rfc/rfc9110.html#section-10.2.3),
[WinHTTP error definitions](https://learn.microsoft.com/en-us/windows/win32/winhttp/error-messages).

## Validation

```powershell
python Tests/RemoteArchive/build_msvc.py --http --rebuild --agent-check --bin-dir .build/msvc/retries
python Tests/RemoteArchive/test_retry.py --exe .build/msvc/retries/7z.exe --agent .build/msvc/retries/agentcheck.exe
python Tests/RemoteArchive/test_cli.py --exe .build/msvc/retries/7z.exe
python Tests/RemoteArchive/test_agent.py --exe .build/msvc/retries/agentcheck.exe
python Tests/RemoteArchive/test_redirect.py --exe .build/msvc/retries/7z.exe --agent .build/msvc/retries/agentcheck.exe
python Tests/RemoteArchive/test_cancel.py --exe .build/msvc/retries/agentcheck.exe
python Tests/RemoteArchive/build.py
python Tests/RemoteArchive/test_remote.py --dll "$PWD/.build/msvc/retries/7z.dll" --sevenzip "$PWD/.build/msvc/retries/7z.exe"
python Tests/RemoteArchive/build_msvc.py --target filemanager --bin-dir .build/msvc/retries-local
```

The new suite passes 71 controlled loopback scenarios. It checks each retryable
status during probing and data reads, retry exhaustion, permanent statuses,
disconnects before headers, receive timeout, short fixed-length/chunked bodies,
exact recovered byte patterns, discarded-byte accounting, source invalidation
on failure, cleared errors after success, source changes following a failed
attempt, ETag bypass, Retry-After seconds/dates/invalid values, deadline limits,
redirects, selected extraction hashes in both frontends, progress request counts,
and callback/event cancellation while waiting in backoff.

The existing 99 CLI, 58 Agent, 35 redirect, 72 stream/prototype and eight
cancellation scenarios also pass. MSVC HTTP builds, the local-only File Manager
build and the portable Zig build succeed. One cancellation run under concurrent
suite load exceeded its three-second opening-readiness guard before the timed
extraction phase; rerunning that latency-sensitive suite alone passed all eight
cases with the existing thresholds unchanged. Timed frontend cancellation then
completed in about 0.3-0.4 seconds. All 68 repeated stalled requests closed.
The prototype timeout fixture now withholds headers until disconnection instead
of sleeping for three seconds: retries could legitimately recover from that
finite delay, so it no longer reliably represented a persistently stalled server.

Desktop interaction and Internet/proxy interoperability remain unverified for
this change. No claim is made to resolve the previously observed WinHTTP internal
event retention after cancellation; see [the diagnostic](17-responsive-cancellation.md).
