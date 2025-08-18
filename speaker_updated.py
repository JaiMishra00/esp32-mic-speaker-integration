from flask import Flask, render_template, send_file, request
import asyncio
import websockets
import wave
import threading
import os

app = Flask(__name__)

SAMPLE_RATE = 16000
CHANNELS = 1
SAMPLE_WIDTH = 2

audio_buffer = bytearray()
connected_clients = set()

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
    try:
        while True:
            await websocket.ping()
            await asyncio.sleep(20)
    except:
        pass

async def send_audio_for_playback(websocket, filename):
    """Send the recorded audio back to ESP32 for playback with flow control"""
    try:
        if not os.path.exists(filename):
            print("[ERROR] No recording file found for playback")
            return
            
        with open(filename, 'rb') as f:
            # Skip WAV header (44 bytes) to get raw PCM data
            f.seek(44)
            audio_data = f.read()
            
        print(f"[DEBUG] Starting playback of {len(audio_data)} bytes")
        
        # Stream audio in chunks with flow control
        chunk_size = 512  # Match ESP32 buffer expectations
        total_chunks = (len(audio_data) + chunk_size - 1) // chunk_size
        chunks_sent = 0
        paused = False
        
        for i in range(0, len(audio_data), chunk_size):
            # Wait if streaming is paused
            while paused:
                await asyncio.sleep(0.01)
                
            chunk = audio_data[i:i+chunk_size]
            
            try:
                await websocket.send(chunk)
                chunks_sent += 1
                
                # Debug output every 10 chunks
                if chunks_sent % 10 == 0:
                    progress = (chunks_sent / total_chunks) * 100
                    print(f"[DEBUG] Playback progress: {chunks_sent}/{total_chunks} chunks ({progress:.1f}%)")
                
                # Small delay to prevent overwhelming the ESP32
                await asyncio.sleep(0.02)  # 20ms delay between chunks
                
            except websockets.exceptions.ConnectionClosed:
                print("[DEBUG] WebSocket closed during playback")
                break
            except Exception as e:
                print(f"[ERROR] Failed to send chunk {chunks_sent}: {e}")
                break
                
        print(f"[DEBUG] Playback complete - sent {chunks_sent} chunks")
        
        # Store flow control state for this connection
        websocket.flow_control_paused = paused
        
    except Exception as e:
        print(f"[ERROR] Failed to send playback audio: {e}")

async def handle_websocket(websocket):
    global audio_buffer
    print(f"[DEBUG] WebSocket connected from {websocket.remote_address}")
    
    connected_clients.add(websocket)
    
    # Initialize flow control state
    websocket.flow_control_paused = False
    websocket.playback_task = None
    
    # Start ping loop
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
                            print(f"Download link: http://localhost:5000/download\n")
                        except Exception as e:
                            print(f"[ERROR] Failed to save WAV file: {e}")
                    else:
                        print("[DEBUG] No audio data was received before stopping.")
                    
                    # Reset buffer for next recording
                    audio_buffer = bytearray()
                    
                elif message == "REQUEST_PLAYBACK":
                    print("[DEBUG] Playback requested, starting streaming...")
                    # Cancel any existing playback task
                    if websocket.playback_task and not websocket.playback_task.done():
                        websocket.playback_task.cancel()
                    
                    # Start new playback task
                    websocket.playback_task = asyncio.create_task(
                        send_audio_for_playback(websocket, "recording.wav")
                    )
                    
                elif message == "PAUSE_STREAM":
                    print("[DEBUG] Stream pause requested by ESP32")
                    websocket.flow_control_paused = True
                    
                elif message == "RESUME_STREAM":
                    print("[DEBUG] Stream resume requested by ESP32")
                    websocket.flow_control_paused = False
                    
            else:
                # Binary audio data from microphone
                audio_buffer.extend(message)
                if len(audio_buffer) % 10240 == 0:  # Debug every ~10KB
                    print(f"[DEBUG] Recording progress: {len(audio_buffer)} bytes")

    except websockets.exceptions.ConnectionClosed:
        print("[DEBUG] WebSocket connection closed")
    except Exception as e:
        print(f"[ERROR] WebSocket error: {e}")
    finally:
        # Cancel any running playback task
        if hasattr(websocket, 'playback_task') and websocket.playback_task and not websocket.playback_task.done():
            websocket.playback_task.cancel()
        connected_clients.discard(websocket)

async def websocket_server_main():
    print("[DEBUG] Starting WebSocket server on port 8765...")
    async with websockets.serve(handle_websocket, "0.0.0.0", 8765):
        await asyncio.Future()

def start_websocket_server():
    asyncio.run(websocket_server_main())

if __name__ == '__main__':
    ws_thread = threading.Thread(target=start_websocket_server, daemon=True)
    ws_thread.start()
    print("[DEBUG] Starting Flask HTTP server on port 5000...")
    app.run(host='0.0.0.0', port=5000, debug=False)