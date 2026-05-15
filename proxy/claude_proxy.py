#!/usr/bin/env python3
"""
Claude64 proxy — bridges IP232/TCP from VICE to the Anthropic Messages API.

Usage:
    export ANTHROPIC_API_KEY=sk-ant-...
    python3 claude_proxy.py

VICE connects to 127.0.0.1:25232 using Swiftlink + IP232 protocol.
Test without VICE:  nc 127.0.0.1 25232   then type  U hello\n.\n
"""
import os
import sys
import json
import socket
import ssl
import http.client
import threading
import time
from pathlib import Path

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

API_HOST   = "api.anthropic.com"
API_KEY    = os.environ.get("ANTHROPIC_API_KEY", "")
MODEL      = "claude-haiku-4-5-20251001"
MAX_TOKENS = 512
LISTEN_HOST = "127.0.0.1"
LISTEN_PORT = 25232
MAX_TEXT_PAYLOAD = 72

SYSTEM_PROMPT = (
    "You are Claude, a helpful assistant running on a Commodore 64. "
    "Keep responses short and conversational. "
    "Use plain ASCII only — no markdown, no bullet points, no special symbols."
)

# Common non-ASCII → ASCII substitutions
_FOLD = {
    "‘": "'",  "’": "'",
    "“": '"',  "”": '"',
    "–": "-",  "—": "-",
    "…": "...",
    "•": "*",  "·": "*",
    "é": "e",  "è": "e",  "ê": "e",
    "à": "a",  "â": "a",
    "ô": "o",  "û": "u",
    "ç": "c",
}

def fold_ascii(text: str) -> str:
    for src, dst in _FOLD.items():
        text = text.replace(src, dst)
    return text.encode("ascii", "replace").decode("ascii")


# ---------------------------------------------------------------------------
# IP232 framing
# ---------------------------------------------------------------------------

class IP232Socket:
    """
    Wraps a raw TCP socket with IP232 framing.
    IP232 uses 0xFF as an escape byte:
        0xFF 0x00 → DCD/DTR off signal
        0xFF 0x01 → DCD/DTR on signal
        0xFF 0xFF → literal 0xFF in data
    """

    def __init__(self, sock: socket.socket):
        self._sock = sock
        self._buf  = b""

    def assert_dcd(self):
        """Send DCD-on so the C64's ACIA sees carrier."""
        self._sock.sendall(b"\xff\x01")

    def send(self, data: bytes):
        out = bytearray()
        for b in data:
            if b == 0xFF:
                out += b"\xff\xff"
            else:
                out.append(b)
        self._sock.sendall(bytes(out))

    def sendline(self, text: str):
        print(f"[proxy] send: {text!r}")
        self.send(text.encode("ascii", "replace") + b"\n")

    def recv_line(self) -> str:
        """Blocking read of one '\\n'-terminated line; strips IP232 escapes."""
        line          = bytearray()
        escape_next   = False

        while True:
            # Drain buffer first
            while self._buf:
                b          = self._buf[0]
                self._buf  = self._buf[1:]

                if escape_next:
                    escape_next = False
                    if b == 0xFF:
                        line.append(0xFF)
                    # 0x00 / 0x01 are DTR signals from VICE — discard
                    continue

                if b == 0xFF:
                    escape_next = True
                    continue

                # VICE/6551 mangles the first byte after the transmitter
                # has been idle by setting bit 7. Since our protocol is pure
                # 7-bit ASCII, just mask bit 7 unconditionally.
                b &= 0x7F
                # Accept CR or LF as line terminator.
                if b == ord('\n') or b == ord('\r'):
                    if line:
                        return line.decode("ascii", "replace")
                    continue
                line.append(b)

            # Need more data from socket
            chunk = self._sock.recv(512)
            if not chunk:
                raise EOFError("connection closed")
            print(f"[proxy] raw recv ({len(chunk)} bytes): {chunk!r}")
            self._buf += chunk

    def close(self):
        try:
            self._sock.close()
        except Exception:
            pass


class RawSocket:
    """Plain TCP line socket used by the IP65 Ethernet client."""

    def __init__(self, sock: socket.socket):
        self._sock = sock
        self._buf = b""

    def assert_dcd(self):
        pass

    def send(self, data: bytes):
        self._sock.sendall(data)

    def sendline(self, text: str):
        print(f"[proxy] send: {text!r}")
        self.send(text.encode("ascii", "replace") + b"\n")

    def recv_line(self) -> str:
        line = bytearray()
        while True:
            while self._buf:
                b = self._buf[0]
                self._buf = self._buf[1:]
                b &= 0x7F
                if b == ord('\n') or b == ord('\r'):
                    if line:
                        return line.decode("ascii", "replace")
                    continue
                line.append(b)

            chunk = self._sock.recv(512)
            if not chunk:
                raise EOFError("connection closed")
            print(f"[proxy] raw recv ({len(chunk)} bytes): {chunk!r}")
            self._buf += chunk

    def close(self):
        try:
            self._sock.close()
        except Exception:
            pass


def run_turn(messages: list, user_text: str, emit_line, echo_mode: bool) -> None:
    print(f"[proxy] user: {user_text[:60]!r}")
    messages.append({"role": "user", "content": user_text})
    emit_line("S ")

    assistant_parts = []

    def on_chunk(text: str):
        assistant_parts.append(text)
        parts = text.split("\n")
        for i, part in enumerate(parts):
            while part:
                emit_line(f"T {part[:MAX_TEXT_PAYLOAD]}")
                part = part[MAX_TEXT_PAYLOAD:]
            if i < len(parts) - 1:
                emit_line("T ")

    if echo_mode:
        on_chunk(f"echo: {user_text}")
        stop_reason = "end_turn"
    else:
        stop_reason = call_anthropic(messages, on_chunk)

    messages.append({
        "role": "assistant",
        "content": "".join(assistant_parts),
    })
    emit_line(f"E {stop_reason}")
    print(f"[proxy] done ({stop_reason})")


# ---------------------------------------------------------------------------
# Anthropic API
# ---------------------------------------------------------------------------

def call_anthropic(messages: list, on_chunk) -> str:
    """
    Stream the Anthropic Messages API.
    Calls on_chunk(text: str) for each text delta.
    Returns the stop_reason string.
    """
    ctx  = ssl.create_default_context()
    conn = http.client.HTTPSConnection(API_HOST, context=ctx, timeout=30)

    payload = json.dumps({
        "model":      MODEL,
        "max_tokens": MAX_TOKENS,
        "stream":     True,
        "system": [{
            "type":          "text",
            "text":          SYSTEM_PROMPT,
            "cache_control": {"type": "ephemeral"},
        }],
        "messages": messages,
    })

    hdrs = {
        "x-api-key":          API_KEY,
        "anthropic-version":  "2023-06-01",
        "anthropic-beta":     "prompt-caching-2024-07-31",
        "content-type":       "application/json",
    }

    conn.request("POST", "/v1/messages", body=payload, headers=hdrs)
    resp = conn.getresponse()

    if resp.status != 200:
        body = resp.read(256).decode("utf-8", "replace")
        conn.close()
        raise RuntimeError(f"API error {resp.status}: {body[:80]}")

    stop_reason = "end_turn"

    while True:
        raw = resp.readline()
        if not raw:
            break
        line = raw.decode("utf-8", "replace").rstrip("\r\n")
        if not line.startswith("data: "):
            continue
        data = line[6:]
        if data == "[DONE]":
            break
        try:
            event = json.loads(data)
        except json.JSONDecodeError:
            continue

        etype = event.get("type", "")
        if etype == "content_block_delta":
            delta = event.get("delta", {})
            if delta.get("type") == "text_delta":
                text = fold_ascii(delta.get("text", ""))
                if text:
                    on_chunk(text)
        elif etype == "message_delta":
            stop_reason = event.get("delta", {}).get("stop_reason", stop_reason)

    conn.close()
    return stop_reason


# ---------------------------------------------------------------------------
# Per-connection handler
# ---------------------------------------------------------------------------

def handle_client(raw_sock: socket.socket, addr, socket_cls=IP232Socket):
    ip232    = socket_cls(raw_sock)
    messages = []
    print(f"[proxy] {addr} connected — asserting DCD")
    ip232.assert_dcd()

    try:
        while True:
            # Read any non-empty line and treat it as the user message.
            line = ip232.recv_line()
            user_text = "".join(c for c in line if c.isprintable()).strip()
            if not user_text:
                continue
            try:
                run_turn(messages, user_text, ip232.sendline, echo_mode=False)
            except RuntimeError as exc:
                ip232.sendline(f"E error: {str(exc)[:40]}")
                print(f"[proxy] API error: {exc}")
                continue

    except EOFError:
        print(f"[proxy] {addr} disconnected")
    except Exception as exc:
        print(f"[proxy] {addr} error: {exc}")
        try:
            ip232.sendline(f"E error: {str(exc)[:40]}")
        except Exception:
            pass
    finally:
        ip232.close()


# ---------------------------------------------------------------------------
# Server
# ---------------------------------------------------------------------------

def echo_handler(raw_sock: socket.socket, addr, socket_cls=IP232Socket):
    """Test mode: echo the user text back without calling Anthropic."""
    ip232 = socket_cls(raw_sock)
    print(f"[echo] {addr} connected")
    ip232.assert_dcd()
    try:
        while True:
            line = ip232.recv_line()
            user_text = "".join(c for c in line if c.isprintable()).strip()
            if not user_text:
                continue
            print(f"[echo] got: {user_text!r}")
            run_turn([], user_text, ip232.sendline, echo_mode=True)
    except EOFError:
        print(f"[echo] {addr} disconnected")
    finally:
        ip232.close()


def file_bridge(bridge_dir: Path, echo_mode: bool):
    messages = []
    request_path = bridge_dir / "REQUEST"
    response_path = bridge_dir / "RESPONSE"
    tmp_path = bridge_dir / "RESPONSE.TMP"

    print(f"[bridge] watching {bridge_dir}")

    while True:
        if not request_path.exists():
            time.sleep(0.1)
            continue

        try:
            user_text = request_path.read_text(encoding="ascii", errors="replace").strip()
            request_path.unlink(missing_ok=True)
            response_path.unlink(missing_ok=True)
            tmp_path.unlink(missing_ok=True)
        except OSError as exc:
            print(f"[bridge] file error: {exc}")
            time.sleep(0.1)
            continue

        if not user_text:
            continue

        lines = []
        try:
            run_turn(messages, user_text, lines.append, echo_mode=echo_mode)
        except RuntimeError as exc:
            print(f"[bridge] API error: {exc}")
            lines = [f"E error: {str(exc)[:40]}"]
        except Exception as exc:
            print(f"[bridge] error: {exc}")
            lines = [f"E error: {str(exc)[:40]}"]

        tmp_path.write_text("\n".join(lines) + "\n", encoding="ascii")
        tmp_path.replace(response_path)


def main():
    echo_mode = "--echo" in sys.argv
    raw_mode = "--raw" in sys.argv
    bridge_dir = None

    if "--bridge-dir" in sys.argv:
        idx = sys.argv.index("--bridge-dir")
        if idx + 1 >= len(sys.argv):
            print("error: --bridge-dir requires a path", file=sys.stderr)
            sys.exit(1)
        bridge_dir = Path(sys.argv[idx + 1])

    if not echo_mode and not API_KEY:
        print("error: ANTHROPIC_API_KEY is not set", file=sys.stderr)
        sys.exit(1)

    if bridge_dir is not None:
        bridge_dir.mkdir(parents=True, exist_ok=True)
        file_bridge(bridge_dir, echo_mode=echo_mode)
        return

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listen = ("0.0.0.0" if raw_mode else LISTEN_HOST, LISTEN_PORT)
    srv.bind(listen)
    srv.listen(4)
    transport = "raw" if raw_mode else "ip232"
    mode_str = "echo" if echo_mode else f"model={MODEL}  max_tokens={MAX_TOKENS}"
    mode_str = f"{mode_str}  transport={transport}"
    print(f"[proxy] listening on {listen[0]}:{listen[1]}  [{mode_str}]")

    handler = echo_handler if echo_mode else handle_client
    socket_cls = RawSocket if raw_mode else IP232Socket

    try:
        while True:
            conn, addr = srv.accept()
            t = threading.Thread(
                target=handler, args=(conn, addr, socket_cls), daemon=True
            )
            t.start()
    except KeyboardInterrupt:
        print("\n[proxy] shutting down")
    finally:
        srv.close()


if __name__ == "__main__":
    main()
