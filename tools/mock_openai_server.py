#!/usr/bin/env python3
"""Local stand-in for an OpenAI-compatible server, for testing Infinite's built-in agent without a real key.
usage: mock_openai_server.py [port=18080] [key=test-key]
Streams /v1/chat/completions as SSE. If tools are offered and the last user message contains 'context', the first
answer is a tool call to infinite_context; after the tool result it answers with text. '--no-tools' as 3rd arg makes
it reject requests that carry `tools` with HTTP 400 (like a model without tool support)."""
import json, sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18080
KEY = sys.argv[2] if len(sys.argv) > 2 else "test-key"
NO_TOOLS = "--no-tools" in sys.argv

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
        if self.headers.get("Authorization") != "Bearer " + KEY:
            return self.send_json(401, {"error": {"message": "Incorrect API key provided"}})
        if NO_TOOLS and body.get("tools"):
            return self.send_json(400, {"error": {"message": "this model does not support tools"}})
        msgs = body.get("messages", [])
        last = msgs[-1] if msgs else {}
        if not body.get("stream"):
            return self.send_json(200, {"choices": [{"message": {"role": "assistant", "content": "ok"}}]})
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()
        def ev(obj): self.wfile.write(b"data: " + json.dumps(obj).encode() + b"\n\n"); self.wfile.flush()
        if last.get("role") == "user" and "context" in str(last.get("content", "")).lower() and body.get("tools"):
            ev({"choices": [{"delta": {"tool_calls": [{"index": 0, "id": "call_1", "function": {"name": "infinite_context", "arguments": ""}}]}}]})
            ev({"choices": [{"delta": {"tool_calls": [{"index": 0, "function": {"arguments": "{}"}}]}, "finish_reason": "tool_calls"}]})
        else:
            tool = last.get("content", "") if last.get("role") == "tool" else ""
            for piece in ["Hello", " from the mock. ", ("Saw tool result: " + tool[:60]) if tool else "No tools used."]:
                ev({"choices": [{"delta": {"content": piece}}]})
            ev({"choices": [{"delta": {}, "finish_reason": "stop"}]})
        ev({"choices": [], "usage": {"prompt_tokens": 11, "completion_tokens": 5}})
        self.wfile.write(b"data: [DONE]\n\n"); self.wfile.flush()
    def send_json(self, code, obj):
        b = json.dumps(obj).encode()
        self.send_response(code); self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)

ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
