# Deliverable Implementation Plan

This plan was written after the initial study. **Update:** a tracing stream, HTTP source, and standalone extraction prototype have been implemented with 72 passing scenarios; see [Implementation results](06-prototype-results.md). The remaining phases and broader product requirements are still future work. Rewriting handlers or compression algorithms is unnecessary.

**Latest status:** the native 26.05 engine and initial CLI integration are now implemented and tested. Phase 2 has a working development implementation; broader interoperability, transfer-progress reporting, and fallback policy still need work. Phase 3 (File Manager) remains pending. See [CLI integration results](07-cli-integration-results.md). Historical future-tense descriptions below record the original plan.

**Subsequent update:** an MSVC/nmake build of the engine, console, and File Manager now succeeds. Initial Phase 3 code includes “Open URL”, a retained read-only source, Internet-zone handling, and fresh-session refresh. Agent integration tests pass; desktop UI validation is still pending. See [MSVC/File Manager results](08-msvc-and-file-manager.md).

## Initial Product Scope

A direct link to a single-volume archive, file listing, extraction of selected items to disk, and item testing. Start with a small tool using the 7-Zip engine, then integrate into CLI, followed by File Manager to deliver the requested browsing experience.

Test ZIP/ZIP64 and 7z first, including encrypted and solid samples. Acceptance of another format by the engine does not justify claiming network support guarantees before its tests pass.

Outside the first release: server writes, browser cookies, sharing pages requiring JavaScript, automatic aggregation of multiple links, unbounded browsing inside nested archives, and support for every build platform at once.

## Phase 0: Prove the Reading Path with the Actual Engine

Create a wrapper with the proposed name `CTracingInStream` around `CInFileStream` that records the offset and size of every Read and Seek. Use `IInArchive` or the actual `CArc` path with local samples, then measure listing and extraction of items at the beginning, middle, and end.

Purpose: identify real read patterns before tuning the cache. An experiment using only Python zipfile does not establish 7-Zip reader behavior.

**Completion criterion:** reproducible traces, extracted files and SHA-256 matching the local reference extraction, and an explanation of solid-data consumption from the trace.

## Phase 1: Standalone Network Stream

Proposed files, not yet created:

```text
CPP/7zip/Common/HttpInStream.h / .cpp
CPP/7zip/Common/RangeCache.h / .cpp
CPP/7zip/UI/Common/ArchiveSource.h / .cpp
CPP/Windows/HttpTransportWinHttp.h / .cpp
```

The final location of `IHttpTransport` will depend on layer dependencies during implementation; `CPP/Windows` must not depend on UI. Add a separate harness and controlled local HTTP tests, then pass the stream to the handler without changing the parser.

**Completion criterion:** ZIP and 7z listing/extraction over HTTP, byte-for-byte agreement with local results, and no full download of the large ZIP in the selective-extraction fixture. The stream passes Read/Seek/EOF, cancellation, and content-change tests.

## Phase 2: Integrated CLI

1. Detect source type during command parsing before filesystem path rules.
2. Bypass local enumeration for a single URL while preserving archive-file selection options.
3. Supply listing and extract/test with metadata and a stream instead of calling `Find_FollowLink` on the URL.
4. Handle display names, errors, exit codes, progress, and fallback.
5. Reject update/delete/rename and other remote-source mutations with a specific message.
6. Update help and makefiles, and review new option names for conflicts.

Target usage, **not currently supported in this version**:

```powershell
7z l "https://example.com/data.zip"
7z x "https://example.com/data.zip" "docs/readme.txt" -oOut
7z t "https://example.com/data.7z" "docs/readme.txt"
```

Do not choose new switch names before reviewing the parser. Shell quoting matters for URLs containing `&`, but quoting alone does not fix the application's current local-file assumptions.

**Completion criterion:** these commands work in the modified build against a fixture, signed URLs remain unchanged, and local extraction still produces the same results.

## Phase 3: File Manager

- An “Open URL” button shows status during probing, then the item tree.
- Pass the stream from `OpenAsArc` to `CAgent` with an extended lifetime.
- Accurate source metadata: safe name, size, read-only status, origin, and transfer statistics.
- Handle Refresh/ReOpen through a source factory, invalidating cache when the version changes.
- Extract and open an item in a temporary file; handle errors, cancellation, and passwords without freezing the UI.
- Block remote archive modification paths, including reinserting a file edited by an external program.
- Pass zone metadata through the available layers instead of attempting to read ADS from a URL.
- Address-bar and history/bookmark support can follow the standalone path, with URL secrets redacted.

**Completion criterion:** a complete session from opening a URL through browsing, item extraction, cancellation, refresh, and closing, with resources released and the local-file path also tested.

## Phase 4: Broader Compatibility

RAR/TAR first, after traces and measurements. Then multi-volume support through an explicit `volume name → URL` mapping; signed URLs for individual volumes must not be inferred by replacing filenames or arbitrarily copying queries. Define origin/path policy, and give each volume its own size and validator.

For multi-volume sets, individual validators alone do not prove that every volume belongs to the same set version; prefer a fixed/trusted manifest or object versions together with engine integrity checks. The resolver supports callbacks throughout extraction, not just during Open.

libcurl and other platforms, parallel optimization, and persistent caching are separate steps after core correctness.

## Acceptance Tests Required During Implementation

| Group | Main cases | Required result |
|---|---|---|
| Stream contract | Zero bytes, partial read, EOF, Seek before zero and beyond end, invalid origins, offsets above 4 GiB, overflow | Match `IStream.h` without truncation or false EOF |
| Normal range | Valid 206, absent Accept-Ranges header, unsupported HEAD, Content-Length describing only the part | Correct size, position, and data |
| Invalid range | 200 after Range, wrong/missing Content-Range, short body, unexpected multipart, unexpected 416 | Clear failure or separate fallback, without mixing data |
| Source consistency | ETag changes at the same size, size changes, weak/missing ETag, 412 during extraction | Invalidate session; no false success |
| Connection | Redirects, loops, expired signed URL, invalid TLS, proxy, 429, mid-read interruption | Bounded, cancellable policy with understandable errors |
| Representation | Unexpected content coding, valid partial response using HTTP chunked transfer | Preserve offsets; distinguish transfer framing from content coding |
| Cache | Overlap, hit, eviction, disk space, incomplete block, different sessions | Never read holes as zeros or data from another source |
| ZIP | Stored/compressed, ZIP64, long comment, Unicode names, encryption, SFX, damaged directory | Matching listing/extraction or the same expected handler failure |
| 7z | Non-solid, single/multiple solid blocks, compressed/encrypted header, wrong password, late file | Correct data and realistic statistics |
| Format expansion | RAR4/5 and TAR, archive inside archive, missing volume | No general support claim before success |
| Product | l/x/e/t, file/folder selection, local files, cancel, refresh, read-only, zone metadata | Consistent behavior and accurate messages |

Verification method: a local fixture server with controllable responses for these cases and request logging, known archives, reference outputs, and hashes. HTTPS testing uses a test certificate trusted in the test environment; do not disable verification in production code. Do not depend on a changing public server to prove acceptance.

Compare **output files and item lists**, not just exit codes. Also review request counts and bytes, and compare cold/warm cache under controlled RTT. If the archive source is untrusted, internal hashes do not establish file trust; their purpose here is to detect differences between the two paths.

## Expected Change Size

The stream-level proof is relatively limited; product integration is larger because of parsing, metadata, callbacks, reopening, UI, and builds. There is insufficient basis for a precise day estimate before traces and network proof. The largest uncertainty is real-world compatibility and performance across formats and servers, rather than the ability to connect `IInStream` to the engine.
