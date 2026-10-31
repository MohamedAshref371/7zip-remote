# Post-v0.5.0 development validation

Development version: **0.6.0-dev**, based on upstream 7-Zip 26.05. This work does
not replace, retag or publish the existing v0.5.0 release.

## Performance measurements

`benchmark_remote.py` generates valid ZIP/TAR/RAR5/solid and non-solid 7z archives
containing 256 small data files and one selected text file. It measures one
end-to-end open/extract/test sample per cell, with either zero or 20 ms added to
each loopback HTTP request. Outputs are compared byte-for-byte with the source.
This is a controlled workload, not a complete internet/bandwidth simulation.

Selected results with 20 ms added per request:

| Archive | Mode | Body bytes | Requests | Seconds |
|---|---|---:|---:|---:|
| RAR5 | 256 KiB blocks | 1,061,276 | 6 | 0.404 |
| RAR5 | Exact reads | 13,668 | 517 | 12.064 |
| RAR5 | Exact, warm disk cache | 1 | 1 | 1.013 |
| TAR | 256 KiB blocks | 1,187,841 | 6 | 0.504 |
| TAR | Exact reads | 139,777 | 269 | 6.388 |
| TAR | Exact, warm disk cache | 1 | 1 | 0.622 |
| ZIP | 256 KiB blocks | 555,441 | 4 | 0.375 |
| ZIP | Exact reads | 132,097 | 3 | 0.238 |
| ZIP | Exact, warm disk cache | 1 | 1 | 0.221 |

Keep both explicit modes: exact reads favor reduced transferred bytes; larger
blocks can be much faster for sequential headers. No automatic mode silently
changes the exact-read contract. A third adaptive mode is deferred until there
is wider real-server evidence and a separately tested policy.

The benchmark exposed repeated crypto-provider setup costs. A provider is now
owned by each single-consumer cache and reused; each header and payload still
gets its own integrity calculation. In the RAR5 zero-delay warm-cache sample,
observed time fell from 3.693 s to 0.988 s. These are individual measurements,
not timing guarantees.

Reports remain under `.build/benchmarks/remote-4blx58w_/` (before provider reuse)
and `.build/benchmarks/remote-774db9gn/` (after), with JSON and Markdown output.

A second run increased the archive to 1,024 data files plus the selected file.
Selected results with 20 ms added per request:

| Archive | Mode | Body bytes | Requests | Seconds |
|---|---|---:|---:|---:|
| RAR5 | 256 KiB blocks | 4,237,724 | 18 | 0.689 |
| RAR5 | Exact reads | 44,388 | 2,053 | 47.165 |
| RAR5 | Exact, warm disk cache | 1 | 1 | 3.420 |
| TAR | 256 KiB blocks | 4,730,881 | 20 | 0.757 |
| TAR | Exact reads | 537,089 | 1,045 | 24.151 |
| TAR | Exact, warm disk cache | 1 | 1 | 1.841 |
| ZIP | 256 KiB blocks | 648,369 | 4 | 0.430 |
| ZIP | Exact reads | 132,097 | 3 | 0.254 |
| ZIP | Exact, warm disk cache | 1 | 1 | 0.223 |

The complete larger run, including both 7z variants and cold cache timings, is
in `.build/benchmarks/remote-xbytyzed/report.md` and `report.json`. It confirms
that request latency dominates exact sequential header scans; warm caching
removes the repeated body downloads but still has local indexing/hash costs.

## Compact cache

New exact entries use manifest `7ZHC03I` and one `parts.bin` file. Each append
contains a checksum-protected header and the exact body bytes; an in-memory
range index is rebuilt on opening. The manifest, ownership markers and session
lock remain separate small files. Older `7ZHC01I` fixed-block and `7ZHC02I`
exact-part entries remain readable. Repairing a legacy exact part writes a new
packed record without altering the other old parts.

An interrupted final append is not indexed. Header corruption stops indexing
at the invalid record; the next append removes that tail before writing. Body
corruption is detected before serving a slice, and causes a refetch. No cache
gap is filled by downloading extra bytes. Quota eviction can remove a complete
pack, including the active session's own pack; other active entries are skipped.

The stress fixture saved 1,000 three-byte ranges in one 123,000-byte data file
(3,000 body bytes plus 120-byte integrity headers), rather than 1,000 separate
part files. It reopened with one probe and 1,000 disk hits. This is logical data
file size; filesystem allocation and metadata overhead vary.

## Desktop acceptance

Computer Use operated a renamed standalone executable to avoid selecting the
installed 7-Zip application by mistake. Disposable loopback fixtures and a
separate cache/output directory were used; cache/mode preferences are restored
when the fixture server exits. Existing ETag/reuse security choices were kept.

Observed successful flows:

- Open URL, browse ZIP, selection preview and selected extraction. The extracted
  text file matched its source byte-for-byte.
- Refresh in a ZIP subfolder; renew its URL while preserving the folder and
  selected filename.
- Scan a deliberately slow TAR: discovered names/counts and network counters
  updated. Cancel returned to the previous ZIP view.
- Open a header-encrypted 7z with the fixture password.
- Configure a separate cache path; open ISO with exact reads and confirm
  `parts.bin` was created. Browse nested Joliet Arabic names and Refresh there.
- About: upstream version, extension version and OpenAI Codex credit fit clearly.
- Final packaged build: a slow scan updated discovered files without a false
  0% in the title; a rejected HTTP 403 link explained the failure and retained
  the preceding archive view and path.

The UI test found and fixed path restoration: `BindToFolder` accepts a single
name, so a complete path/trailing slash failed. `BindRemoteArchivePath` now walks
components and the Agent suite tests trailing separators, missing paths and
root binding. The UI also revealed a residual 0% during header scans; opening
now uses the item count, with no percentage while its total is unknown.

Strict verified disk reopens, consent/cancellation/rollback, corrupted records,
quota and lock behavior are additionally covered by automated suites. Desktop
cache testing retained the user's existing ETag-bypass preference, so automatic
verified reuse was not claimed from that particular desktop session.

## Automated validation

Both HTTP-enabled and local-only Windows File Manager builds succeeded. The
final validation set passed **374 scenarios**, plus two resource-layout checks:

| Suite | Passing scenarios |
|---|---:|
| CLI network/archive behavior | 99 |
| File Manager Agent | 62 |
| Exact reads and recovery | 35 |
| Packed cache and legacy compatibility | 10 |
| Exact-mode cache Agent | 14 |
| Default-mode cache Agent | 14 |
| Exact-mode URL renewal | 15 |
| Low-level disk cache | 62 |
| Opening file progress and folder restoration | 63 |

The reports are retained under `.build/native/runs/`, `.build/msvc/runs/` and
`.build/remote/runs/`. Timing samples are separate from these scenario counts.

## Packaging

`package_release.py` creates a portable x64 binary ZIP, a matching source ZIP,
licenses, release notes, per-file hashes and ZIP SHA-256 sidecars. The binary
manifest records the source commit and whether local changes were present.
`7zRemoteFM.exe` is distinct from the installed 7-Zip name. No publishing,
installation or shell registration is performed.
