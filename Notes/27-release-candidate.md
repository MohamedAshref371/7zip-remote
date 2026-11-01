# v1.0.0-rc1 — Windows x64 Release Candidate

This candidate is based on upstream 7-Zip 26.05. It is prepared for review and
acceptance, not yet a stable v1.0.0 publication.

## Changes since 0.6.0-dev

- Help → About now puts `7-Zip Remote 1.0.0-rc1` first and labels `7-Zip 26.05`
  separately as the upstream engine version.
- HTTP-enabled File Manager and console preferences use
  `HKEY_CURRENT_USER\Software\7-Zip Remote` instead of the installed 7-Zip's
  registry tree. The default disk cache moved to
  `%LOCALAPPDATA%\7-Zip Remote\RemoteCache`. Existing v0.5.0 settings are not
  imported; set cache and ETag choices again. Local-only builds keep upstream
  registry behavior.
- File association commands use the running File Manager's actual executable
  path, including the portable `7zRemoteFM.exe` name.
- Open URL now warns that exact reads can make large RAR/TAR header scans much
  slower. The 256 KiB block mode remains the default. In a controlled benchmark
  with 1,024 data files and 20 ms per request, RAR5 took 47.165 s with exact
  reads versus 0.689 s in block mode. These are workload samples, not guarantees.
- The local release gate rebuilds and tests x64 binaries, checks compiled dialog
  layout, and can create and verify portable binary/source ZIPs from a clean commit.

## Supported scope

Direct HTTP(S) links to single-volume archives on range-capable servers; Windows
x64; browsing and selected extraction/testing with read-only remote sessions.
ZIP, 7z, RAR/RAR5, TAR and ISO have controlled integration coverage. Strong ETag
verification is the default; bypass requires a deliberate choice. Links requiring
browser cookies/login, sharing pages, remote writes, and multi-volume archives
are outside this release's scope. Solid archives can require most of their data.

## Acceptance before stable v1.0.0

1. Run `python Tests/RemoteArchive/release_gate.py --build` on Windows with MSVC,
   Python 3 and Zig 0.13.0. It must finish with `Release candidate gate passed`.
2. On a clean Windows x64 account without a build toolchain, unpack the produced
   ZIP and open/extract/test ZIP and 7z from direct URLs. Verify extracted hashes,
   Refresh, cancellation, expired-link renewal and cache settings. Confirm the
   installed 7-Zip's preferences are unchanged.
3. Check several real servers used by the intended audience, including direct
   HTTPS, signed URLs, redirects, strong/missing ETags and a proxy if supported by
   that audience. Record server type, outcome and byte counts without URL secrets.
4. Review the packaged README, licenses, SHA-256 sidecars, source ZIP and release
   notes. Publish an `rc1` release only after these checks. Tag stable `v1.0.0`
   from the exact source used for the final validated package.

Earlier controlled validation and benchmark details are in
[26-development-validation.md](26-development-validation.md).

## Current candidate evidence

The local release gate passed on Windows x64 after a bounded retry was added
for transient sharing violations while atomically replacing cache metadata.
The legacy exact-cache migration check also passed using a previous Agent build.

One public real-server check used the official
[7-Zip 26.05 extra archive](https://github.com/ip7z/7zip/releases/download/26.05/7z2605-extra.7z)
on GitHub Releases. Its complete download matched the upstream SHA-256
`f27a8f103c84785026edcba1f67faf1357225df9d19ddb0b7760d55947141bc7`.
Remote selected extraction of `x64/7za.exe` matched extraction from the local
archive (SHA-256 `7ddf0a3bc451e166679296be630d6f0573eb386159c04184761c43020c484291`).
No extracted executable was launched. Additional target-server and clean-machine
acceptance are still pending.

The six MediaFire sample pages supplied for acceptance yielded direct HTTPS
download links. In the CLI, all six listed successfully with explicit ETag
bypass; strict mode rejected the first sample because the server lacked a strong
ETag, as intended. A selected `ma3any.db` (Arabic name in the plain 7z) extracted
from each remote archive with identical SHA-256:
`67b9c37c5415a5ff8ec08fbc953c79ce0d55966d0ab0dfda0d3ad41dd1ee9c7b`.
This covered plain ZIP and 7z, password ZIP and RAR5, and RAR5/7z with encrypted
filenames. The selected file was 589,824 bytes in each archive. Direct download
links and their temporary tokens were kept only under ignored `.build/acceptance`
and are not included in the source or release notes.

The candidate File Manager was also opened as `7zRemoteFM.exe`. Its Open URL
dialog showed the exact-read performance warning and its default ETag bypass
checkbox was unchecked. The official GitHub 7z archive opened and displayed its
contents in the desktop UI. The MediaFire samples were tested through the CLI;
desktop extraction from those samples and a clean Windows account remain to be
checked before the stable release.
