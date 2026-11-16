#!/usr/bin/env python3
"""Minimal MCP client for Affinity's local MCP server (SSE transport, http://localhost:6767/sse).

Affinity (3.2+) serves MCP itself once Settings > Model Context Protocol > Enable MCP server is on.
This client speaks the public MCP JSON-RPC protocol only; nothing in Affinity's bundle is read.

    from affinity_mcp import Affinity
    with Affinity() as a:
        print(a.call("list_sdk_documentation"))
        print(a.script("return 1+1"))
"""

import json
import queue
import re
import threading
import urllib.request

SSE_URL = "http://localhost:6767/sse"


class AffinityUnavailable(RuntimeError):
    pass


class Affinity:
    def __init__(self, url=SSE_URL, timeout=120):
        self.url, self.timeout = url, timeout
        self.base = url.rsplit("/", 1)[0]
        self._q, self._id, self._post = {}, 0, None
        self._lock = threading.Lock()
        self._ready = threading.Event()

    # -- transport --------------------------------------------------------
    def __enter__(self):
        try:
            self._resp = urllib.request.urlopen(self.url, timeout=10)
        except Exception as e:  # noqa: BLE001
            raise AffinityUnavailable(f"Affinity MCP not reachable at {self.url}: {e}. "
                                      "Open Affinity and turn on Settings > Model Context Protocol.") from e
        self._resp.fp.raw._sock.settimeout(None)
        threading.Thread(target=self._reader, daemon=True).start()
        if not self._ready.wait(10):
            raise AffinityUnavailable("no endpoint event from Affinity")
        self._rpc("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                                 "clientInfo": {"name": "re-survey", "version": "1"}})
        self._notify("notifications/initialized")
        # Affinity refuses every other doc topic until the preamble was read in this session.
        try:
            self.preamble = self.call("read_sdk_documentation_topic", {"filename": "preamble"})
        except RuntimeError:  # newer Affinity builds name it differently; find it in the listing
            topics = str(self.call("list_sdk_documentation", {}))
            name = next((t for t in re.findall(r"[\w./-]+", topics) if "preamble" in t.lower()), None)
            self.preamble = self.call("read_sdk_documentation_topic", {"filename": name}) if name else ""
        return self

    def __exit__(self, *a):
        try:
            self._resp.close()
        except Exception:  # noqa: BLE001
            pass

    def _reader(self):
        try:
            self._read_loop()
        except Exception:  # noqa: BLE001  (stream closed on exit)
            pass

    def _read_loop(self):
        event, data = None, []
        for raw in self._resp:
            line = raw.decode("utf-8", "replace").rstrip("\r\n")
            if line.startswith("event:"):
                event = line[6:].strip()
            elif line.startswith("data:"):
                data.append(line[5:].lstrip())
            elif line == "":
                payload = "\n".join(data)
                if event == "endpoint":
                    self._post = self.base + payload if payload.startswith("/") else payload
                    self._ready.set()
                elif payload:
                    try:
                        msg = json.loads(payload)
                    except ValueError:
                        msg = None
                    if isinstance(msg, dict) and "id" in msg and msg["id"] in self._q:
                        self._q[msg["id"]].put(msg)
                event, data = None, []

    def _send(self, obj):
        req = urllib.request.Request(self._post, data=json.dumps(obj).encode(),
                                     headers={"Content-Type": "application/json"}, method="POST")
        urllib.request.urlopen(req, timeout=30).read()

    def _notify(self, method, params=None):
        self._send({"jsonrpc": "2.0", "method": method, **({"params": params} if params else {})})

    def _rpc(self, method, params=None, timeout=None):
        with self._lock:
            self._id += 1
            rid = self._id
        q = self._q[rid] = queue.Queue()
        self._send({"jsonrpc": "2.0", "id": rid, "method": method, "params": params or {}})
        try:
            msg = q.get(timeout=timeout or self.timeout)
        except queue.Empty:
            raise TimeoutError(f"{method} timed out") from None
        finally:
            self._q.pop(rid, None)
        if "error" in msg:
            raise RuntimeError(f"{method}: {msg['error']}")
        return msg["result"]

    # -- API --------------------------------------------------------------
    def tools(self):
        return [t["name"] for t in self._rpc("tools/list")["tools"]]

    def tool_schemas(self):
        return self._rpc("tools/list")["tools"]

    def call_raw(self, name, args=None, timeout=None):
        return self._rpc("tools/call", {"name": name, "arguments": args or {}}, timeout)

    def call(self, name, args=None, timeout=None):
        """Text content joined; images returned as ('image', mime, b64) tuples in a list if present."""
        res = self.call_raw(name, args, timeout)
        texts, images = [], []
        for c in res.get("content", []):
            if c.get("type") == "text":
                texts.append(c["text"])
            elif c.get("type") == "image":
                images.append((c.get("mimeType"), c.get("data")))
        if res.get("isError"):
            raise RuntimeError(f"{name}: {' '.join(texts)[:2000]}")
        return ("\n".join(texts), images) if images else "\n".join(texts)
