"""Integration tests: actual 7z DLL + controlled loopback HTTP range server.

Artifacts, traces and report.json are retained in .build/remote/runs/.
No public archive servers, certificate bypasses, or browser sessions are used.
"""
import argparse
import hashlib
import http.server
import json
import os
from pathlib import Path
import random
import re
import socket
import subprocess
import tempfile
import threading
import time
import urllib.parse
import zipfile

ROOT = Path(__file__).resolve().parents[2]


class FixtureServer(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, files):
        super().__init__(("127.0.0.1", 0), Handler)
        self.files = files
        self.records = []
        self.lock = threading.Lock()

    def handle_error(self, request, client_address):
        # Clients intentionally abort invalid/oversized responses.
        pass


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def do_HEAD(self):
        self.send_error(405)

    def do_GET(self):
        route = urllib.parse.urlsplit(self.path)
        parts = route.path.strip("/").split("/")
        if len(parts) != 2:
            self.send_error(404)
            return
        mode, name = parts
        virtual = name == "pattern.bin"
        blob = self.server.files.get(name)
        if not virtual and blob is None:
            self.send_error(404)
            return
        total = (1 << 32) + 1048576 if virtual else len(blob)
        requested = self.headers.get("Range", "")
        match = re.fullmatch(r"bytes=(\d+)-(\d+)", requested)
        if not match:
            self.send_error(400)
            return
        start, end = map(int, match.groups())
        probe = requested == "bytes=0-0"
        with self.server.lock:
            self.server.records.append(dict(mode=mode, name=name, start=start, end=end,
                                            if_match=self.headers.get("If-Match"),
                                            encoding=self.headers.get("Accept-Encoding"),
                                            path=self.path))
        if start >= total or end >= total or end < start:
            self.send_error(416)
            return
        if mode == "slow":
            time.sleep(0.8)
        if mode == "stalled":
            time.sleep(3)
        if mode == "redirect":
            self.send_response(302)
            self.send_header("Location", f"/ok/{name}")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if mode == "ignore":
            self.send_response(200)
            self.send_header("Content-Length", str(total))
            self.end_headers()
            self.close_connection = True
            return
        if mode == "precondition" and not probe:
            self.send_response(412)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if mode in ("forbidden", "missing", "range416"):
            self.send_error({"forbidden": 403, "missing": 404, "range416": 416}[mode])
            return
        tag = '"fixture-v1"'
        if mode == "changed" and not probe:
            tag = '"fixture-v2"'
        if mode == "weak":
            tag = 'W/"fixture-v1"'
        if mode == "changed-size" and not probe:
            total += 1
        self.send_response(206)
        if mode != "missing-range":
            response_start = start + 1 if mode == "wrong-range" and not probe else start
            self.send_header("Content-Range", f"bytes {response_start}-{end}/{total}")
            if mode == "duplicate-range":
                self.send_header("Content-Range", f"bytes {start}-{end}/{total}")
        if mode != "missing-etag":
            self.send_header("ETag", tag)
            if mode == "duplicate-etag":
                self.send_header("ETag", tag)
        if mode == "encoded":
            self.send_header("Content-Encoding", "gzip")
        self.send_header("Content-Type", "multipart/byteranges; boundary=x" if mode == "multipart"
                         else "application/octet-stream")
        # Deliberately no Accept-Ranges header: successful GET is authoritative.
        chunked = mode in ("chunked", "extra-body")
        length = end - start + 1
        if chunked:
            self.send_header("Transfer-Encoding", "chunked")
        else:
            self.send_header("Content-Length", str(length + 1 if mode == "wrong-length" else length))
        self.end_headers()
        body = bytes(i % 251 for i in range(start, end + 1)) if virtual else blob[start:end + 1]
        if mode == "truncated" and not probe:
            self.wfile.write(body[:len(body) // 2])
            self.wfile.flush()
            self.close_connection = True
            self.connection.shutdown(socket.SHUT_WR)
            return
        if mode == "extra-body":
            body += b"X"
        if chunked:
            for offset in range(0, len(body), 8192):
                block = body[offset:offset + 8192]
                self.wfile.write(f"{len(block):x}\r\n".encode() + block + b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
        else:
            self.wfile.write(body)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, default=ROOT / ".build/remote/7zremote.exe")
    parser.add_argument("--dll", type=Path, default=Path("C:/Program Files/7-Zip/7z.dll"))
    parser.add_argument("--sevenzip", type=Path, default=Path("C:/Program Files/7-Zip/7z.exe"))
    args = parser.parse_args()
    runs = ROOT / ".build/remote/runs"
    runs.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="run-", dir=runs))
    inputs = work / "input"
    inputs.mkdir()
    rng = random.Random(7)
    contents = {
        "000-first.bin": rng.randbytes(2 << 20),
        "001-middle.bin": rng.randbytes(2 << 20),
        "002-target.txt": ("remote archive test — بيانات عربية\n" * 100).encode(),
        "003-last.bin": rng.randbytes(2 << 20),
    }
    for name, blob in contents.items():
        (inputs / name).write_bytes(blob)
    with zipfile.ZipFile(work / "sample.zip", "w", zipfile.ZIP_DEFLATED) as z:
        z.comment = b"c" * 65535
        for name, blob in contents.items():
            z.writestr(name, blob)
        z.writestr("unicode-ملف.txt", b"unicode member")
    for name, switches in (
        ("nonsolid.7z", ["-t7z", "-ms=off"]),
        ("solid.7z", ["-t7z", "-ms=on", "-mqs=off"]),
        ("secret.7z", ["-t7z", "-ms=off", "-mhe=on", "-pfixture-password"]),
        ("secret.zip", ["-tzip", "-mem=AES256", "-pfixture-password"]),
    ):
        subprocess.run([str(args.sevenzip), "a", str(work / name), *switches, *contents],
                       cwd=inputs, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    files = {p.name: p.read_bytes() for p in work.iterdir() if p.suffix in (".zip", ".7z")}
    server = FixtureServer(files)
    serving = threading.Thread(target=server.serve_forever, daemon=True)
    serving.start()
    base = f"http://127.0.0.1:{server.server_port}"
    results = []
    counter = 0
    env = dict(os.environ, REMOTE_TEST_PASSWORD="fixture-password", REMOTE_WRONG_PASSWORD="incorrect")

    def run(label, command, fmt, source, extra=(), expected=True):
        nonlocal counter
        counter += 1
        trace = work / f"{counter:02}-{label}.csv"
        start = time.monotonic()
        proc = subprocess.run([str(args.exe), command, fmt, str(source), "--dll", str(args.dll),
                               "--trace", str(trace), *extra], capture_output=True, timeout=40, env=env)
        elapsed = time.monotonic() - start
        stderr = proc.stderr.decode("utf-8", errors="replace")
        stdout = proc.stdout.decode("utf-8", errors="replace")
        assert (proc.returncode == 0) == expected, (label, proc.returncode, stdout, stderr)
        stats = json.loads(stderr.strip().splitlines()[-1])
        results.append(dict(label=label, success=proc.returncode == 0, seconds=round(elapsed, 3), **stats))
        print(f"PASS {label}: {stats['requests']} requests, {stats['downloaded']} bytes", flush=True)
        return stdout, stats

    try:
        for name in ("sample.zip", "nonsolid.7z", "solid.7z", "secret.7z", "secret.zip"):
            fmt = "zip" if name.endswith(".zip") else "7z"
            password = ["--password-env", "REMOTE_TEST_PASSWORD"] if name.startswith("secret") else []
            local, _ = run(f"{name}-local-list", "l", fmt, work / name, password)
            remote, _ = run(f"{name}-remote-list", "l", fmt, f"{base}/ok/{name}", password)
            assert json.loads("[" + ",".join(local.splitlines()) + "]") == json.loads(
                "[" + ",".join(remote.splitlines()) + "]")
            listing = [json.loads(line) for line in local.splitlines()]
            assert set(contents).issubset({item["path"] for item in listing})
            if name == "sample.zip":
                assert any(item["path"] == "unicode-ملف.txt" for item in listing)
            # Trace the first, middle and last data entries, including the small late target.
            selected = list(contents) if name in ("sample.zip", "solid.7z", "nonsolid.7z") else ["002-target.txt"]
            for member in selected:
                idx = next(item["index"] for item in listing if item["path"] == member)
                local_out = work / f"local-{name}-{idx}.out"
                remote_out = work / f"remote-{name}-{idx}.out"
                run(f"{name}-{idx}-local-x", "x", fmt, work / name,
                    ["--index", str(idx), "--out", str(local_out), *password])
                _, stats = run(f"{name}-{idx}-remote-x", "x", fmt, f"{base}/ok/{name}",
                               ["--index", str(idx), "--out", str(remote_out), *password])
                assert sha(local_out.read_bytes()) == sha(remote_out.read_bytes()) == sha(contents[member])
                if member == "002-target.txt" and name == "sample.zip":
                    assert stats["downloaded"] < len(files[name]) // 4, stats
            target = next(item["index"] for item in listing if item["path"] == "002-target.txt")
            run(f"{name}-remote-test", "t", fmt, f"{base}/ok/{name}",
                ["--index", str(target), *password])

        run("chunked", "l", "zip", f"{base}/chunked/sample.zip")
        run("signed-query", "l", "zip", f"{base}/ok/sample.zip?sig=a%2Fb%2Bc&x=1&x=2")
        assert any(r["path"].endswith("?sig=a%2Fb%2Bc&x=1&x=2") for r in server.records)
        for mode in ("ignore", "weak", "missing-etag", "missing-range", "duplicate-range",
                     "duplicate-etag", "wrong-range", "changed", "changed-size", "precondition",
                     "encoded", "multipart", "wrong-length", "truncated", "extra-body",
                     "redirect", "forbidden", "missing", "range416"):
            run(mode, "l", "zip", f"{base}/{mode}/sample.zip", expected=False)
        run("no-password", "l", "7z", f"{base}/ok/secret.7z", expected=False)
        run("wrong-header-password", "l", "7z", f"{base}/ok/secret.7z",
            ["--password-env", "REMOTE_WRONG_PASSWORD"], expected=False)
        failed_output = work / "wrong-password.out"
        run("wrong-data-password", "x", "zip", f"{base}/ok/secret.zip",
            ["--index", "0", "--out", str(failed_output), "--password-env", "REMOTE_WRONG_PASSWORD"],
            expected=False)
        assert not failed_output.exists(), "Failed extraction must remove its newly created output"
        existing_output = work / "existing.out"
        existing_output.write_bytes(b"user-owned-file")
        run("no-overwrite", "x", "zip", f"{base}/ok/sample.zip",
            ["--index", "0", "--out", str(existing_output)], expected=False)
        assert existing_output.read_bytes() == b"user-owned-file"
        run("timeout", "l", "zip", f"{base}/stalled/sample.zip", ["--timeout-ms", "200"], expected=False)
        _, stats = run("cancel-before", "l", "zip", f"{base}/ok/sample.zip",
                       ["--cancel-after-ms", "0"], expected=False)
        assert stats["requests"] == 0 and stats["result"] == "80004004"
        _, stats = run("cancel-during", "l", "zip", f"{base}/slow/sample.zip",
                       ["--cancel-after-ms", "100"], expected=False)
        assert stats["result"] == "80004004"
        _, stats = run("stream-contract-64bit", "streamcheck", "raw", f"{base}/ok/pattern.bin")
        assert stats["source_size"] > 1 << 32 and stats["downloaded"] < 2 << 20
        assert stats["cache_hits"] > 0
        # Every post-probe request must be pinned; no implicit credential/session dependence.
        assert all(r["if_match"] == '"fixture-v1"' for r in server.records if (r["start"], r["end"]) != (0, 0))
        assert all(r["encoding"] == "identity" for r in server.records)
    finally:
        server.shutdown()
        server.server_close()
        report = dict(engine_dll=str(args.dll), engine_sha256=sha(args.dll.read_bytes()),
                      fixture_hashes={name: sha(blob) for name, blob in files.items()},
                      cases=results, requests=server.records)
        (work / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"Report: {work / 'report.json'}", flush=True)
    print(f"All {len(results)} scenarios passed.")


if __name__ == "__main__":
    main()
