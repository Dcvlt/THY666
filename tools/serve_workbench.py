"""Serve only the browser workbench on localhost; no device or file-write API."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit
from local_env import env_int

ROOT = Path(__file__).resolve().parents[1]


class WorkbenchHandler(SimpleHTTPRequestHandler):
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm",
                      ".js": "text/javascript", ".json": "application/json"}

    def public_path(self, request_path):
        """Restrict static content to visible files within the configured web root."""
        segments = unquote(urlsplit(request_path).path).replace("\\", "/").split("/")
        if any(segment.startswith(".") for segment in segments if segment):
            return None
        try:
            target = Path(self.translate_path(request_path)).resolve()
            return target if target.is_relative_to(Path(self.directory).resolve()) else None
        except (OSError, RuntimeError, ValueError):
            return None

    def send_head(self):
        if self.public_path(self.path) is None:
            self.send_error(404)
            return None
        return super().send_head()

    def list_directory(self, path):
        self.send_error(404)
        return None

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        super().end_headers()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=None)
    args = parser.parse_args()
    port = args.port if args.port is not None else env_int("THY666_WORKBENCH_PORT", 3330, 1, 65535)
    if not 1 <= port <= 65535:
        parser.error("--port must be in 1..65535")
    for name in ("core.wasm", "chords.runtime.json"):
        if not (ROOT / "web/generated" / name).exists():
            raise SystemExit("First run python tools/build_core.py --test (and import the chord archive if missing)")
    handler = partial(WorkbenchHandler, directory=str(ROOT / "web"))
    server = ThreadingHTTPServer(("127.0.0.1", port), handler)
    print(f"THY666 workbench: http://127.0.0.1:{server.server_address[1]}/", flush=True)
    print("Local C/WebAssembly preview; Ctrl+C stops the server.", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
