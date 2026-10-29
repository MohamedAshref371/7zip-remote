# Native 26.05 Build and Initial CLI Integration

Date: October 29, 2026.

**Historical milestone:** this file records the earlier portable-toolchain build.
Visual Studio C++ tools have since been installed, and an initial File Manager
integration added. See [the latest MSVC results](08-msvc-and-file-manager.md).

The current milestone builds the 26.05 engine and console from this repository
and adds optional direct-URL support to the regular console. File Manager is
still pending; the project is not yet release-ready.

## Build Environment

- Python 3.14 and the existing portable Zig 0.13.0/Clang toolchain are available.
- Visual Studio is installed, but its C++ toolset is unavailable; neither GNU
  make nor nmake is available on PATH.
- [build_native.py](../Tests/RemoteArchive/build_native.py) selects the Windows,
  multithreaded source lists from the upstream makefiles and builds them with
  C++17. The full Format7zF engine selection contains 283 translation units;
  the HTTP-enabled console contains 91. Resources and DLL exports are included.
- Optimized assembly is replaced by the existing portable C implementations.
  This is a development build, not the upstream optimized release build.
- Binaries are `.build/native/7z.exe` and `.build/native/7z.dll`; tools, caches,
  and temporary files remain under `.build/`. Installed 7-Zip is unchanged.
- Zig requires execution outside the sandbox in this environment because its
  compiler-cache operations fail inside it. Tests likewise need loopback access.

## Implemented CLI Behavior

- `l`, `x`, `e`, and `t` accept a single direct HTTP/HTTPS URL.
- URL detection happens before archive wildcard/path handling and local drive
  enumeration. Signed query characters and parameter order are preserved.
- [RemoteArchiveSource.h](../CPP/7zip/UI/Common/RemoteArchiveSource.h) owns the
  HTTP stream and supplies a sanitized filename hint to the existing handlers.
- Listing and extraction use the existing engine, password handling, member
  filters, and extraction callbacks. Format selection is automatic; ZIP and 7z
  are covered by integration tests.
- Archive update/delete/rename operations reject URL sources before networking.
- Query/userinfo is excluded from ordinary archive output and transport errors.
  HTTP status codes accompany transport diagnostics.
- The existing zone policy is respected; remote extraction uses Internet zone 3
  when marking is requested. No local-file ADS lookup is attempted on a URL.
- Console cancellation is checked between synchronous HTTP operations and body
  reads. It remains subject to synchronous request waits, not a hard deadline.
- `Z7_HTTP` gates the addition. Windows GCC and MSVC console makefiles include
  optional HTTP objects and WinHTTP linkage. Their toolchains were unavailable
  here, so the executed build route is the portable Python/Zig script.

## Verification

- The existing prototype suite passed **72 scenarios** using the locally built
  **26.05 DLL**, replacing its previous dependence on the installed 26.02 engine.
- [test_cli.py](../Tests/RemoteArchive/test_cli.py) passed **61 CLI scenarios**.
  These include local/remote item-list comparisons, `x/e/t`, exact output-byte
  checks, Unicode names, wildcard selection, encrypted headers, solid/non-solid
  archives, Internet-zone ADS, signed queries, rejected mutations, wrong
  passwords, and malformed/changed/unsupported HTTP responses.
- Extraction of the small selected member requested less than half of the ZIP
  and non-solid 7z fixture sizes. In one run, ZIP requested 853,760 bytes of a
  4,261,631-byte archive; non-solid 7z requested 262,735 of 4,194,894 bytes.
  The solid fixture requested effectively its entire compressed content.
  These figures sum requested ranges, including the initial probe; they are
  fixture measurements, not Internet performance guarantees.
- A separate console build **without `Z7_HTTP`** succeeded and passed local ZIP
  and password-protected 7z integrity tests using the same 26.05 DLL.
- Retained summary: [native-cli-results.json](native-cli-results.json). Full
  reports and per-command output remain under `.build/native/runs/`; prototype
  reports remain under `.build/remote/runs/`.

## Reproduce

```powershell
python Tests/RemoteArchive/build_native.py --http
python Tests/RemoteArchive/build.py
python Tests/RemoteArchive/test_remote.py --dll "$PWD/.build/native/7z.dll" --sevenzip "$PWD/.build/native/7z.exe"
python Tests/RemoteArchive/test_cli.py

& .\.build\native\7z.exe l 'https://example.com/archive.zip'
& .\.build\native\7z.exe x 'https://example.com/archive.zip' 'docs/readme.txt' -oOut
& .\.build\native\7z.exe t 'https://example.com/archive.7z' 'docs/readme.txt'
```

The URLs are placeholders and must be replaced by direct links satisfying the
transport requirements. Keep the development EXE and DLL together.

## Remaining Work

1. File Manager “Open URL”, source lifetime, read-only operations, and Refresh/ReOpen.
2. Successful real HTTPS/proxy interoperability and broader server compatibility.
3. Separate network-progress reporting and more responsive cancellation.
4. Redirects, bounded retries, and explicit full-download fallback policy.
5. Additional formats, ZIP64/SFX fixtures, remote volume resolution, and larger
   cache/latency workloads before broader support claims.

Servers must currently return correct single-range 206 responses, a known total
size, and a strong ETag on every response. No browser cookies, login pages,
automatic sharing-link resolution, remote writes, or full-download fallback are
implemented. HTTPS retains normal Windows certificate validation.
