# Optional reads without HTTP block rounding

Open URL now offers **Download only requested data (no read-ahead)**, unchecked
by default. Its preference is saved on OK. The active session retains its mode
on Refresh and URL renewal, including when reopening from a nested archive.

In this mode, a cache miss starts at the requested stream position and fetches
at most the requested length. 256 KiB remains an upper limit per read, not a
minimum. Reads may return short at that limit or an existing cached interval.
Memory entries are nonoverlapping, reused for interior reads, and retained in
the existing 32-entry LRU (at most 8 MiB). Download previews subtract the union
of memory and disk coverage without double counting or rounding ranges.

Optional persistent disk caching supports variable parts at arbitrary offsets.
Each file contains only the fetched body plus its integrity header. Reads
validate the whole stored part before returning a slice; requests stop before
the next cached part, downloading only gaps. Failed or busy disk I/O falls back
to memory/network as before. Atomic writes, quotas, active locks, safe Clear,
source identity and generation-bound SHA-256 checks remain in effect.

Exact entries use manifest magic `7ZHC02I` and fixed-block entries keep
`7ZHC01I`, within the existing owned cache root. Entries are reused only by the
matching mode; old fixed-block data is preserved. Catalog/Clear/quota operations
recognize both. Older builds cannot reuse the new exact entries.

A first exact open does not fetch fixed-size edge fingerprints. A strict reopen
uses the fresh strong-ETag probe and can then read entirely from disk. Unverified
reuse still requires explicit consent and compares up to three stored parts
against fresh network reads; metadata/sample checks do not prove whole-file
equality. Changed size, strong ETag or sampled content isolates a new generation.

This option removes read-ahead in the HTTP stream. Archive handlers can still
request buffers larger than the compressed member, read archive metadata, or
decode solid predecessors. It cannot promise downloading only the compressed
bytes belonging to a selected file. Evicted memory entries are read from disk
when available; missing or evicted disk parts can be downloaded again.

## Validation

HTTP build with disk parts: `.build/msvc/exact-disk-cache/`.

- 31 exact-read scenarios: server-observed byte ranges, three-byte reads,
  interior/overlapping cache reads, boundary crossing, large/EOF/zero reads,
  merged preview ranges, ZIP/7z/encrypted/solid extraction,
  ETag policies, rejected responses and URL renewal.
- 14 disk-cache Agent scenarios and 15 renewal scenarios passed in exact mode.
- 62 existing Agent scenarios, 14 default-mode disk-cache Agent scenarios and
  62 low-level disk-cache scenarios passed on the new build/tool.
- Compiled Open URL/cache resource controls fit without overlaps. Interactive
  desktop operation has not been verified for this change.

The extended exact-read suite additionally verifies variable on-disk payload
sizes, a probe-only reopen, slicing parts using different read boundaries,
repairing a corrupt three-byte part, and quota eviction. The disk-Agent and
renewal suites can be run with `--exact-http-reads` to exercise the same cache
policies and reopen/rollback operations in the new mode.

Same-fixture body-byte measurements for opening, selected extraction and testing
in one Agent session (all extracted content compared against the source):

| Archive | Default blocks | Exact reads |
|---|---:|---:|
| ZIP with long archive comment | 591,488 | 132,097 |
| Non-solid 7z | 262,690 | 1,272 |
| Encrypted non-solid 7z | 262,834 | 1,394 |
| Solid 7z | 4,195,159 | 4,195,159 |

These measurements were recorded for the initial memory-only mode; repeated
fixture generation can vary 7z metadata by a few bytes. The disk-enabled tests
confirmed the same initial download and only a one-byte probe on strict reopen
for all four formats. These are not general savings guarantees. The solid
case made more requests (21 versus 18) without reducing bytes.

```powershell
python Tests/RemoteArchive/build_msvc.py --http --rebuild --agent-check --bin-dir .build/msvc/exact-disk-cache
python Tests/RemoteArchive/test_exact_reads.py --exe .build/msvc/exact-disk-cache/agentcheck.exe
python Tests/RemoteArchive/test_agent.py --exe .build/msvc/exact-disk-cache/agentcheck.exe
python Tests/RemoteArchive/test_renewal.py --exe .build/msvc/exact-disk-cache/agentcheck.exe --exact-http-reads
python Tests/RemoteArchive/test_disk_cache_agent.py --exe .build/msvc/exact-disk-cache/agentcheck.exe --exact-http-reads
python Tests/RemoteArchive/test_cache_layout.py --exe .build/msvc/exact-disk-cache/7zFM.exe
```
