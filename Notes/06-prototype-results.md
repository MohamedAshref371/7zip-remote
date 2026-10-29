# Initial Implementation: HTTP Source, Tracing, and Extraction Tool

Implementation date: October 29, 2026. This updates the study following the request to “start.”

## What Now Exists

A standalone `7zremote.exe` tool lists ZIP and 7z items and extracts or tests a selected item from a local file or direct URL. It uses the 7-Zip engine through a DLL without reimplementing parsers or decompression algorithms.

| File | Implemented functionality |
|---|---|
| [HttpRangeSource.cpp](../CPP/Windows/HttpRangeSource.cpp) | WinHTTP, ranges, validation of headers/ETag/size, errors, and timeouts |
| [HttpInStream.cpp](../CPP/7zip/Common/HttpInStream.cpp) | `IInStream` and `IStreamGetSize`, 64-bit position, and cache limited to 8 MiB |
| [TracingInStream.cpp](../CPP/7zip/Common/TracingInStream.cpp) | Records engine reads/seeks, separating opening and extraction phases |
| [Remote7z.cpp](../CPP/7zip/UI/Remote7z/Remote7z.cpp) | Experimental program, JSON listing, and item selection by index |
| [build.py](../Tests/RemoteArchive/build.py) | Windows x64 build using a portable compiler inside the project |
| [test_remote.py](../Tests/RemoteArchive/test_remote.py) | Fixtures, local HTTP server, and integration tests |

The implementation uses cache inside `CHttpInStream` and a separate WinHTTP layer rather than creating every proposed abstraction up front. `CArchiveSource`, a volume resolver, and UI integration are not yet implemented.

## Environment and Proof Limits

- Project interfaces and local streams from the **26.05** repository.
- Runtime engine: installed `C:\Program Files\7-Zip\7z.dll`, version **26.02**.
- Engine SHA-256: `69fd4df057985c40e510e2fac182881c7f85e90aa13ec703f763a8fdb2ce61f8`.
- Build: Zig 0.13.0 / Clang, Windows x64, C++17, with `-Wall -Wextra -Werror` for application code. Toolchain runtime warnings appeared in the first build.
- Visual Studio is present but the C++ toolset is unavailable; a compiler was downloaded to `.build/tools` without system installation. Sandbox restrictions on the compiler cache and loopback required appropriate execution permissions for building and testing.
- The full 26.05 engine was not built, the installed DLL and `7z.exe` were not replaced, and File Manager was not changed.

The result proves the new stream works with a real 7-Zip engine and interfaces; it is not final compatibility certification for a full 26.05 build.

## Measured Results

**The final run passed 72 scenarios.** Intentionally failing cases passed their rejection checks; this count does not mean 72 different archives.

The selected small file, `002-target.txt`, is **4800 bytes**, preceded by roughly 4 MiB of pseudorandom data. Every run starts with a fresh cache, and measurements include opening the archive and extracting the item:

| Archive | Size in bytes | HTTP body bytes read | Requests | Note |
|---|---:|---:|---:|---|
| ZIP with long comment | 6,359,571 | 854,548 | 5 | About 13.4% of the archive |
| Non-solid 7z | 6,292,169 | 525,002 | 4 | About 8.3% of the archive |
| Solid 7z | 6,292,823 | 4,719,960 | 20 | About 75%; preceding data must be decompressed |
| 7z with encrypted header | 6,292,337 | 525,170 | 4 | Succeeded with the correct password |
| AES-encrypted ZIP | 6,292,316 | 787,293 | 5 | Succeeded with the correct password |

SHA-256 values of local and network outputs matched the original files, and item lists matched. Tests covered files at the beginning, middle, and end, Unicode names, and item testing without output.

In the local trace for the same item, the non-solid 7z reader read **84 bytes** during extraction, whereas the solid reader read **4,456,448 bytes**. Both read 230 bytes during opening in this sample. The difference between engine demand and HTTP volume illustrates prefetch cost; 256 KiB blocks can be optimized.

The `streamcheck` test used a virtual source of **4,296,015,872 bytes**, reading **1,048,577 bytes** over 5 requests, while checking data beyond 4 GiB, block boundaries, cache hits, EOF, and overflow.

These are loopback measurements with specific fixtures, not an Internet speed benchmark or savings guarantee. The counter measures body bytes returned by WinHTTP to the application, excluding headers/TLS and internal buffering not delivered to the application.

## Verified Cases

- Valid Range without `Accept-Ranges`, valid chunked responses, and preservation of signed-query characters and order.
- Ignored Range returning 200; missing/duplicate/invalid Content-Range and ETag; weak ETag.
- Changed ETag or size, and HTTP 412; the stream stops without mixing versions.
- Truncated or excess body, incorrect Content-Length, unsupported content encoding, and multipart responses.
- Rejection of redirects in this prototype, and 403, 404, and 416.
- Missing or incorrect password; cleanup of newly created output on failure; no overwrite of an existing file.
- Cancellation before the first request and while waiting for a response, and an HTTP timeout test.
- `Read(0)`, short reads at cache boundaries, negative/invalid Seek and Seek beyond end, and 64-bit arithmetic.

## Observations During Implementation

1. The initial timeout test showed that session settings alone did not meet the expected short timeout; settings were added to the request and `WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT`. Behavior remained non-immediate: timeout returned after about **4.1 seconds** despite a 200 ms setting, and cancellation while waiting for a delayed response took about **0.9 seconds**. The synchronous mechanism does not justify claiming immediate cancellation or a strict deadline; the final UI requires a carefully designed async implementation. [WinHTTP reference](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags#winhttp_option_receive_response_timeout).
2. HTTP 200 is rejected at the headers and is not placed at a partial offset. There is no automatic full download.
3. A strong ETag is a strict requirement, including its presence in subsequent responses; this may reject servers that could be supported later.
4. Cache is fixed at 8 MiB with 256 KiB blocks. The samples do not establish performance under heavy eviction or high RTT.
5. Single-item extraction to an explicit path with CREATE_NEW does not implement timestamps, links, ADS, or Zone.Identifier, and does not execute outputs. This is a proof tool, not a complete replacement for the product extraction path.

## Running

Executable: `.build/remote/7zremote.exe`.

```powershell
python Tests/RemoteArchive/build.py
python Tests/RemoteArchive/test_remote.py

& .\.build\remote\7zremote.exe l zip "https://example.com/archive.zip" --dll "C:\Program Files\7-Zip\7z.dll"
& .\.build\remote\7zremote.exe x zip "https://example.com/archive.zip" --dll "C:\Program Files\7-Zip\7z.dll" --index 2 --out ".\result.bin"
```

The URL is an example and must be replaced with a direct link meeting the requirements. The [Tool guide](../Tests/RemoteArchive/README.md) explains building, passwords, tracing, and limitations.

The full report and CSV traces are in `.build/remote/runs/run-baybstum/`, which contains Git-ignored artifacts. A summary is retained in the repository: [remote-prototype-results.json](remote-prototype-results.json).

## Remaining Work Before Integration

Next, build and test the 26.05 engine itself, then integrate a source descriptor into CLI and metadata/Open/ReOpen paths, followed by an “Open URL” interface in File Manager.

Successful HTTPS connections, a real proxy, ZIP64 archives, SFX, and multiple volumes were not tested. The source test above 4 GiB covers the stream, not the ZIP64 parser. TLS validation uses Windows defaults without certificate-bypass flags. Redirects, retries, fallback, parallelism, and disk caching are not implemented.
