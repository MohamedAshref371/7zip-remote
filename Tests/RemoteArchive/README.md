# Remote archive prototype (Windows x64)

## HTTP redirects

The current shared transport follows 301/302/303/307/308 redirects, with a maximum
of five hops and one deadline for the entire range request. Relative Location
values are resolved without decoding signed paths/queries. HTTPS downgrades and
embedded credentials are rejected; cookies and authentication stay disabled.
The resolved destination is reused for subsequent reads in the session. Refresh
starts from the original user URL. See [redirect implementation and validation](../../Notes/18-http-redirects.md).

```powershell
python Tests/RemoteArchive/build_msvc.py --http --rebuild --agent-check --bin-dir .build/msvc/redirects
python Tests/RemoteArchive/test_redirect.py --exe .build/msvc/redirects/7z.exe --agent .build/msvc/redirects/agentcheck.exe
```

The manual fixture server also prints `redirect_url` for Open URL, selected
extraction, Refresh and cancellation acceptance tests.

## Optional ETag verification bypass

The transport and console verify ETags by default. At the user's request, the
File Manager's **File → Open URL...** dialog now starts with **Skip ETag verification**
checked. Uncheck it to require a strong ETag. It applies to that session
and is retained on Refresh; the panel path shows `[HTTP; ETag off]`. Each new
Open URL dialog starts with ETag bypass enabled.

The dialog also has an **Archive type** dropdown: **Auto** (default), **ZIP**,
**7z**, **RAR**, and **ISO**. RAR restricts detection to the RAR and RAR5 handlers and lets
the engine identify the generation. Auto uses normal format detection; other explicit
selections are passed to the existing handler selection. Refresh retains both the
type and ETag policy. Direct address-bar opens and the console retain their
existing strict ETag defaults. The latest GUI build is in `.build/msvc/iso-options/`.

The unified RAR choice is covered by `test_rar_family.py --rar4 PATH --rar5 PATH`,
using caller-supplied fixtures. It compares complete extracted file hashes with
local extraction and checks both generations, unknown URL extensions, Auto, and
ETag bypass. No RAR fixtures are downloaded or redistributed by the test.

The regular console exposes the same opt-in for `l/x/e/t`:

```powershell
& .\.build\msvc\etag-option\7z.exe l 'https://example.com/archive.zip' -shttp-no-etag
& .\.build\msvc\etag-option\7z.exe x 'https://example.com/archive.zip' 'docs/readme.txt' -oOut -shttp-no-etag
```

In this mode the client neither checks ETags nor sends `If-Match`. It still
checks the total size on subsequent responses, exact Content-Range, body length,
HTTP status, representation, and TLS certificates. Normal archive integrity
checks remain active. Changes that preserve the file size may go undetected;
use the option only when you trust the archive to remain unchanged.

Build the current development binaries into a separate folder with:

```powershell
python Tests/RemoteArchive/build_msvc.py --http --rebuild --agent-check --bin-dir .build/msvc/etag-option
python Tests/RemoteArchive/test_cli.py --exe "$PWD/.build/msvc/etag-option/7z.exe"
python Tests/RemoteArchive/test_agent.py --exe "$PWD/.build/msvc/etag-option/agentcheck.exe"
```

The fixture server prints `no_etag_url` for manual checkbox tests. Opening it
without the option must fail; with the option, browse/extract and Refresh should
work. See [ETag option results](../../Notes/09-etag-option.md).

## MSVC engine, console, and File Manager

The HTTP-enabled File Manager progress dialog now shows downloaded body bytes,
archive size, average network speed, and request count for the current operation.
See [network progress](../../Notes/14-network-progress.md) for semantics and checks.
Selected ZIP/7z extraction and testing also show a conservative approximate
download total alongside downloaded bytes. See [download estimates](../../Notes/15-download-estimates.md).
RAR/RAR5 and ISO now provide estimates too; the Agent suite generates stored RAR
and ISO/Joliet fixtures, including solid dependencies and separated ISO extents.
Optional compressed-fixture checks are documented in [RAR/ISO estimates](../../Notes/16-rar-iso-estimates.md).

For a manual desktop test after building, start a loopback-only fixture server:

```powershell
python Tests/RemoteArchive/serve_fixture.py
```

Keep it running and open `.build/msvc/bin/7zFM.exe`. Use **File → Open URL...**
with the printed `zip_url`. Browse `docs`, extract `target.txt` to a fresh local
directory, refresh, and return to local browsing. Try `encrypted_url` with the
printed test-only password, `slow_url` for cancellation, and `rejected_url` for
the unsupported-Range error. Current URLs are also saved to
`.build/msvc/manual-session.json`; request logs and reference files stay in the
session's directory. Ctrl+C stops the server. These are disposable test fixtures,
not Internet interoperability or completed desktop acceptance evidence.

With the Visual Studio Desktop development with C++ workload and Windows SDK:

```powershell
python Tests/RemoteArchive/build_msvc.py --http --agent-check
python Tests/RemoteArchive/test_cli.py --exe "$PWD/.build/msvc/bin/7z.exe"
python Tests/RemoteArchive/test_agent.py
python Tests/RemoteArchive/build.py
python Tests/RemoteArchive/test_remote.py --dll "$PWD/.build/msvc/bin/7z.dll" --sevenzip "$PWD/.build/msvc/bin/7z.exe"
```

`build_msvc.py` discovers Visual Studio through vswhere, loads the x64 toolchain
environment, and executes the upstream nmake files, including MASM assembly.
No tools are installed and installed 7-Zip is unchanged. `--target` selects
`engine`, `console`, `filemanager`, or `all`. Use `--rebuild` after header changes
to force recompilation of all dependents. `--agent-check` links a headless check
against the actual HTTP-enabled File Manager objects; it requires `--http`.

HTTP-enabled binaries are collected in `.build/msvc/bin/`; builds without HTTP
use `.build/msvc/bin-local/`. Logs and object files stay under `.build/msvc/`.
Build the engine and keep `7z.dll` beside `7z.exe` and `7zFM.exe`.

The HTTP-enabled File Manager has **File → Open URL...**. Its opening work uses
the existing worker/progress dialog; archive contents use the normal browsing
and extraction paths. Explicit Refresh opens a fresh source, preserving the
current subfolder when possible; refreshing a nested archive returns to the
outer remote archive. Failed refresh preserves the previous view. URLs and
signed queries remain session-only; displayed/history paths use a sanitized
`[HTTP]` name. Automatic persistence/restoration of remote sessions is not
implemented. Remote mutation and external-editor writeback are blocked.

The File Manager executable builds, and `test_agent.py` verifies its Agent
without windows: browsing, extraction/testing, encrypted headers, solid data,
zone marking, source consistency, callback lifetime, cancellation before open,
and rejected mutations. **Desktop UI interaction has not been verified** because
the computer-use native connection is unavailable in this session. See
[MSVC/File Manager results](../../Notes/08-msvc-and-file-manager.md).

## Native 26.05 engine and integrated console

The checkout now also supports an optional HTTP-enabled build of the regular
console. Using the same portable Zig compiler, build both binaries with:

```powershell
python Tests/RemoteArchive/build_native.py --http
python Tests/RemoteArchive/test_cli.py
python Tests/RemoteArchive/test_remote.py --dll "$PWD/.build/native/7z.dll" --sevenzip "$PWD/.build/native/7z.exe"
```

`build_native.py` reads the upstream makefile source lists for the Windows,
multithreaded configuration and uses C implementations instead of MASM assembly.
It builds resources and DLL exports as well as the complete selected engine
source list. This is a development build using Zig 0.13.0/C++17, not the upstream
optimized release toolchain. No downloads or system installation occur.
`--target engine|console`, `--jobs N`, `--zig PATH`, and `--out PATH` are available.
Without `--http`, the console retains its normal local-file behavior.

Keep `.build/native/7z.exe` and `.build/native/7z.dll` together:

```powershell
& .\.build\native\7z.exe l 'https://example.com/archive.zip'
& .\.build\native\7z.exe x 'https://example.com/archive.zip' 'docs/readme.txt' -oOut
& .\.build\native\7z.exe e 'https://example.com/archive.7z' 'docs/readme.txt' -oOut
& .\.build\native\7z.exe t 'https://example.com/archive.7z' 'docs/readme.txt'
```

These URLs are placeholders. The same strict Range/ETag and direct-link limits
described below apply. Formats are detected by the existing console engine;
ZIP and 7z have integration coverage. Passwords, item selection, overwrite
policy, timestamps, and extraction paths use the existing console mechanisms.
URL query/userinfo is excluded from normal archive display and diagnostics.
Remote data receives Internet-zone metadata when the selected zone policy
requires it (`-snz1` explicitly marks all extracted files). Network progress is
not yet shown separately from the existing archive progress. Cancellation is
checked through console callbacks during asynchronous WinHTTP waits. Pending
requests are closed on cancellation; see [responsive cancellation](../../Notes/17-responsive-cancellation.md).

The GCC console makefile accepts `Z7_HTTP=1` on Windows; the MSVC console build
accepts `Z7_HTTP=1` with a compiler supporting the HTTP code's modern C++ syntax.
The GCC route remains unexecuted; the MSVC route is now tested as described above.
The earlier portable milestone predates the initial File Manager integration.
See [current results](../../Notes/07-cli-integration-results.md).

## Standalone prototype

The harness uses this repository's 7-Zip stream/COM interfaces and local file
streams, plus an explicitly selected existing `7z.dll`. It does not modify the
regular `7z.exe` command line or File Manager.

## Build

Python 3 and Zig 0.13.0 for Windows x64 are sufficient. From the repository root:

```powershell
python -m pip install --target .build/tools ziglang==0.13.0
python Tests/RemoteArchive/build.py
```

The compiler, temporary files and caches stay under ignored `.build/`.
`build.py --zig PATH` selects an available compiler. The build script itself
never downloads dependencies. This build uses C++17; legacy makefiles are unchanged.

## Run

```powershell
$exe = '.\.build\remote\7zremote.exe'
$dll = 'C:\Program Files\7-Zip\7z.dll'

# List JSON records, then select a member by its returned index.
& $exe l zip 'https://example.com/archive.zip' --dll $dll
& $exe x zip 'https://example.com/archive.zip' --dll $dll --index 2 --out '.\result.bin'

# Test one member without writing it; use '7z' for the 7z format.
& $exe t 7z 'https://example.com/archive.7z' --dll $dll --index 2

# Local and remote inputs can both emit an engine read/seek trace.
& $exe l zip '.\archive.zip' --dll $dll --trace '.\reads.csv'
```

The URLs are placeholders. Servers must return correct single-range 206
responses with a known total size. A strong ETag, including on subsequent
requests, is required by default; the regular console and File Manager can
explicitly opt out as described above. The standalone prototype remains strict.
URLs must be ASCII/percent-encoded, without userinfo or fragments.
Signed query strings are preserved. Format selection is explicit: `zip` or `7z`.

`--password-env NAME` reads an archive password from an environment variable.
It does not provide server authentication. Passwords are not printed, but this
prototype does not guarantee erasure of every password memory copy.
`--timeout-ms N` controls WinHTTP timeouts; `--cancel-after-ms N` is a test aid.
Ctrl+C requests cancellation. Pending WinHTTP send/receive/read operations are
cancelled through the same asynchronous wait path; cancellation is not a hard
real-time guarantee.

Listing is JSON Lines on stdout. Statistics on stderr distinguish engine reads
from HTTP body bytes returned by WinHTTP; they exclude HTTP/TLS overhead and
internally buffered bytes not delivered to the application. Traces contain
offsets and phases, not URLs or passwords. A selected trace file is replaced.
Extraction uses CREATE_NEW and an explicit output path, never archive paths.
It removes its new output on extraction failure and preserves existing files.
It does not restore permissions, timestamps, links, ADS, or Internet-zone
metadata, and does not launch extracted files.

## Test

```powershell
python Tests/RemoteArchive/test_remote.py
```

Optional paths: `--exe PATH --dll PATH --sevenzip PATH`. The executable and DLL
must both be x64; select only a trusted DLL. Tests need loopback networking on
`127.0.0.1`. They generate fixtures with the selected 7-Zip executable and
Python's ZIP writer, then compare local/remote listings and extracted SHA-256
hashes. Fixtures, CSV traces and `report.json` remain under
`.build/remote/runs/` (tens of MiB per run).

The internal `streamcheck raw URL` command expects byte N to equal N modulo
251; it tests the stream contract and is not an archive command.

## Limits

- The following limits describe the original standalone milestone. Current
  CLI/GUI integration and redirect support are described above.
- No standalone format autodetection, multivolume
  archives, remote writes, browser authentication, retries or
  automatic full-download fallback.
- 256 KiB blocks, 8 MiB LRU cache, single consumer per source. The initial
  one-byte probe is counted but not cached. Values are provisional.
- HTTP errors invalidate the stream; retry with a new source. No mixing of
  versions or reuse of cache across sessions.
- HTTPS retains Windows certificate validation. Successful HTTPS/proxy
  interoperability has not been integration-tested in this run.
- Runtime validation used installed **7-Zip 26.02**. Source headers and local
  stream code are **26.05**. This is not a full build/test of the 26.05 engine.

See [the initial prototype results](../../Notes/06-prototype-results.md).
