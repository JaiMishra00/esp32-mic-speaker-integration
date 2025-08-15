# esp32 Mic Speaker Integration
With just a microphone, a speaker, and this code, your ESP32 can listen, process, and speak back – for voice assistants, intercoms, sound-triggered projects, or just teaching your microcontroller to say “hello” right back to you.
This project builds a simple, end-to-end audio pipeline using an ESP32, a Flask+WebSocket server, and a MAX98357 amplifier to achieve a microphone-to-server-to-speaker loop with a downloadable recording. The software is split across two main components:
ESP32 firmware (C++/Arduino): real-time audio capture, streaming, control, and playback.
Server (Python/Flask + async WebSocket): ingestion, file creation (WAV), streaming back to the ESP32, and download endpoint.

## Hardware
1. esp32 dev board
2. INMP441 (or equivalent I2S digital microphone)
3. MAX98357A (or equivalent I2S DAC/power amplifier)
4. Full-range speaker (4–8Ω, 3–5W recommended)
5. Power supply:
    ESP32: 5V via USB or regulated 5V/3.3V supply
    MAX98357A: 5V recommended (can run on 3.3V at lower output)
6. Breadboard and jumper wires

## Key Design Choices

Sample format: 16kHz, 16-bit mono

    16kHz is a good balance between intelligibility and bandwidth for voice and midrange audio, reducing Wi-Fi load versus 44.1/48kHz.

    16-bit PCM keeps a standard, simple pathway into WAV without conversion.

Streaming in small chunks

    Avoids large memory spikes on ESP32.

    Enables steady playback with minimal latency.

Circular buffer with watermarks

    High/low watermarks detect when incoming data is too fast or too slow.

    Flow control messages (pause/resume) keep buffer within safe levels, preventing artifacts.

Stereo duplication for playback

    MAX98357 expects stereo frames; duplicating mono minimizes CPU work and keeps timing simple
