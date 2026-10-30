"""Exercise the real 26.05 console with local and controlled HTTP inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import socket
import subprocess
import tempfile
import threading
import zipfile

from test_remote import FixtureServer, ROOT


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, default=ROOT / ".build/native/7z.exe")
    args = parser.parse_args()
    exe = args.exe.resolve()
    runs = ROOT / ".build/native/runs"
    runs.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="cli-", dir=runs))
    rng = random.Random(15)
    contents = {
        "000-first.bin": rng.randbytes(2 << 20),
        "docs/target.txt": b"selected remote member\n" * 100,
        "unicode-\u0645\u0644\u0641.txt": b"unicode member",
        "999-last.bin": rng.randbytes(2 << 20),
    }
    inputs = work / "input"
    inputs.mkdir()
    for name, data in contents.items():
        path = inputs / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    archive = work / "sample.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        z.comment = b"x" * 65535
        for name, data in contents.items():
            z.writestr(name, data)
    cases = []

    def run(label, arguments, success=True):
        result = subprocess.run([str(exe), *map(str, arguments), "-sccUTF-8", "-bd"],
                                cwd=inputs, capture_output=True, timeout=60)
        output = (result.stdout + result.stderr).decode("utf-8", errors="replace").replace("\r\n", "\n")
        (work / (label + ".txt")).write_text(output, encoding="utf-8")
        assert (result.returncode == 0) == success, (label, result.returncode, output)
        assert "private-token" not in output, (label, "URL secret leaked", output)
        cases.append(dict(label=label, exit_code=result.returncode))
        print("PASS " + label, flush=True)
        return output

    assert "26.05" in run("engine-version", ["i"])
    for name, flags in [("nonsolid.7z", ["-ms=off"]),
                        ("solid.7z", ["-ms=on"]),
                        ("secret.7z", ["-ms=off", "-mhe=on", "-pfixture-password"])]:
        run("create-" + name, ["a", work / name, "-t7z", *flags, "."])
    files = {p.name: p.read_bytes() for p in work.iterdir() if p.suffix in (".zip", ".7z")}
    server = FixtureServer(files)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base = f"http://127.0.0.1:{server.server_port}"
    try:
        for name in files:
            password = ["-pfixture-password"] if name.startswith("secret") else []
            url = f"{base}/ok/{name}?sig=private-token%2Ftest&x=1&x=2"
            before = len(server.records)
            local = run(name + "-local-list", ["l", work / name, "-slt", *password])
            remote = run(name + "-remote-list", ["l", url, "-slt", *password])
            # Archive metadata differs by source. Item records must agree.
            def items(text):
                return text.split("----------\n", 1)[1].strip()
            assert items(local) == items(remote), name
            assert len(server.records) > before
            for command in ("x", "e"):
                output_dir = work / (name + "-" + command)
                first_request = len(server.records)
                run(name + "-" + command,
                    [command, url, "docs/target.txt", "-o" + str(output_dir), "-y", "-snz1", *password])
                target = output_dir / ("docs/target.txt" if command == "x" else "target.txt")
                assert target.read_bytes() == contents["docs/target.txt"]
                assert len(list(output_dir.rglob("*.txt"))) == 1
                zone = Path(str(target) + ":Zone.Identifier").read_bytes()
                assert b"ZoneId=3" in zone
                ranges = server.records[first_request:]
                requested_bytes = sum(r["end"] - r["start"] + 1 for r in ranges)
                cases[-1].update(requests=len(ranges), requested_bytes=requested_bytes,
                                 archive_bytes=len(files[name]))
                if name in ("sample.zip", "nonsolid.7z"):
                    assert requested_bytes < len(files[name]) // 2, (name, requested_bytes)
            run(name + "-test", ["t", url, "docs/target.txt", *password])
            run(name + "-local-test", ["t", work / name, *password])

        run("unicode-select", ["x", f"{base}/ok/sample.zip", "unicode-\u0645\u0644\u0641.txt",
                               "-o" + str(work / "unicode"), "-y"])
        assert (work / "unicode" / "unicode-\u0645\u0644\u0641.txt").read_bytes() == b"unicode member"
        run("wildcard-select", ["x", f"{base}/ok/sample.zip", "docs/*", "-o" + str(work / "wildcard"), "-y"])
        assert (work / "wildcard/docs/target.txt").read_bytes() == contents["docs/target.txt"]
        run("local-extract", ["x", archive, "docs/target.txt", "-o" + str(work / "local"), "-y"])
        assert (work / "local/docs/target.txt").read_bytes() == contents["docs/target.txt"]
        # Archive mutation must be rejected before any HTTP request.
        for command in ("a", "u", "d", "rn"):
            before = len(server.records)
            run("reject-" + command, [command, f"{base}/ok/sample.zip?sig=private-token", "docs/target.txt"], False)
            assert len(server.records) == before
        error_hints = {
            "ignore": "Download the archive locally",
            "weak": "skip ETag verification only if",
            "missing-etag": "-shttp-no-etag",
            "changed": "Reopen the archive",
            "changed-size": "Reopen the archive",
            "precondition": "Reopen the archive",
            "truncated": "reopen the archive",
            "redirect": "final direct URL",
            "unauthorized": "Browser login sessions are not supported",
            "forbidden": "obtain a fresh direct download URL",
            "missing": "Check the URL",
            "proxy-auth": "proxy authentication is not supported",
            "gone": "Check the URL",
            "range416": "Reopen the archive",
            "rate-limit": "Wait before reopening",
            "server-error": "Try reopening the archive later",
            "unexpected-status": "Unexpected HTTP status 418",
            "wrong-range": "Content-Range",
        }
        status_codes = {"unauthorized": 401, "forbidden": 403, "missing": 404,
                        "proxy-auth": 407, "gone": 410, "range416": 416,
                        "rate-limit": 429, "server-error": 503, "unexpected-status": 418}
        for mode, hint in error_hints.items():
            for command in ("l", "t"):
                output = run(mode + "-" + command,
                             [command, f"{base}/{mode}/sample.zip?sig=private-token"], False)
                assert hint in output, (mode, command, output)
                if mode in status_codes:
                    assert str(status_codes[mode]) in output
        # Reserve an unused loopback port without listening: connection refusal
        # must provide a useful message without leaking the signed URL.
        with socket.socket() as unavailable:
            unavailable.bind(("127.0.0.1", 0))
            port = unavailable.getsockname()[1]
            for command in ("l", "t"):
                output = run("connection-refused-" + command,
                             [command, f"http://127.0.0.1:{port}/sample.zip?sig=private-token"], False)
                assert "Cannot connect to the server" in output
        run("wrong-password", ["t", f"{base}/ok/secret.7z", "-pincorrect"], False)
        run("chunked", ["t", f"{base}/chunked/sample.zip"])
        assert any(r["path"].endswith("?sig=private-token%2Ftest&x=1&x=2") for r in server.records)
        assert all(r["if_match"] == '"fixture-v1"' for r in server.records
                   if (r["start"], r["end"]) != (0, 0))

        # Explicitly disabling ETag must omit If-Match without weakening range,
        # length, representation, status or source-size validation.
        unchecked_start = len(server.records)
        opt = "-shttp-no-etag"
        for mode in ("missing-etag", "weak", "duplicate-etag", "changed"):
            for command in ("l", "t"):
                run("no-etag-" + mode + "-" + command,
                    [command, f"{base}/{mode}/sample.zip?sig=private-token", opt])
        for command in ("x", "e"):
            output_dir = work / ("no-etag-" + command)
            run("no-etag-" + command,
                [command, f"{base}/missing-etag/sample.zip", "docs/target.txt", opt,
                 "-o" + str(output_dir), "-y"])
            target = output_dir / ("docs/target.txt" if command == "x" else "target.txt")
            assert target.read_bytes() == contents["docs/target.txt"]
        for mode in ("changed-size", "wrong-range", "missing-range", "duplicate-range", "truncated",
                     "extra-body", "encoded", "multipart", "wrong-length", "ignore", "precondition", "redirect"):
            run("no-etag-reject-" + mode, ["t", f"{base}/{mode}/sample.zip", opt], False)
        assert all(r["if_match"] is None for r in server.records[unchecked_start:])
        run("no-etag-local-rejected", ["l", archive, opt], False)
        run("etag-default-still-strict", ["l", f"{base}/missing-etag/sample.zip"], False)
    finally:
        server.shutdown()
        server.server_close()
        report = dict(executable=str(exe), executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                      engine_sha256=hashlib.sha256(exe.with_name("7z.dll").read_bytes()).hexdigest(),
                      cases=cases, requests=server.records)
        (work / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        print("Report: " + str(work / "report.json"), flush=True)
    print(f"All {len(cases)} CLI scenarios passed.")


if __name__ == "__main__":
    main()
