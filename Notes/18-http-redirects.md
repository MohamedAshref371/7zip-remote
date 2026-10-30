# HTTP redirects and stability checks

The shared HTTP range transport now follows **301, 302, 303, 307 and 308** for
the console, File Manager and standalone prototype. It follows at most five
redirects per logical range request. Every hop counts as an HTTP request; the
original deadline covers the entire chain. The operation thread resolves each
Location and performs cancellation checks. The previous request is closed and
its final asynchronous callback drained before replacing the connection.

Location supports absolute HTTP(S), scheme-relative, root-relative, path-relative
and query-only references. Relative path resolution removes literal dot segments;
percent escapes, query ordering, duplicate query parameters and signed tokens are
not decoded or re-encoded. A new path replaces the old query; query-only and
fragment-only references retain the appropriate base components. Fragments are
removed before sending. Missing, empty or duplicate Location headers fail.

Every destination is validated as an ASCII encoded HTTP(S) URL. Embedded userinfo,
including an empty userinfo delimiter, is rejected. A redirect from HTTPS to HTTP
is rejected before making its destination connection. Cookies, automatic server
and proxy authentication and Referer remain disabled. Range and identity encoding
are sent on every hop; subsequent ranges retain If-Match in strict mode. HTTP 206,
Content-Range, body length, total size and ETag validation remain mandatory under
the existing ETag policy. Error messages exclude both original and destination
URLs. No browser login, HTML sharing-page resolution, retry or fallback is added.

The resolved URL becomes the source's next request destination, avoiding a repeat
of the initial chain for every cache miss. Later data-range redirects are also
supported and must pass the established source identity/size checks. File Manager
retains the original user URL for explicit Refresh, so refreshed sessions resolve
the link again. Redirect response bodies are not read or published as archive
data; downloaded-byte statistics continue to count only body bytes delivered to
the transport's reads. Network request counts include redirect hops.

References: [RFC 3986 relative resolution](https://www.rfc-editor.org/rfc/rfc3986.html#section-5.2),
[WinHTTP security considerations](https://learn.microsoft.com/en-us/windows/win32/winhttp/winhttp-security-considerations).

## Validation

```powershell
python Tests/RemoteArchive/build_msvc.py --http --rebuild --agent-check --bin-dir .build/msvc/redirects
python Tests/RemoteArchive/test_redirect.py --exe .build/msvc/redirects/7z.exe --agent .build/msvc/redirects/agentcheck.exe
python Tests/RemoteArchive/test_cancel.py --exe .build/msvc/redirects/agentcheck.exe
python Tests/RemoteArchive/test_cli.py --exe .build/msvc/redirects/7z.exe
python Tests/RemoteArchive/test_agent.py --exe .build/msvc/redirects/agentcheck.exe
python Tests/RemoteArchive/build.py
python Tests/RemoteArchive/test_remote.py --dll "$PWD/.build/msvc/redirects/7z.dll" --sevenzip "$PWD/.build/msvc/redirects/7z.exe"
python Tests/RemoteArchive/build_msvc.py --target filemanager --bin-dir .build/msvc/redirects-local
```

MSVC HTTP and local-only File Manager builds and the portable Zig build succeed.
The redirect suite passes 35 scenarios, checking selected extraction hashes,
five redirect status codes, signed queries, relative references, cross-origin
ports, five/six-hop boundaries, malformed destinations, absent cookies/auth/Referer,
late data-range redirects, retained ETag/size checks, bypass behavior, request and
byte accounting, cancellation at a stalled destination, and a shared 250 ms
deadline across delayed hops. Cross-origin fixtures use two loopback ports.

The cancellation suite passes all eight cases, including 68 repeated aborts,
and every stalled fixture socket closes. The existing 99 CLI, 58 Agent and
72 stream/prototype regression scenarios pass. Before the redirect change, the
eight cancellation cases were also rerun successfully on the prior binaries.

Remaining acceptance work:

- Desktop Computer Use initialization succeeded, but listing applications failed
  with `Computer Use native pipe is unavailable` / `os error 2`. No actual button
  interaction is claimed. Open URL, browse, selected extract, Refresh, Cancel and
  return to local browsing still need interactive verification. The fixture server
  now prints `redirect_url` for this flow.
- The new suite uses HTTP loopback fixtures. Real HTTPS redirect chains, HTTPS
  downgrade rejection and proxy interoperability still need integration checks;
  no certificate validation bypass or Windows trust-store changes were made.
- The previously observed WinHTTP internal event retention after repeated aborts
  remains unresolved; see [the cancellation diagnostic](17-responsive-cancellation.md).

These results support a development pre-release, with the desktop and server
acceptance limitations stated explicitly.
