# Study of Direct-URL Archive Support in 7-Zip

Study date: October 29, 2026.
Local version: **7-Zip 26.05**, with source release date `2026-10-21`.
Git reference at the time of the study: `8204ba7d01f8cd73abee5e2f82ac98e7ce3edef9`.

**Latest implementation update:** the engine, console, and File Manager now build with MSVC/nmake. The optional HTTP-enabled File Manager includes an initial “Open URL” implementation, backed by an Agent tested for browsing, extraction, and read-only enforcement. The prototype, CLI, and Agent suites passed 72, 61, and 21 scenarios respectively. Desktop UI verification remains pending because the computer-use connection is unavailable. See [MSVC and File Manager results](08-msvc-and-file-manager.md). The following sections summarize the original study and broader design.

## Conclusion

**Opening an archive from a direct URL, listing its contents, and extracting selected files is architecturally feasible.** The best integration point is a new data source implementing `IInStream`, translating reads into partial HTTP requests with caching. Existing format readers and decompression can be reused.

The project does contain low-level components in `C` and `Asm`, but the requested addition belongs primarily in the C++ data-source and user-interface layers. There is no initial need to modify LZMA or write a new ZIP/RAR parser.

“Like a local file” is achievable for **browsing, reading, and extraction**, but does not imply equal speed, guarantee downloading only the requested file, or include editing the archive on the server. Download volume depends on server support, archive format, and compression method.

## Proposed Decisions

| Topic | Recommendation |
|---|---|
| Integration approach | `CHttpInStream` behind `IInStream`, with a source model distinguishing URLs from local paths |
| Starting scope | Windows, direct HTTPS URL, single-volume archive, listing/extraction/testing |
| Initial formats | ZIP/ZIP64 and 7z, with clear solid-compression distinctions |
| HTTP backend | WinHTTP for the first Windows version; a transport interface allowing libcurl later |
| Server lacks partial reads | Full download to a temporary file under an explicit setting, or a clear error |
| Archive consistency between requests | Strong ETag and conditional requests; lack of consistency evidence defaults to full download |
| Writing to the URL | Out of scope: no update, delete, or automatic upload |
| Multiple volumes | Later phase mapping volume names to explicit URLs |

These are engineering choices for the study, not settings currently available in the application.

## Study Files

1. [Project map and integration points](01-source-map.md): opening/extraction paths and source references.
2. [Partial-reading feasibility by format](02-format-feasibility.md): ZIP, 7z, RAR, TAR, and practical limits.
3. [Network source design](03-http-stream-design.md): protocol, cache, errors, and data consistency.
4. [Implementation and verification plan](04-implementation-plan.md): deliverable phases and acceptance tests.
5. [Evidence and study limits](05-evidence-and-open-questions.md): what was reviewed and what requires implementation experiments.
6. [Initial implementation results](06-prototype-results.md): new code, measurements, commands, and testing limits.
7. [26.05 build and CLI integration](07-cli-integration-results.md): current development binaries, CLI verification, and remaining work.
8. [MSVC build and File Manager integration](08-msvc-and-file-manager.md): latest build commands, Agent verification, and desktop-testing limits.

## Illustrative Benefit

For a 20 GiB ZIP, downloading its directory and a 5 MiB compressed file's data, plus some additional reads, may be enough to extract that file. This is a **conditional illustration, not a measurement**. If the file is inside a large solid block, a substantial part of that block may need to be read and decompressed. If the server does not support partial reads, the application will need to download the entire archive.

## Work Status

The study, standalone prototype, 26.05 engine build, and CLI integration are complete. Initial File Manager code now supports opening direct URLs and refreshing with a fresh session, with Agent-level coverage for local/remote browsing, extraction, passwords, zones, and rejected mutations. The desktop interaction itself has not been verified. Successful public HTTPS/proxy interoperability, redirects, retries, full-download fallback, and remote volume resolution remain future work. This is a development milestone, not a release-ready product.
