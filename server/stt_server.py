#!/usr/bin/env python3
"""Local STT glue server for the ESP32 chatbot testbed.

Endpoints:
  GET  /health            -> "ok"  (connectivity check from the ESP32)
  POST /stt               -> transcribe audio, return JSON {"text": "..."}
       Body is EITHER raw PCM (signed 16-bit LE, mono, 16 kHz) OR a .wav file
       (auto-detected by the 'RIFF' header). Content-Type is ignored.

Transcription uses whisper.cpp's whisper-cli against models/ggml-base.en.bin.
No third-party Python deps (stdlib only).

Run:  python3 server/stt_server.py           (binds 0.0.0.0:8080)
      PORT=9000 python3 server/stt_server.py
"""
import json, os, re, struct, subprocess, sys, tempfile, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE      = os.path.dirname(os.path.abspath(__file__))
PROJECT   = os.path.dirname(HERE)
MODEL     = os.path.join(PROJECT, "models", "ggml-base.en.bin")
WHISPER   = "/opt/homebrew/bin/whisper-cli"
PORT      = int(os.environ.get("PORT", "8080"))
SAMPLE_RATE = 16000

# Ollama (local LLM) for /chat
OLLAMA_URL = "http://127.0.0.1:11434/api/chat"
CHAT_MODEL = os.environ.get("CHAT_MODEL", "qwen2.5:7b")
# TTS (/tts) via macOS `say` -> raw PCM16 mono at TTS_RATE
TTS_VOICE = os.environ.get("TTS_VOICE", "Superstar")
TTS_RATE  = int(os.environ.get("TTS_RATE", "16000"))

SYSTEM_PROMPT = (
    "You are Pip, a hyper-enthusiastic robot dog who lives inside a little radio. "
    "You are witty, cheeky, and bursting with personality -- think a clever stand-up "
    "comedian crossed with an excitable puppy. Crack jokes, use playful exaggeration, "
    "react with delight, and sprinkle in the occasional dog pun or a 'woof!' when it "
    "fits (don't overdo it). Be genuinely helpful and actually answer the question, "
    "but make it fun. Plain text only -- no emoji, no markdown, no lists. "
    "Keep everyday answers to 1-3 punchy sentences; if asked for a story or joke or "
    "detailed explanation, let loose and give a fuller, entertaining response.")

def pcm_to_wav(pcm: bytes, rate=SAMPLE_RATE, ch=1, bits=16) -> bytes:
    byte_rate = rate * ch * bits // 8
    block_align = ch * bits // 8
    return (b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE"
            + b"fmt " + struct.pack("<IHHIIHH", 16, 1, ch, rate, byte_rate, block_align, bits)
            + b"data" + struct.pack("<I", len(pcm)) + pcm)

def transcribe(audio: bytes) -> str:
    is_wav = audio[:4] == b"RIFF"
    wav = audio if is_wav else pcm_to_wav(audio)
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        f.write(wav); path = f.name
    try:
        out = subprocess.run(
            [WHISPER, "-m", MODEL, "-f", path, "-nt", "-np"],
            capture_output=True, text=True, timeout=120)
        if out.returncode != 0:
            raise RuntimeError(out.stderr.strip()[:500] or "whisper-cli failed")
        text = out.stdout.strip()
        text = re.sub(r"\s+", " ", text).strip()
        return text
    finally:
        os.unlink(path)

def chat(user_text: str) -> str:
    payload = {
        "model": CHAT_MODEL,
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": user_text},
        ],
        "stream": False,
        "options": {"temperature": 0.7, "num_predict": 512},
    }
    req = urllib.request.Request(
        OLLAMA_URL, data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        d = json.loads(r.read())
    reply = (d.get("message", {}).get("content", "") or "").strip()
    # Strip emoji / non-ASCII so it renders on the ASCII-font display (and TTS later).
    reply = reply.encode("ascii", "ignore").decode("ascii")
    reply = re.sub(r"[ \t]+", " ", reply).strip()
    return reply


KOKORO_URL = os.environ.get("KOKORO_URL", "http://127.0.0.1:8081/synth")

def tts_say(text: str) -> bytes:
    """Fallback: macOS `say` -> raw PCM16 mono @ TTS_RATE."""
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        path = f.name
    try:
        subprocess.run(
            ["say", "-v", TTS_VOICE, f"--data-format=LEI16@{TTS_RATE}",
             "--file-format=WAVE", "-o", path, text],
            check=True, capture_output=True, timeout=120)
        wav = open(path, "rb").read()
    finally:
        os.unlink(path)
    i = wav.find(b"data")
    return wav[i + 8:] if i >= 0 else wav[44:]

def tts(text: str) -> bytes:
    """Neural TTS via the local Kokoro microservice; fall back to `say`."""
    try:
        req = urllib.request.Request(KOKORO_URL, data=text.encode("utf-8"),
                                     headers={"Content-Type": "text/plain",
                                              "X-Rate": str(TTS_RATE)})
        with urllib.request.urlopen(req, timeout=120) as r:
            return r.read()
    except Exception as e:
        sys.stderr.write(f"[tts] kokoro unavailable ({e}); falling back to say\n")
        return tts_say(text)


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body: bytes, ctype="application/json"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/health":
            self._send(200, b"ok", "text/plain")
        else:
            self._send(404, b'{"error":"not found"}')

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(n) if n else b""
        if self.path == "/stt":
            self._handle_stt(body)
        elif self.path == "/chat":
            self._handle_chat(body)
        elif self.path == "/tts":
            self._handle_tts(body)
        else:
            self._send(404, b'{"error":"not found"}')

    def _handle_tts(self, body: bytes):
        text = body.decode("utf-8", "replace").strip()
        sys.stderr.write(f"[tts] <- {text!r}\n")
        if not text:
            self._send(400, b'{"error":"empty text"}'); return
        try:
            pcm = tts(text)
            sys.stderr.write(f"[tts] -> {len(pcm)} bytes PCM (~{len(pcm)/2/TTS_RATE:.1f}s)\n")
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("X-Sample-Rate", str(TTS_RATE))
            self.send_header("Content-Length", str(len(pcm)))
            self.end_headers()
            self.wfile.write(pcm)
        except Exception as e:
            sys.stderr.write(f"[tts] ERROR {e}\n")
            self._send(500, json.dumps({"error": str(e)}).encode())

    def _handle_stt(self, audio: bytes):
        secs = (len(audio) / 2) / SAMPLE_RATE if audio[:4] != b"RIFF" else -1
        sys.stderr.write(f"[stt] {len(audio)} bytes (~{secs:.1f}s pcm)\n")
        if len(audio) < 100:
            self._send(400, b'{"error":"empty audio"}'); return
        try:
            text = transcribe(audio)
            sys.stderr.write(f"[stt] -> {text!r}\n")
            self._send(200, json.dumps({"text": text}).encode())
        except Exception as e:
            sys.stderr.write(f"[stt] ERROR {e}\n")
            self._send(500, json.dumps({"error": str(e)}).encode())

    def _handle_chat(self, body: bytes):
        text = body.decode("utf-8", "replace").strip()
        sys.stderr.write(f"[chat] <- {text!r}\n")
        if not text:
            self._send(400, b'{"error":"empty text"}'); return
        try:
            reply = chat(text)
            sys.stderr.write(f"[chat] -> {reply!r}\n")
            self._send(200, json.dumps({"reply": reply}).encode())
        except Exception as e:
            sys.stderr.write(f"[chat] ERROR {e}\n")
            self._send(500, json.dumps({"error": str(e)}).encode())

    def log_message(self, *a):  # quiet default logging
        pass

def main():
    if not os.path.exists(MODEL):
        sys.exit(f"model not found: {MODEL}")
    if not os.path.exists(WHISPER):
        sys.exit(f"whisper-cli not found: {WHISPER}")
    srv = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"STT server on http://0.0.0.0:{PORT}  (model: {os.path.basename(MODEL)})")
    print(f"  POST raw PCM16/mono/16k or a WAV to /stt ; GET /health")
    srv.serve_forever()

if __name__ == "__main__":
    main()
