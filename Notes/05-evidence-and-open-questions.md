# Evidence, Study Limits, and Open Questions

**Timeline note:** this file records evidence from the initial study. After the request to begin implementation, the prototype and tests were completed; see [Initial implementation results](06-prototype-results.md) for the latest status.

For the current MSVC build and initial File Manager integration, see [the latest results](08-msvc-and-file-manager.md). The sections below remain the historical study record.

## Work Performed

- Inspected repository structure and Git status; the worktree was clean at the start of the study, and `Notes` was empty.
- Verified the version in [C/7zVersion.h](../C/7zVersion.h) and read the local build instructions.
- Read stream/archive contracts and traced local opening/extraction paths in Common, Console, Agent, and File Manager.
- Inspected ZIP, 7z, RAR/RAR5, TAR, and GZIP paths related to Seek and item selection.
- Inspected callbacks for multiple volumes, reopening, read-only properties, and zone metadata.
- Searched `CPP` and `C` for `winhttp`, `wininet`, `curl_easy`, `InternetOpen`, `HttpSendRequest`, and `URLDownload`; no matches were found. This is specific search evidence, not proof that every possible form of network code is absent.
- Reviewed official HTTP, WinHTTP, and libcurl references to validate the transport-layer design.

I did not read every project file line by line. The study focuses on the requested feature and its main paths. Reverse engineering was not used because the original source is available and the task was architectural source analysis.

## Confidence

| Conclusion | Evidence type | Confidence/limit |
|---|---|---|
| A source other than a local file can be injected | `IInArchive::Open`, `COpenOptions.stream`, and `CAgent::Open` | High, from source |
| CLI needs changes before Open | Enumeration and `CFileInfo` in Main/List/Extract | High, from source |
| Current ReOpen does not preserve an external stream | Clears stream and creates `CInFileStream` | High, from source |
| ZIP suits partial reading | Handler tail/central-directory/item-stream paths | High architecturally; savings require measurement |
| Solid compression can increase data needed for a selected file | Folder maps and decompression path preceding the item | High, from source; numerical cost was not measured |
| WinHTTP suits an initial Windows implementation | API documentation and fit with direct-link scope | Engineering choice; needs building and testing |
| Parser changes can probably be avoided | Separation of streams and handlers | Strong architectural inference; not proven for every format |
| The feature is release-ready | No implementation existed at study time | **False**; the later prototype is also not yet integrated into the product |

## Work Not Yet Done at the Initial Study Stage

7-Zip had not been built, no backend had been added, runtime code had not been changed, no sample archive had been downloaded, network usage had not been measured, and TLS/proxy behavior had not been tested. The preceding file's test plan described future requirements, not tests already passed.

The legal suitability of a new library or dependency was not determined. During implementation and distribution, review component licenses alongside the project's [License.txt](../DOC/License.txt); the study adds no actual dependency.

## Decisions to Resolve When Implementation Begins

| Question | Assumption used in the plan | When must it be resolved? |
|---|---|---|
| CLI or File Manager target? | Proof, then CLI, then File Manager | Before defining first-release scope; File Manager is required for graphical browsing |
| Windows only or multiple platforms? | Windows first, with replaceable transport interface | Before finalizing backend and build matrix |
| Must login links be supported? | Public direct links or signed URLs, without a browser session | Before designing auth UI |
| What happens without Range/validator? | Full fallback under an explicit policy, or error | Before finalizing UX and CLI options |
| RAM/cache/download limits? | Adjustable experimental values | After traces and fixture measurements |
| Accept links without a strong ETag for partial downloading? | Not by default; use a full snapshot | Before expanding compatibility or adding an object-version policy |
| Split archives required? | Deferred, with an explicit resolver | After proving single-volume archives |
| Minimum supported Windows/SDK? | Unspecified; do not break older local builds without a decision | Before choosing APIs and changing makefiles |

These questions did not prevent completion of the study. The user's subsequent “start” request authorized implementation; the stated assumptions were used for the initial prototype without downloading large public archives.

## External References

Reviewed on October 29, 2026. Their uses are documented next to the decisions in [Network source design](03-http-stream-design.md).

- [RFC 9110: Range requests](https://www.rfc-editor.org/rfc/rfc9110.html#section-14).
- [RFC 9110: If-Match](https://www.rfc-editor.org/rfc/rfc9110.html#section-13.1.1).
- [Microsoft: About WinHTTP](https://learn.microsoft.com/en-us/windows/win32/winhttp/about-winhttp).
- [Microsoft: WinHttpQueryHeaders](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpqueryheaders).
- [Microsoft: WinHttpSetTimeouts](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpsettimeouts).
- [libcurl: CURLOPT_RANGE](https://curl.se/libcurl/c/CURLOPT_RANGE.html).

## Most Useful Next Technical Step

Implement a tracing stream, then a small HTTP stream, and pass it to the existing ZIP and 7z engine with a local test server. A successful proof turns the architectural conclusion into implementation evidence and provides real numbers for choosing cache behavior before investing in UI integration.
