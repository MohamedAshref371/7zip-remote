# 7-Zip Remote Archive Support

A Windows development build based on **7-Zip 26.05** that can open archives from
direct HTTP/HTTPS URLs, browse their contents, and extract selected files using
HTTP byte-range requests.

The original 7-Zip engine and format handlers are reused. Depending on the archive
and its compression layout, selected extraction can avoid downloading the entire
archive.

**Status:** development and compatibility testing are in progress. Windows x64
is the tested platform. See the [implementation notes](Notes/README.md) for the
design, evidence, and remaining work.

## Features

- **File Manager:** File → Open URL..., archive browsing, selected extraction,
  password handling, and fresh-session Refresh.
- **Archive type selection:** Auto, ZIP, 7z, RAR, or ISO. The single RAR choice detects
  the appropriate RAR/RAR5 handler internally. Refresh retains the selected type.
- **Console:** direct URLs for `l`, `x`, `e`, and `t`.
- **HTTP input stream:** WinHTTP transport, seekable reads, a bounded memory
  cache, and validation of ranges, lengths, and source size.
- **Redirects:** follow HTTP 301/302/303/307/308, including relative links, with
  at most five hops per range request. HTTPS-to-HTTP redirects are rejected.
- **Automatic retries:** up to two retries for temporary network failures and
  HTTP 408/429/500/502/503/504. Waits are cancellable, respect `Retry-After`,
  and share the original range-request deadline.
- **Network diagnostics:** actionable messages for denied or expired links,
  authentication requirements, rate limits, server failures, unsupported partial
  downloads, and connection errors. Messages exclude URLs and access tokens.
- **File Manager network progress:** downloaded body bytes, an approximate total
  download for selected ZIP/7z/RAR/RAR5/ISO extraction/testing, average transfer speed, and
  HTTP request count. Archive size is shown separately as a reference.
  Estimates account for cached blocks; solid 7z uses the full compressed block
  conservatively; RAR follows solid predecessors and ISO uses individual extents.
  Other formats, unsupported layouts, and archive opening show an unavailable estimate.
- **ETag policy:** the Open URL dialog defaults to **Skip ETag verification**
  checked. Uncheck it to require a strong ETag. The console verifies ETags by
  default and exposes `-shttp-no-etag` to opt out.
- **Read-only remote sessions:** archive update, deletion, renaming, and editor
  writeback are blocked. Extraction uses the existing output and zone policies.

When ETag verification is skipped, file changes that preserve the total size can
go undetected. Range, length, size, TLS, and the archive handlers' integrity checks
remain active. Enable verification when the server provides a strong ETag.

## Build

Requirements:

- Windows with **Visual Studio Desktop development with C++**, the MSVC x64/x86
  tools, and a Windows SDK.
- Python 3.

From the repository root:

```powershell
python Tests/RemoteArchive/build_msvc.py --http --rebuild --agent-check --bin-dir .build/msvc/retries
```

The script discovers Visual Studio, loads its x64 environment, and builds the
engine, console, and File Manager through the upstream `nmake` files, including
MASM assembly. A separate GNU make installation is unnecessary for this route.
`--agent-check` also builds the headless integration harness.

The output directory contains `7z.dll`, `7z.exe`, `7zFM.exe`, and
`agentcheck.exe`. Keep the DLL beside the executables. Build artifacts and logs
stay under `.build/`; the installed copy of 7-Zip is unchanged.

Without `--http`, the build uses the ordinary local-file behavior. Portable
Zig/Clang build instructions are available in the [test and build guide](Tests/RemoteArchive/README.md).

## Use

### File Manager

```powershell
& .\.build\msvc\iso-options\7zFM.exe
```

1. Choose **File → Open URL...** and paste a direct archive URL.
2. Leave **Archive type** on **Auto**, or choose ZIP, 7z, RAR, or ISO if known.
3. Select the desired ETag policy; bypass is checked by default in this dialog.
4. Browse and extract items through the normal File Manager controls.

**View → Refresh** opens a fresh source and retains the type and ETag choices.
The current subfolder is restored when possible; refreshing inside a nested
archive returns to the outer remote archive. A failed refresh preserves the
previous view. URLs are kept in the session; displayed/history paths use a
sanitized archive name.

### Console

```powershell
$sevenzip = '.\.build\msvc\retries\7z.exe'

& $sevenzip l 'https://example.com/archive.zip'
& $sevenzip x 'https://example.com/archive.zip' 'docs/readme.txt' -oOut
& $sevenzip t 'https://example.com/archive.7z' 'docs/readme.txt'

# For servers without a strong ETag:
& $sevenzip l 'https://example.com/archive.zip' -shttp-no-etag
```

These URLs are placeholders. The unified RAR selection described above belongs
to the remote File Manager interface; standard console format switches retain
their existing behavior.

## Server Requirements and Limits

- URLs must point directly to archive data. Correct single-range **HTTP 206**
  responses and a known total size are required. Strict mode additionally needs
  a strong ETag on every response.
- Remote archives are read-only and currently single-volume.
- Browser login/cookies, sharing-page resolution,
  and automatic full-download fallback are not implemented.
- Failed partial ranges restart from their beginning; retry bytes count toward
  actual downloads. Source identity, range and size checks remain active.
  ETag bypass retains its existing same-size-change limitation.
- Redirect destinations must satisfy the same HTTP(S), range and version checks.
  Cookies and authentication remain disabled. Signed query strings are preserved
  as supplied by each Location; old query parameters are not appended to new URLs.
- Solid compression may require reading and decompressing preceding data,
  potentially most or all of the archive. Savings depend on the archive layout.
- HTTP requests can be cancelled while response headers or body data are pending,
  with cancellation checks during asynchronous waits. Separate network statistics are shown in File Manager progress
  dialogs; the console does not yet display these statistics.
- Remote compatibility has coverage for ZIP, 7z, and single-volume RAR/RAR5
  samples. Other handlers and advanced format features need separate validation.

## Tests and Validation

```powershell
python Tests/RemoteArchive/test_cli.py --exe "$PWD/.build/msvc/retries/7z.exe"
python Tests/RemoteArchive/test_agent.py --exe "$PWD/.build/msvc/retries/agentcheck.exe"
python Tests/RemoteArchive/test_redirect.py --exe "$PWD/.build/msvc/retries/7z.exe" --agent "$PWD/.build/msvc/retries/agentcheck.exe"
python Tests/RemoteArchive/test_retry.py --exe "$PWD/.build/msvc/retries/7z.exe" --agent "$PWD/.build/msvc/retries/agentcheck.exe"

# Supply your own local RAR-generation fixtures:
python Tests/RemoteArchive/test_rar_family.py --rar4 'path/to/legacy.rar' --rar5 'path/to/modern.rar'

# Local server for manual File Manager tests:
python Tests/RemoteArchive/serve_fixture.py
```

Recorded validation across the development milestones:

| Suite | Passing scenarios | Coverage |
|---|---:|---|
| Stream/prototype | 72 | Read/Seek, offsets above 4 GiB, HTTP validation, cache, cancellation, and extraction hashes |
| Console | 99 | Local/remote commands, actionable diagnostics, ETag policies, selected extraction, zones, and failure cases |
| File Manager Agent | 58 | Browsing, extraction/testing, passwords, read-only enforcement, format selection, network counters, and ZIP/7z/RAR/ISO estimates |
| Compressed RAR estimates | 10 | RAR/RAR5 metadata plans, solid predecessors, encrypted RAR4 headers/data, ETag policies, and extracted hashes |
| Cancellation | 8 | Stalled headers/body during opening/extraction, repeated callback/event cancellation, closed connections, and partial-range rejection |
| RAR family | 8 | Both generations, Auto/shared RAR selection, unknown URL extensions, ETag bypass, and output hashes |

A real MediaFire HTTPS link was also tested with ETag bypass: listing and selected
extraction succeeded, and the extracted file matched the local reference.
The maintainer confirmed the earlier GUI URL-opening flow. Full interactive
acceptance testing of the latest dialog and Refresh flow remains pending;
headless Agent tests cover the backend rather than desktop interaction.

See [ETag results](Notes/09-etag-option.md), [RAR-family results](Notes/11-rar-family.md),
and the [full test guide](Tests/RemoteArchive/README.md). Local archive fixtures
and generated artifacts are not required to be committed to the repository.

## Codex Contributions

The remote-archive extension was developed with **Codex, OpenAI's coding
assistant**, under the repository maintainer's direction. Codex contributed:

- Source analysis, feasibility research, integration mapping, and implementation notes.
- The WinHTTP range transport, seekable HTTP stream, memory cache, and tracing tool.
- Console and File Manager integration, remote read-only enforcement, and zone handling.
- Configurable ETag verification, archive type selection, and unified RAR/RAR5 detection.
- MSVC and portable build scripts, controlled HTTP fixtures, integration tests,
  and verification against local reference files.

The maintainer supplied requirements, compatibility examples, and manual GUI
feedback. The original compression algorithms, archive readers, and 7-Zip
application are the work of **Igor Pavlov and upstream contributors**.

## Upstream and Licensing

This repository is based on Igor Pavlov's 7-Zip sources. Refer to
[the upstream source README](DOC/readme.txt) and [license information](DOC/License.txt)
for the original project and component-specific notices, including the unRAR
restriction. Preserve the applicable upstream notices when distributing builds.
