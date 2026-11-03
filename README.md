# ESP32 Projects

A collection of ESP32 / embedded projects. The current flagship is the **Chatbot Dog** —
a standalone, Wi-Fi voice chatbot on an ESP32‑S3 touchscreen board that talks to a local
AI "brain" running on a Mac (speech‑to‑text → LLM → text‑to‑speech), all on the LAN.

> Future ESP32 projects will be added as sibling folders; today the repo root holds the
> Chatbot Dog.

---

## 🐶 Chatbot Dog

Tap a button on the touchscreen, talk, and a cheeky robot‑dog persona ("Pip") transcribes
what you said, thinks up a reply with a local LLM, and **speaks it back** through the
onboard speaker in a natural neural voice. The eventual enclosure is a small radio‑style
housing, possibly inside a crocheted dog.

### How it works

```
                ESP32‑S3 (device)                         Mac (local "brain")
   ┌─────────────────────────────────┐          ┌──────────────────────────────────┐
   │  mic ─▶ ES8311 ADC ─▶ I2S        │          │  /stt   whisper.cpp (base.en)     │
   │  touchscreen UI (ST77922, LVGL‑  │  Wi‑Fi   │  /chat  Ollama (qwen2.5:7b, "Pip")│
   │  free Arduino_GFX) push‑to‑talk  │ ───────▶ │  /tts   Kokoro neural TTS         │
   │  record PCM ─▶ HTTP POST ────────┼──────────┤         (say fallback)            │
   │  play reply ◀─ I2S ◀─ ES8311 DAC │ ◀─────── │  stt_server.py + kokoro_tts.py    │
   └─────────────────────────────────┘   audio   └──────────────────────────────────┘
```

State machine on the device:
`IDLE → RECORDING → UPLOADING(/stt) → THINKING(/chat) → SPEAKING(/tts) → RESULT`

### Hardware

| | |
|---|---|
| Board | LCDWIKI **ES3C35P** — 3.5" IPS ESP32‑S3 touchscreen |
| MCU | ESP32‑S3 (rev v0.2), dual‑core 240 MHz, **8 MB PSRAM**, **16 MB flash** |
| Display | **Sitronix ST77922**, 320×480 RGB565, QSPI @ 40 MHz |
| Touch | Integrated in ST77922, I²C **0x55** (not the address the vendor PDF prints) |
| Audio codec | **ES8311** @ I²C 0x18; mic in + speaker out; amp enable GPIO1 (active LOW) |
| Connectivity | 2.4 GHz Wi‑Fi, BLE; USB‑Serial/JTAG |

**Pin map** (ESP32‑S3 GPIO): LCD QSPI CS=10 SCK=12 D0=11 D1=13 D2=14 D3=9, backlight=41 ·
Touch/I²C SDA=38 SCL=39 INT=47 RST=48 · Audio I2S MCLK=17 BCLK=18 WS=21 DOUT=15 DIN=16 ·
Amp enable=1 (active LOW).

> ⚠️ The display panel reset is tied to chip power (no GPIO), so a software reboot does
> **not** reset the panel — a USB unplug/replug (cold power cycle) is sometimes needed.
> The USB‑Serial/JTAG can also wedge; the same cold power cycle recovers it.

---

## Repo layout

```
.
├── sketches/                 # Arduino sketches (bring‑up tests → full app)
│   ├── 00_Quiet/             # disable amp / idle
│   ├── 04_Speaker_Tone/      # ES8311 DAC + speaker tone (known‑good audio baseline)
│   ├── 05_Microphone_Record/ # ES8311 mic capture + amplitude meter
│   ├── 06_WiFi_Connection/   # Wi‑Fi association test
│   ├── 10_Display_Test/      # ST77922 display + touch bring‑up (custom panel class)
│   ├── 11_Touch_Probe/       # touch register reverse‑engineering probe
│   └── 20_Chatbot_STT/       # ⭐ the full chatbot app
├── server/
│   ├── stt_server.py         # LAN‑facing server: /health /stt /chat /tts
│   └── kokoro_tts.py         # localhost Kokoro neural‑TTS microservice
├── scripts/
│   └── download_models.sh    # fetch whisper + Kokoro models (not stored in git)
├── esp.sh                    # build/flash/monitor helper (wraps arduino-cli + FQBN)
└── models/                   # model files (gitignored; see scripts/download_models.sh)
```

---

## Setup

### Device (Arduino toolchain, headless via arduino‑cli)

```bash
brew install arduino-cli
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32            # 3.3.12
arduino-cli lib install "DevLab_AudioTools@1.2.6" "DevLab_AudioDriver" "GFX Library for Arduino"
```

Board settings are baked into `esp.sh` as this FQBN:

```
esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashMode=qio,FlashSize=16M,\
PartitionScheme=app3M_fat9M_16MB,PSRAM=opi
```

Wi‑Fi credentials: copy `sketches/<name>/wifi_secrets.h.example` → `wifi_secrets.h` and fill
in your 2.4 GHz SSID/password (gitignored). In `sketches/20_Chatbot_STT/20_Chatbot_STT.ino`
set `SERVER_BASE` to your Mac's LAN IP.

```bash
./esp.sh flash sketches/20_Chatbot_STT      # compile + upload
./esp.sh monitor                            # serial @115200
```

### Server (the Mac "brain")

```bash
brew install whisper-cpp ollama
ollama pull qwen2.5:7b
scripts/download_models.sh                   # whisper + Kokoro model files

# Kokoro TTS venv
python3.12 -m venv server/tts-venv
server/tts-venv/bin/pip install kokoro-onnx soxr soundfile

# Run BOTH (two terminals):
server/tts-venv/bin/python server/kokoro_tts.py          # localhost:8081 (neural TTS)
/usr/bin/python3 server/stt_server.py                    # 0.0.0.0:8080 (LAN)
```

> **macOS firewall gotcha:** the LAN‑facing server must run under Apple‑signed
> `/usr/bin/python3`. Homebrew Python is ad‑hoc signed and macOS silently resets its
> incoming LAN connections even when "allowed". The Kokoro service binds to localhost
> only (loopback is firewall‑exempt), so it can use the venv Python.

---

## Status

### ✅ Working
- All hardware verified: ES8311 codec, speaker (DAC), microphone (ADC), Wi‑Fi, ST77922
  display, capacitive touch.
- End‑to‑end voice chatbot: push‑to‑talk → transcription on screen → LLM reply → spoken
  reply through the speaker.
- Two‑panel touchscreen UI ("You:" / "Pip:"), mic‑reactive animation, scrollable replies.
- Local brain: whisper.cpp (STT), Ollama `qwen2.5:7b` with a funny "Pip" persona (LLM),
  Kokoro neural TTS (natural voice; `say` fallback).

### 🔭 Roadmap
- **Latency (in progress):** stream the pipeline — sentence‑chunked TTS so Pip starts
  talking ~1 s after you stop; keep whisper resident (no per‑turn reload); move the LLM to
  MLX + a 3B model. Target < 1 s to first audio.
- **Fidelity:** bump device playback 16 kHz → 24 kHz for full‑quality Kokoro audio.
- **Later:** point the device at a dedicated home server; conversation memory; wake word;
  LVGL UI polish; the physical crocheted‑dog enclosure.

### Measured latencies (warm, localhost)
`/stt` ~0.2–0.4 s · `/chat` ~0.5–0.7 s (short reply) · `/tts` (Kokoro) RTF ~0.29.
Perceived latency today is dominated by waiting for the **full** LLM reply before speaking —
hence the streaming work above.

---

## Notes
- Keep the known‑good sketches as references; don't swap the ES8311 driver for a generic
  ES8388 config — this board is ES8311.
- The factory firmware backup (if taken) lives in `backup/` and is gitignored.

🤖 Built with [Claude Code](https://claude.com/claude-code).
