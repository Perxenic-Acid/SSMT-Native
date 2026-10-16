"""归档下载与缓存回归，仅使用本地 HTTP，不加载图形组件。"""

import hashlib
import io
from concurrent.futures import ThreadPoolExecutor
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import subprocess
import tempfile
import threading
import unittest
import zipfile


MODULE = Path(__file__).resolve().parents[1] / "AcquireArchive.cmake"


class ArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ssmt-bridge-download-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.archive = self.root / "cache with spaces" / "bridge.zip"
        fixture = io.BytesIO()
        with zipfile.ZipFile(fixture, "w") as zipped:
            zipped.writestr("source/README.txt", "固定源码夹具")
        self.payload = fixture.getvalue()
        self.expected_hash = hashlib.sha256(self.payload).hexdigest()
        self.responses = []
        self.requests = 0
        self.transfer_started = threading.Event()
        self.release_transfer = threading.Event()
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                owner.requests += 1
                response = owner.responses.pop(0) if owner.responses else "valid"
                if response == "error":
                    self.send_error(503)
                    return
                body = b"wrong content" if response == "wrong" else owner.payload
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                if response == "blocked":
                    self.wfile.write(body[:10])
                    self.wfile.flush()
                    owner.transfer_started.set()
                    owner.release_transfer.wait(timeout=10)
                    self.wfile.write(body[10:])
                    self.close_connection = True
                    return
                self.wfile.write(body[:10] if response == "partial" else body)
                self.wfile.flush()
                self.close_connection = True

            def log_message(self, *args):
                pass

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()

        def close_server():
            server.shutdown()
            server.server_close()
            worker.join(timeout=5)

        self.addCleanup(close_server)
        self.url = f"http://127.0.0.1:{server.server_port}/bridge.zip"

    def acquire(self, local=None, candidates=(), tag="", extract=True):
        script = self.root / f"acquire{tag}.cmake"
        extra = ""
        if local:
            extra += f' LOCAL_ARCHIVE "{local.as_posix()}"'
        if candidates:
            extra += " CANDIDATES " + " ".join(f'"{p.as_posix()}"' for p in candidates)
        extraction = (
            f'file(ARCHIVE_EXTRACT INPUT "${{result}}" DESTINATION "{self.root.as_posix()}/extracted{tag}")\n'
            if extract else ""
        )
        script.write_text(
            'cmake_minimum_required(VERSION 3.24)\n'
            f'include("{MODULE.as_posix()}")\n'
            f'ssmt_acquire_bridge_archive(result "{self.archive.as_posix()}" '
            f'"{self.url}" "{self.expected_hash}"{extra})\n'
            + extraction,
            encoding="utf-8",
        )
        result = subprocess.run(
            ["cmake", "-P", str(script)], capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=30,
        )
        self.assertFalse(list(self.root.rglob("*.part")), "不得留下失败的半包")
        return result

    def assert_complete(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / "extracted/source/README.txt").read_text(encoding="utf-8"), "固定源码夹具")

    def test_valid_shared_cache_never_downloads(self):
        self.archive.parent.mkdir()
        self.archive.write_bytes(self.payload)
        self.assert_complete(self.acquire())
        self.assertEqual(self.requests, 0)

    def test_valid_legacy_archive_is_imported_after_corrupt_candidates(self):
        corrupt, legacy = self.root / "partial.zip", self.root / "legacy.zip"
        corrupt.write_bytes(self.payload[:10])
        legacy.write_bytes(self.payload)
        self.assert_complete(self.acquire(candidates=[corrupt, legacy]))
        self.assertEqual(self.archive.read_bytes(), self.payload)
        self.assertEqual(legacy.read_bytes(), self.payload)
        self.assertEqual(corrupt.read_bytes(), self.payload[:10])
        self.assertEqual(self.requests, 0)

    def test_partial_and_hash_mismatch_downloads_retry_then_publish(self):
        self.responses = ["partial", "wrong", "valid"]
        self.assert_complete(self.acquire())
        self.assertEqual(self.requests, 3)
        self.assertEqual(self.archive.read_bytes(), self.payload)

    def test_parallel_configures_share_one_complete_archive(self):
        self.responses = ["blocked"]
        with ThreadPoolExecutor(max_workers=2) as workers:
            first = workers.submit(self.acquire, tag="_first", extract=False)
            try:
                self.assertTrue(self.transfer_started.wait(timeout=5))
                self.assertFalse(self.archive.exists(), "未完成的下载不能提前发布到共享缓存")
                second = workers.submit(self.acquire, tag="_second", extract=False)
            finally:
                self.release_transfer.set()
            for future in [first, second]:
                result = future.result(timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.requests, 1)
        self.assertEqual(self.archive.read_bytes(), self.payload)

    def test_failed_download_preserves_previous_cache_and_stops_before_extract(self):
        self.archive.parent.mkdir()
        self.archive.write_bytes(b"previous invalid cache")
        self.responses = ["error"] * 3
        result = self.acquire()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.requests, 3)
        self.assertEqual(self.archive.read_bytes(), b"previous invalid cache")
        self.assertFalse((self.root / "extracted").exists())
        self.assertIn("SSMT_GIMI_BRIDGE_ARCHIVE", result.stderr)
        self.assertNotIn("ZIP decompression", result.stderr)

    def test_hash_mismatch_is_never_cached_or_extracted(self):
        self.responses = ["wrong"] * 3
        result = self.acquire()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.requests, 3)
        self.assertFalse(self.archive.exists())
        self.assertFalse((self.root / "extracted").exists())
        self.assertIn("SHA-256", result.stderr)

    def test_offline_archive_is_verified_without_downloading(self):
        source = self.root / "offline archive.zip"
        source.write_bytes(self.payload)
        self.assert_complete(self.acquire(local=source))
        self.assertEqual(self.requests, 0)
        self.assertFalse(self.archive.exists())

    def test_invalid_explicit_archive_does_not_fall_back_to_cache_or_network(self):
        self.archive.parent.mkdir()
        self.archive.write_bytes(self.payload)
        source = self.root / "bad.zip"
        source.write_bytes(b"broken")
        result = self.acquire(local=source)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SHA-256", result.stderr)
        self.assertFalse((self.root / "extracted").exists())
        self.assertEqual(self.requests, 0)

    def test_missing_explicit_archive_stops_without_downloading(self):
        result = self.acquire(local=self.root / "missing.zip")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / "extracted").exists())
        self.assertEqual(self.requests, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
