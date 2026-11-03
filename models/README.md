# Models

Model files are **not** checked into git (they're large). Fetch them with:

```bash
scripts/download_models.sh
```

This downloads:

| File | Size | Used by | Purpose |
|------|------|---------|---------|
| `ggml-base.en.bin` | ~142 MB | `server/stt_server.py` (whisper.cpp) | speech‑to‑text |
| `kokoro-v1.0.onnx` | ~310 MB | `server/kokoro_tts.py` | neural TTS model |
| `voices-v1.0.bin`  | ~27 MB  | `server/kokoro_tts.py` | Kokoro voice embeddings |
