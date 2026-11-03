#!/usr/bin/env bash
# Launch all three local "brain" services for the Chatbot Dog.
#   - whisper-server  :8082  (STT, resident on GPU)      [homebrew whisper-cpp]
#   - kokoro_tts.py   :8081  (neural TTS, loopback only)  [server/tts-venv python]
#   - stt_server.py   :8080  (LAN-facing: /stt /chat /tts /converse) [Apple-signed /usr/bin/python3]
# Logs go to /tmp/*.log. Ctrl-C stops all. Run from anywhere.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"

echo "stopping any existing instances..."
pkill -f "whisper-server .*ggml-base" 2>/dev/null || true
pkill -f "kokoro_tts.py" 2>/dev/null || true
pkill -f "stt_server.py" 2>/dev/null || true
sleep 1

echo "[1/3] whisper-server (STT) :8082"
whisper-server -m "$ROOT/models/ggml-base.en.bin" --host 127.0.0.1 --port 8082 -t 4 --convert \
  >/tmp/whisper_server.log 2>&1 &

echo "[2/3] kokoro_tts (TTS) :8081"
"$ROOT/server/tts-venv/bin/python" "$ROOT/server/kokoro_tts.py" >/tmp/kokoro.log 2>&1 &

echo "   warming Ollama (qwen2.5:7b)..."
curl -s --max-time 60 http://127.0.0.1:11434/api/generate \
  -d '{"model":"qwen2.5:7b","prompt":"hi","stream":false,"keep_alive":"30m"}' >/dev/null 2>&1 || true

# wait for Kokoro to finish loading its model (cold load can take several seconds)
printf "   waiting for Kokoro to load"
for i in $(seq 1 30); do
  curl -s --max-time 2 http://127.0.0.1:8081/health >/dev/null 2>&1 && { echo " ready"; break; }
  printf "."; sleep 1
done

echo "[3/3] stt_server (LAN gateway) :8080  -- must be Apple-signed python"
/usr/bin/python3 "$ROOT/server/stt_server.py" >/tmp/stt_server.log 2>&1 &

sleep 2
echo
echo "health checks:"
printf "  whisper :8082  "; curl -s --max-time 3 http://127.0.0.1:8082/ >/dev/null && echo "up" || echo "(no / route; check /tmp/whisper_server.log)"
printf "  kokoro  :8081  "; curl -s --max-time 3 http://127.0.0.1:8081/health || echo "DOWN"; echo
printf "  gateway :8080  "; curl -s --max-time 3 http://127.0.0.1:8080/health || echo "DOWN"; echo
echo
echo "All launched. LAN endpoint: http://$(ipconfig getifaddr en0 2>/dev/null || echo '<mac-ip>'):8080"
echo "Tail logs: tail -f /tmp/stt_server.log /tmp/kokoro.log /tmp/whisper_server.log"
echo "Stop all: pkill -f 'whisper-server|kokoro_tts.py|stt_server.py'"
wait
