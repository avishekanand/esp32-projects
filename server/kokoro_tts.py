#!/usr/bin/env python3
"""Kokoro neural-TTS microservice (localhost only -> firewall-exempt).

The LAN-facing stt_server.py (/usr/bin/python3) forwards /tts here. This process
runs in server/tts-venv with the ML deps and keeps the model warm.

  GET  /health            -> "ok"
  POST /synth             -> raw PCM16 mono. Body = text.
       Header X-Rate: output sample rate (default 16000). Native model rate is
       24000; we resample with soxr. Header X-Voice overrides the voice.

Run:  server/tts-venv/bin/python server/kokoro_tts.py      (binds 127.0.0.1:8081)
"""
import os, sys, struct
import numpy as np, soxr
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from kokoro_onnx import Kokoro

HERE    = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
MODEL   = os.path.join(PROJECT, "models", "kokoro-v1.0.onnx")
VOICES  = os.path.join(PROJECT, "models", "voices-v1.0.bin")
PORT    = int(os.environ.get("KOKORO_PORT", "8081"))
VOICE   = os.environ.get("KOKORO_VOICE", "af_heart")
SPEED   = float(os.environ.get("KOKORO_SPEED", "1.0"))
NATIVE  = 24000

print("loading Kokoro...", flush=True)
_k = Kokoro(MODEL, VOICES)
print(f"Kokoro ready (voice={VOICE}, speed={SPEED})", flush=True)

def synth(text: str, out_rate: int, voice: str) -> bytes:
    samples, sr = _k.create(text, voice=voice, speed=SPEED, lang="en-us")  # float32 @24k
    if out_rate != sr:
        samples = soxr.resample(samples, sr, out_rate)
    pcm = (np.clip(samples, -1.0, 1.0) * 32767.0).astype("<i2").tobytes()
    return pcm

class H(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/health":
            self.send_response(200); self.send_header("Content-Length", "2"); self.end_headers()
            self.wfile.write(b"ok")
        else:
            self.send_response(404); self.end_headers()

    def do_POST(self):
        if self.path != "/synth":
            self.send_response(404); self.end_headers(); return
        n = int(self.headers.get("Content-Length", "0"))
        text = self.rfile.read(n).decode("utf-8", "replace").strip() if n else ""
        rate = int(self.headers.get("X-Rate", "16000"))
        voice = self.headers.get("X-Voice", VOICE)
        if not text:
            self.send_response(400); self.end_headers(); return
        try:
            pcm = synth(text, rate, voice)
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("X-Sample-Rate", str(rate))
            self.send_header("Content-Length", str(len(pcm)))
            self.end_headers()
            self.wfile.write(pcm)
        except Exception as e:
            sys.stderr.write(f"[kokoro] ERROR {e}\n")
            msg = str(e).encode()
            self.send_response(500); self.send_header("Content-Length", str(len(msg)))
            self.end_headers(); self.wfile.write(msg)

    def log_message(self, *a): pass

if __name__ == "__main__":
    srv = ThreadingHTTPServer(("127.0.0.1", PORT), H)
    print(f"Kokoro TTS on http://127.0.0.1:{PORT}  POST /synth", flush=True)
    srv.serve_forever()
