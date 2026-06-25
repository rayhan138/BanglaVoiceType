# Implementation Guide — Bangla Voice Typing Keyboard

> **Version:** 1.0.0  
> **Platform:** Windows 10/11 (x64)  
> **Language:** C++ (C++17)  
> **Last Updated:** 2026-06-25

---

## Table of Contents

1. [Overview](#1-overview)
2. [System Requirements](#2-system-requirements)
3. [Core Pipeline](#3-core-pipeline)
4. [Speech-to-Text Engines](#4-speech-to-text-engines)
   - 4.1 [Primary: Groq Whisper API](#41-primary-groq-whisper-api)
   - 4.2 [Fallback: Google Web Speech API](#42-fallback-google-web-speech-api)
   - 4.3 [Engine Selection & Failover Logic](#43-engine-selection--failover-logic)
5. [Audio Pipeline](#5-audio-pipeline)
   - 5.1 [Microphone Capture](#51-microphone-capture)
   - 5.2 [WAV Encoding](#52-wav-encoding)
6. [Text Output Pipeline](#6-text-output-pipeline)
   - 6.1 [Clipboard Integration](#61-clipboard-integration)
   - 6.2 [Keystroke Simulation](#62-keystroke-simulation)
7. [Global Hotkey System](#7-global-hotkey-system)
8. [Configuration](#8-configuration)
9. [Error Handling Strategy](#9-error-handling-strategy)
10. [Security Considerations](#10-security-considerations)
11. [Performance Targets](#11-performance-targets)

---

## 1. Overview

**Bangla Voice Typing Keyboard** is a lightweight, native Windows desktop application that converts spoken Bangla (Bengali) into text and automatically pastes it into any active application.

### Key Design Principles

| Principle | Description |
|-----------|-------------|
| **Minimal Footprint** | < 5MB binary, < 10MB RAM usage |
| **Zero Dependencies** | No runtime installations (Python, .NET, Node.js) |
| **Fail-Safe** | Dual STT engine with automatic failover |
| **Non-Intrusive** | Floating window, system tray, global hotkey |
| **Unicode-First** | Full Unicode/UTF-16 support for Bangla script |

---

## 2. System Requirements

| Requirement | Minimum |
|-------------|---------|
| **OS** | Windows 10 version 1809+ / Windows 11 |
| **Architecture** | x64 |
| **RAM** | 50MB available |
| **Network** | Active internet connection |
| **Audio** | Microphone (built-in or external) |
| **Permissions** | Microphone access, clipboard access |

### Build Requirements (Development Only)

| Tool | Version |
|------|---------|
| **Visual Studio 2022** | v17.0+ (or Build Tools for VS 2022) |
| **Windows SDK** | 10.0.19041.0+ |
| **CMake** | 3.15+ |
| **C++ Standard** | C++17 |

---

## 3. Core Pipeline

The application follows a linear data pipeline with clearly defined stages:

```
┌─────────────────────────────────────────────────────────────────────┐
│                        CORE PIPELINE                                │
│                                                                     │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────────┐  │
│  │  STAGE 1 │───▶│  STAGE 2 │───▶│  STAGE 3 │───▶│   STAGE 4    │  │
│  │  CAPTURE │    │  ENCODE  │    │TRANSCRIBE│    │    OUTPUT     │  │
│  │          │    │          │    │          │    │              │  │
│  │ Mic PCM  │    │ PCM→WAV  │    │ WAV→Text │    │ Clipboard +  │  │
│  │ 16kHz    │    │ In-memory│    │ via API  │    │ Ctrl+V Paste │  │
│  │ 16-bit   │    │ RIFF hdr │    │ (Groq/   │    │ into active  │  │
│  │ Mono     │    │          │    │  Google) │    │ window       │  │
│  └──────────┘    └──────────┘    └──────────┘    └──────────────┘  │
│                                                                     │
│  Thread:  Audio      Main         Worker          Main              │
│  Timing:  Real-time  < 1ms        1-3 sec         < 500ms          │
└─────────────────────────────────────────────────────────────────────┘
```

### Stage Descriptions

| Stage | Input | Output | Thread | Latency |
|-------|-------|--------|--------|---------|
| **1. Capture** | Microphone audio | Raw PCM samples (`int16_t[]`) | Audio thread (miniaudio) | Real-time |
| **2. Encode** | PCM buffer | WAV byte array (`uint8_t[]`) | Main thread | < 1ms |
| **3. Transcribe** | WAV bytes | Bangla text (`std::wstring`) | Worker thread | 1-3 seconds |
| **4. Output** | Bangla text | Pasted into active app | Main thread | < 500ms |

---

## 4. Speech-to-Text Engines

### 4.1 Primary: Groq Whisper API

[Groq](https://console.groq.com) provides access to OpenAI's Whisper model on ultra-fast LPU hardware.

#### API Contract

| Field | Value |
|-------|-------|
| **Endpoint** | `POST https://api.groq.com/openai/v1/audio/transcriptions` |
| **Auth** | `Authorization: Bearer <GROQ_API_KEY>` |
| **Content-Type** | `multipart/form-data` |
| **Model** | `whisper-large-v3-turbo` |
| **Language** | `bn` (ISO 639-1 code for Bengali) |
| **Max File Size** | 25MB |
| **Response Format** | JSON |

#### Request Fields (multipart/form-data)

| Field | Type | Required | Description |
|-------|------|:--------:|-------------|
| `file` | binary | ✅ | Audio file (WAV format, 16kHz, 16-bit, mono) |
| `model` | string | ✅ | `whisper-large-v3-turbo` |
| `language` | string | ✅ | `bn` for Bengali |
| `response_format` | string | ❌ | `json` (default) |

#### Example Request (cURL equivalent)

```bash
curl -X POST "https://api.groq.com/openai/v1/audio/transcriptions" \
  -H "Authorization: Bearer gsk_xxxxxxxxxxxxx" \
  -F "file=@recording.wav" \
  -F "model=whisper-large-v3-turbo" \
  -F "language=bn"
```

#### Success Response (HTTP 200)

```json
{
  "text": "আমি বাংলায় কথা বলছি",
  "x_groq": {
    "id": "req_xxxx"
  }
}
```

#### Error Responses

| HTTP Code | Meaning | Action |
|-----------|---------|--------|
| 400 | Bad request (invalid audio) | Show error to user |
| 401 | Invalid API key | Prompt user to check `config.txt` |
| 413 | File too large (> 25MB) | Limit recording duration |
| 429 | Rate limit exceeded | Fall back to Google STT |
| 500+ | Server error | Fall back to Google STT |

#### Rate Limits (Free Tier)

| Limit | Value |
|-------|-------|
| Requests per minute | 20 |
| Requests per day | 2,000 |
| Audio seconds per hour | 7,200 (2 hours) |
| Audio seconds per day | 28,800 (8 hours) |

---

### 4.2 Fallback: Google Web Speech API

The Google Web Speech API is accessed via the free endpoint bundled with the `SpeechRecognition` ecosystem. This is used as a **fallback only** when Groq is unavailable.

#### API Contract

| Field | Value |
|-------|-------|
| **Endpoint** | `POST https://www.google.com/speech-api/v2/recognize` |
| **Auth** | Default embedded key (no signup) |
| **Query Params** | `output=json&lang=bn-BD&key=AIzaSy...` |
| **Content-Type** | `audio/l16; rate=16000` |
| **Language** | `bn-BD` (BCP-47 code for Bengali, Bangladesh) |

#### Limitations

| Limit | Value |
|-------|-------|
| Daily requests | ~50 (undocumented, may change) |
| API key | Hardcoded default (may be revoked by Google) |
| Accuracy | Good but lower than Whisper for Bangla |
| Reliability | Not guaranteed for production use |

> **Note:** This fallback is intended for graceful degradation only. Users should configure a Groq API key for reliable operation.

---

### 4.3 Engine Selection & Failover Logic

```
                    ┌─────────────────────┐
                    │   Audio Ready for   │
                    │    Transcription     │
                    └──────────┬──────────┘
                               │
                    ┌──────────▼──────────┐
                    │  Groq API key       │
                    │  configured?        │
                    └──────────┬──────────┘
                          ┌────┴────┐
                         YES        NO
                          │          │
               ┌──────────▼──────┐   │
               │ Send to Groq   │   │
               │ Whisper API    │   │
               └──────────┬─────┘   │
                     ┌────┴────┐    │
                  SUCCESS    FAIL   │
                     │         │    │
          ┌──────────▼──┐  ┌──▼────▼─────────┐
          │ Return text │  │ Send to Google   │
          │             │  │ Web Speech API   │
          └─────────────┘  └──────────┬───────┘
                                ┌────┴────┐
                             SUCCESS    FAIL
                                │         │
                     ┌──────────▼──┐  ┌──▼──────────────┐
                     │ Return text │  │ Show error msg  │
                     │             │  │ "No internet /  │
                     └─────────────┘  │  service down"  │
                                      └─────────────────┘
```

#### Failover Triggers

The system falls back to Google STT when Groq returns:
- HTTP 429 (rate limit exceeded)
- HTTP 5xx (server error)
- Connection timeout (> 10 seconds)
- No API key configured

---

## 5. Audio Pipeline

### 5.1 Microphone Capture

Audio is captured using the **miniaudio** library (single-header, cross-platform).

#### Audio Format

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| **Sample Rate** | 16,000 Hz | Optimal for Whisper (trained on 16kHz) |
| **Bit Depth** | 16-bit signed integer | Standard for speech recognition |
| **Channels** | 1 (Mono) | Speech is mono; stereo wastes bandwidth |
| **Format Tag** | PCM (uncompressed) | Maximum compatibility with STT APIs |
| **Byte Rate** | 32,000 bytes/sec | 16,000 × 2 bytes × 1 channel |

#### Recording Buffer

- PCM samples are accumulated in a `std::vector<int16_t>`
- Protected by `std::mutex` (audio callback runs on miniaudio's internal thread)
- Buffer grows dynamically — no fixed recording limit
- Typical 10-second recording ≈ 320KB of raw PCM data

#### Maximum Recording Duration

| Duration | PCM Size | WAV Size | Within Groq 25MB limit? |
|----------|----------|----------|:-----------------------:|
| 10 sec | 320 KB | 320 KB | ✅ |
| 1 min | 1.9 MB | 1.9 MB | ✅ |
| 5 min | 9.6 MB | 9.6 MB | ✅ |
| 13 min | 24.9 MB | 24.9 MB | ✅ (limit) |

We enforce a **maximum recording duration of 12 minutes** to stay within the 25MB API limit.

---

### 5.2 WAV Encoding

Raw PCM data is wrapped in a WAV (RIFF) header in-memory before sending to the API.

#### WAV File Structure

```
Offset  Size  Field           Value
──────  ────  ─────           ─────
0       4     ChunkID         "RIFF"
4       4     ChunkSize       36 + DataSize
8       4     Format          "WAVE"
12      4     Subchunk1ID     "fmt "
16      4     Subchunk1Size   16
20      2     AudioFormat     1 (PCM)
22      2     NumChannels     1 (Mono)
24      4     SampleRate      16000
28      4     ByteRate        32000
32      2     BlockAlign      2
34      2     BitsPerSample   16
36      4     Subchunk2ID     "data"
40      4     Subchunk2Size   NumSamples × 2
44      ...   Data            Raw PCM samples (int16_t[])
```

Total header size: **44 bytes** (constant).

---

## 6. Text Output Pipeline

### 6.1 Clipboard Integration

The recognized Bangla text is placed on the Windows clipboard using the Win32 API.

#### Clipboard Flow

1. `OpenClipboard(NULL)` — acquire clipboard ownership
2. `EmptyClipboard()` — clear current contents
3. `GlobalAlloc(GMEM_MOVEABLE, size)` — allocate global memory
4. `memcpy()` — copy wide string (`wchar_t[]`) into allocated memory
5. `SetClipboardData(CF_UNICODETEXT, hMem)` — set as Unicode text
6. `CloseClipboard()` — release ownership

> **Important:** Bangla text requires `CF_UNICODETEXT` format. The `CF_TEXT` (ANSI) format cannot represent Bengali characters.

---

### 6.2 Keystroke Simulation

After placing text on the clipboard, the app simulates `Ctrl+V` to paste into the active window.

#### SendInput Sequence

```
1. Sleep(200ms)          ← Let target window regain focus
2. SendInput: KEY_DOWN   VK_CONTROL
3. SendInput: KEY_DOWN   VK_V (0x56)
4. SendInput: KEY_UP     VK_V (0x56)
5. SendInput: KEY_UP     VK_CONTROL
```

> **Why not `keybd_event`?** — `SendInput` is the modern replacement. `keybd_event` is deprecated and may not work with UAC-elevated windows.

---

## 7. Global Hotkey System

The application registers a system-wide hotkey using the Win32 `RegisterHotKey` API.

| Parameter | Value |
|-----------|-------|
| **Hotkey** | `Alt+X` |
| **Win32 Modifiers** | `MOD_ALT` |
| **Virtual Key** | `VK_X` (0x58) |
| **Message** | `WM_HOTKEY` (delivered to main message loop) |

#### Behavior

| State | Hotkey Press | Action |
|-------|-------------|--------|
| **Ready** | `Alt+X` | Start recording → state becomes **Recording** |
| **Recording** | `Alt+X` | Stop recording → send to STT → state becomes **Processing** |
| **Processing** | `Alt+X` | Ignored (already processing) |
| **Done** | `Alt+X` | Start new recording → state becomes **Recording** |

---

## 8. Configuration

The application reads configuration from `config.txt` in the same directory as the `.exe`.

#### Config File Format

```ini
# Bangla Voice Typing Keyboard Configuration
# Place this file in the same directory as BanglaVoiceTyping.exe

# Groq API Key (required for best accuracy)
# Get your free key at: https://console.groq.com
GROQ_API_KEY=gsk_your_api_key_here

# Hotkey (optional, default: Alt+X)
# Format: MODIFIER+KEY
# Modifiers: Alt, Ctrl, Shift, Win
# HOTKEY=Alt+X

# Auto-paste (optional, default: true)
# Set to false to only copy to clipboard without auto-pasting
# AUTO_PASTE=true

# STT Engine (optional, default: groq)
# Options: groq, google, auto (try groq first, fallback to google)
# STT_ENGINE=auto
```

#### Config Parsing Rules

1. Lines starting with `#` are comments
2. Empty lines are ignored
3. Format: `KEY=VALUE` (no spaces around `=`)
4. Keys are case-insensitive
5. Values are trimmed of leading/trailing whitespace
6. Missing `config.txt` → use defaults (Google STT only, Alt+X hotkey)

---

## 9. Error Handling Strategy

| Error | User Impact | Handling |
|-------|-------------|----------|
| No microphone detected | Cannot record | Show error in status bar: "🎤 No microphone found" |
| Microphone permission denied | Cannot record | Show: "⚠️ Microphone access denied" |
| No internet connection | Cannot transcribe | Show: "🌐 No internet connection" |
| Groq API key missing | Lower accuracy | Use Google STT, show: "ℹ️ Using Google STT (add Groq key for better accuracy)" |
| Groq API key invalid | Auth failure | Show: "🔑 Invalid API key — check config.txt" |
| Groq rate limit (429) | Temporary | Auto-fallback to Google STT silently |
| Groq server error (5xx) | Temporary | Auto-fallback to Google STT |
| Audio too short (< 0.5s) | No useful speech | Show: "🎤 Recording too short" |
| Audio too long (> 12 min) | Exceeds API limit | Auto-stop at 12 min, show warning |
| Transcription unclear | No text returned | Show: "❓ Could not understand audio" |
| Clipboard access denied | Cannot paste | Show: "📋 Clipboard access denied" |
| Hotkey conflict | Cannot register | Show: "⌨️ Alt+X is already in use by another app" |

---

## 10. Security Considerations

| Concern | Mitigation |
|---------|------------|
| **API key storage** | Stored in local `config.txt` file (not hardcoded in binary) |
| **API key in memory** | Loaded once at startup, stored in `std::wstring` (cleared on exit) |
| **Audio data** | Sent to Groq/Google servers via HTTPS (TLS 1.2+) |
| **Audio on disk** | Audio is **never written to disk** — processed entirely in memory |
| **Clipboard** | Previous clipboard content is saved and restored after paste |
| **Network** | All API calls use HTTPS via WinHTTP (certificate validation enabled) |

---

## 11. Performance Targets

| Metric | Target | Method |
|--------|--------|--------|
| **Binary size** | < 5 MB | Static linking, `/O2` optimization |
| **Startup time** | < 500ms | Minimal initialization, no heavy frameworks |
| **RAM usage (idle)** | < 10 MB | No background processing when idle |
| **RAM usage (recording)** | < 20 MB | PCM buffer grows linearly |
| **Recording → Paste latency** | < 3 seconds | Groq LPU inference is fast |
| **CPU usage (idle)** | ~ 0% | Event-driven (Win32 message loop) |
| **CPU usage (recording)** | < 2% | Lightweight PCM accumulation |
