# Actionable remote archive diagnostics

The HTTP transport now distinguishes authentication requirements, access denial,
missing/removed files, proxy authentication, rejected ranges, rate limits, and
server failures. Messages suggest a fresh direct link, reopening the archive,
waiting, checking the connection, or downloading locally as appropriate.

WinHTTP failures distinguish timeouts, name resolution, connection failures,
and HTTPS certificate verification. Original HRESULTs remain available. The
response status is cleared before sending each request so a failed connection
does not inherit an earlier successful HTTP status.

Missing strong ETags explain both the local-download option and the existing
explicit bypass, including its consistency limitation. No automatic retry,
redirect, full download, or ETag-policy change is introduced. Error messages
continue to exclude URLs, signed queries, response bodies, and credentials.

Build and verification:

```powershell
python Tests/RemoteArchive/build_msvc.py --http --rebuild --agent-check --bin-dir .build/msvc/network-errors
python Tests/RemoteArchive/test_cli.py --exe .build/msvc/network-errors/7z.exe
python Tests/RemoteArchive/test_agent.py --exe .build/msvc/network-errors/agentcheck.exe
```

The Windows x64 MSVC engine, console, File Manager and Agent harness built
successfully. The CLI suite passed 99 scenarios, including connection refusal
and actionable errors for listing and testing. The Agent suite passed 42 scenarios, including seven checks that
the new messages reach the File Manager backend. Desktop dialogs were not
interactively verified. The HTTP fixture covers both listing and testing errors,
checks HTTP status reporting, and verifies that signed-query secrets stay out of
diagnostics. DNS, TLS, and timeout message branches require separate targeted
validation; loopback tests do not establish Internet interoperability.
