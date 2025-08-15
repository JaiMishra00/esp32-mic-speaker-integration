from flask import Flask, render_template, send_file, request
import asyncio
import websockets
import wave
import threading
import os

app = Flask(__name__)

SAMPLE_RATE = 16000
CHANNELS = 1
SAMPLE_WIDTH = 2  # bytes (16-bit)
audio_buffer = bytearray()

@app.route('/')
def index():
    print("[DEBUG] Received GET / request from", request.remote_addr)
    return render_template('index1.html')

@app.route('/download')
def download():
    print("[DEBUG] /download accessed")
    if not os.path.exists("recording.wav") or os.path.getsize("recording.wav") == 0:
        return "No recording file found.", 404
    return send_file("recording.wav", as_attachment=True)

async def ping_loop(websocket):
    """Send periodic pings to keep connection alive."""
    try:
        while True:
            await websocket.ping()
            await asyncio.sleep(20)  # every 20 seconds
    except:
        pass

async def handle_websocket(websocket):
    global audio_buffer
    print(f"[DEBUG] WebSocket connected from {websocket.remote_address}")

    # Start keep-alive ping loop
    asyncio.create_task(ping_loop(websocket))

    try:
        async for message in websocket:
            if isinstance(message, str):
                print(f"[DEBUG] Received text message: {message}")
                if message == "STOP_RECORD":
                    if audio_buffer:
                        try:
                            with wave.open("recording.wav", 'wb') as wf:
                                wf.setnchannels(CHANNELS)
                                wf.setsampwidth(SAMPLE_WIDTH)
                                wf.setframerate(SAMPLE_RATE)
                                wf.writeframes(audio_buffer)
                            print(f"[DEBUG] Saved recording.wav ({len(audio_buffer)} bytes)")
                            print("\n==== AUDIO FILE READY ====")
                            print(f"Download link: http://192.168.34.194:5000/download\n")
                        except Exception as e:
                            print("[ERROR] Failed to save WAV file:", e)
                    else:
                        print("[DEBUG] No audio data was received before stopping.")
                    audio_buffer = bytearray()
            else:
                audio_buffer.extend(message)
                print(f"[DEBUG] Received audio chunk: {len(message)} bytes, total: {len(audio_buffer)} bytes")

    except websockets.exceptions.ConnectionClosed:
        print("[DEBUG] WebSocket connection closed")
    except Exception as e:
        print(f"[ERROR] WebSocket error: {e}")

async def websocket_server_main():
    print("[DEBUG] Starting WebSocket server on port 8765...")
    async with websockets.serve(handle_websocket, "0.0.0.0", 8765):
        await asyncio.Future()  # run forever

def start_websocket_server():
    asyncio.run(websocket_server_main())

if __name__ == '__main__':
    # Start WebSocket server in a separate thread
    ws_thread = threading.Thread(target=start_websocket_server, daemon=True)
    ws_thread.start()

    print("[DEBUG] Starting Flask HTTP server on port 5000...")
    app.run(host='0.0.0.0', port=5000, debug=False)
