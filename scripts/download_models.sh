#!/usr/bin/env bash
# Fetch the model files the server needs (not stored in git).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p models && cd models

echo "== Whisper STT model (base.en, ~142MB) =="
[ -f ggml-base.en.bin ] || curl -L -o ggml-base.en.bin \
  https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin

echo "== Kokoro TTS model (~310MB) + voices (~27MB) =="
[ -f kokoro-v1.0.onnx ] || curl -L -o kokoro-v1.0.onnx \
  https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.0/kokoro-v1.0.onnx
[ -f voices-v1.0.bin ] || curl -L -o voices-v1.0.bin \
  https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.0/voices-v1.0.bin

echo "Done. Models in $(pwd)"
ls -lh ggml-base.en.bin kokoro-v1.0.onnx voices-v1.0.bin
