# MSVC Build and Initial File Manager Integration

Date: October 29, 2026.

**Later update:** an explicit per-session ETag bypass is now implemented and has
passed real MediaFire HTTPS listing/extraction checks. See [ETag option results](09-etag-option.md).

Visual Studio C++ tools are now installed. The engine, console, and File Manager
build successfully through the upstream Windows nmake files. An initial remote
File Manager implementation is included; its Agent is integration-tested, but
the window interactions have not yet been verified.

## Environment and Build

- Visual Studio Enterprise 2026, installation version 18.8.12009.203.
- MSVC tools 14.51.36231; Windows SDK selected by vcvars64: 10.0.26100.0.
- Native x64 nmake builds include the project's MASM implementations and its
  warning-as-error settings. No installed 7-Zip binaries were replaced.
- [build_msvc.py](../Tests/RemoteArchive/build_msvc.py) discovers the installation
  with vswhere and captures the compiler environment. Objects and logs remain
  in `.build/msvc/`; HTTP-enabled binaries are in `.build/msvc/bin/`.
- The previous HTTP code used `GetTickCount64`, which is hidden by the project's
  older Windows target definitions. Request timing now uses `GetTickCount` with
  unsigned elapsed-time subtraction, including counter wraparound, allowing the
  native build without raising the whole project's Windows target.
- A separate File Manager build without `Z7_HTTP` also succeeded after these
  changes. GNU make remains unnecessary for this Windows build route.

## File Manager Implementation

- **File → Open URL...** accepts a direct HTTP/HTTPS archive URL without adding
  it to URL history. The address-path binding also recognizes a URL before
  applying Windows path normalization.
- Opening uses the existing archive-opening worker/progress dialog. The Agent
  creates and owns the HTTP source, and the normal handlers and folder interfaces
  provide browsing, password handling, extraction, and testing.
- Only a sanitized `[HTTP]` filename appears in the panel path/history. The
  actual URL is kept in the active session, preserving signed query strings.
- `CanUpdate` rejects remote sources in the operation layer, while the folder's
  read-only property drives existing UI/editor restrictions. This covers delete,
  rename, create-folder, and replacement of an externally edited member.
- Internet-zone data is supplied through the existing extraction policy and
  virtual-folder metadata, without attempting an ADS lookup on a URL.
- Explicit Refresh opens a fresh Agent/source and drops old cache only after
  the new archive opens successfully. The current subfolder is rebound when
  possible. Refresh inside a nested archive returns to the remote archive root.
- The temporary opening callback is cleared from the transport before returning
  the folder. Later reads do not retain a dangling pointer to that callback.
- Synchronous HTTP waits still limit cancellation responsiveness. During later
  extraction, the existing handler/extraction callbacks handle cancellation;
  transport reads do not retain the old opening-dialog callback.

## Verification

| Suite | Passed scenarios | Scope |
|---|---:|---|
| Prototype/stream | 72 | Updated stream code and MSVC-built 26.05 engine, including offsets above 4 GiB, timeout, cancellation, HTTP validation, hashes, and encrypted/solid archives |
| Native CLI | 61 | MSVC-built console and engine; local/remote listing and extraction, read-only behavior, signed queries, zones, and HTTP failures |
| File Manager Agent | 21 | The actual compiled File Manager/Agent objects, exercised without opening windows |

[AgentCheck.cpp](../Tests/RemoteArchive/AgentCheck.cpp) is linked against the
File Manager's compiled objects. It calls `IFolderManager`, enters a subfolder,
extracts and tests a selected member, checks the read-only property, and invokes
real mutation interfaces to confirm their rejection. The opening callback is
destroyed before extraction, checking the lifetime boundary. Its Python driver
compares output bytes and Internet-zone ADS, tests encrypted/solid archives,
opens fresh sessions, and verifies cancellation and HTTP rejection cases.

**Desktop UI verification is pending.** Both available computer-use entry points
reported that the native pipe was unavailable. No successful menu navigation,
dialog screenshots, GUI Refresh operation, or interactive cancellation test is
claimed. The Agent test establishes backend behavior, not the complete GUI flow.

Retained evidence: [msvc-filemanager-results.json](msvc-filemanager-results.json).
Full reports and logs remain in the ignored `.build/` tree.

## Reproduce and Try the Development Build

```powershell
python Tests/RemoteArchive/build_msvc.py --http --agent-check
python Tests/RemoteArchive/test_cli.py --exe "$PWD/.build/msvc/bin/7z.exe"
python Tests/RemoteArchive/test_agent.py

& .\.build\msvc\bin\7zFM.exe
```

Keep `7zFM.exe`, `7z.exe`, and `7z.dll` together in the output directory. Use
**File → Open URL...** with a direct archive link. The server must return valid
single-range 206 responses, a known total size, and a strong ETag on every
response. Browser login/sharing pages, redirects, retries, remote volumes, and
automatic full-download fallback remain unsupported.

Next acceptance work is the desktop flow: open, browse, selected extraction,
password prompt, cancellation, failed/successful refresh, and closing back to
local browsing. Successful real HTTPS/proxy interoperability and broader archive
formats also remain unverified. This is a development build, not a finished release.
