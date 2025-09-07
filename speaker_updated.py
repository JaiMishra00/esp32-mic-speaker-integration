#!/usr/bin/env python3

import asyncio
import websockets
import json
import wave
import os
import time
from datetime import datetime
from http.server import HTTPServer, SimpleHTTPRequestHandler
import threading
import struct

class AudioServer:
    def __init__(self, websocket_port=8765, http_port=8000):
        self.websocket_port = websocket_port
        self.http_port = http_port
        self.audio_files = {}  # Store audio files with timestamps
        self.clients = set()
        
        # Create audio directory
        os.makedirs("audio_files", exist_ok=True)
        
    async def register_client(self, websocket):
        """Register a new client"""
        self.clients.add(websocket)
        print(f"Client connected. Total clients: {len(self.clients)}")
        
    async def unregister_client(self, websocket):
        """Unregister a client"""
        self.clients.discard(websocket)
        print(f"Client disconnected. Total clients: {len(self.clients)}")
        
    def save_audio_file(self, audio_data, metadata):
        """Save audio data as WAV file"""
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"audio_{timestamp}.wav"
        filepath = os.path.join("audio_files", filename)
        
        sample_rate = metadata.get('sample_rate', 16000)
        bits_per_sample = metadata.get('bits_per_sample', 16)
        channels = metadata.get('channels', 1)
        
        print(f"Saving audio: {filename}")
        print(f"Sample rate: {sample_rate}Hz, Bits: {bits_per_sample}, Channels: {channels}")
        print(f"Data size: {len(audio_data)} bytes")
        
        try:
            with wave.open(filepath, 'wb') as wav_file:
                wav_file.setnchannels(channels)
                wav_file.setsampwidth(bits_per_sample // 8)
                wav_file.setframerate(sample_rate)
                wav_file.writeframes(audio_data)
                
            self.audio_files[filename] = {
                'filepath': filepath,
                'timestamp': timestamp,
                'metadata': metadata,
                'size': len(audio_data)
            }
            
            print(f"Audio saved successfully: {filepath}")
            return filename
            
        except Exception as e:
            print(f"Error saving audio file: {e}")
            return None
    
    def load_audio_file(self, filename):
        """Load audio file and return raw data"""
        filepath = os.path.join("audio_files", filename)
        
        if not os.path.exists(filepath):
            print(f"Audio file not found: {filepath}")
            return None
            
        try:
            with wave.open(filepath, 'rb') as wav_file:
                frames = wav_file.readframes(wav_file.getnframes())
                return frames
        except Exception as e:
            print(f"Error loading audio file: {e}")
            return None
    
    async def handle_message(self, websocket, message):
        """Handle incoming WebSocket messages"""
        try:
            # Try to parse as JSON first
            data = json.loads(message)
            message_type = data.get('type')
            
            if message_type == 'start_recording':
                # Initialize streaming recording
                timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                filename = f"audio_{timestamp}.wav"
                filepath = os.path.join("audio_files", filename)
                
                # Store recording info
                self.active_recordings[websocket] = {
                    'filename': filename,
                    'filepath': filepath,
                    'metadata': data,
                    'audio_data': bytearray(),
                    'start_time': time.time()
                }
                
                print(f"Started streaming recording: {filename}")
                print(f"Sample rate: {data.get('sample_rate')}Hz")
                
            elif message_type == 'end_recording':
                # Finish recording and save file
                if websocket in self.active_recordings:
                    recording = self.active_recordings[websocket]
                    
                    # Save the accumulated audio data
                    self.save_streaming_audio(recording)
                    
                    # Store in audio files registry
                    self.audio_files[recording['filename']] = {
                        'filepath': recording['filepath'],
                        'timestamp': recording['filename'].split('_')[1:],
                        'metadata': recording['metadata'],
                        'size': len(recording['audio_data']),
                        'duration': data.get('duration', 0)
                    }
                    
                    websocket.last_audio_file = recording['filename']
                    
                    # Create download link
                    download_link = f"http://localhost:{self.http_port}/audio_files/{recording['filename']}"
                    
                    # Send completion message
                    response = {
                        "type": "recording_complete",
                        "filename": recording['filename'],
                        "size": len(recording['audio_data']),
                        "duration": data.get('duration', 0)
                    }
                    await websocket.send(json.dumps(response))
                    
                    # Send download link
                    link_response = {
                        "type": "download_link",
                        "link": download_link
                    }
                    await websocket.send(json.dumps(link_response))
                    
                    print(f"Recording completed: {recording['filename']}")
                    print(f"Duration: {data.get('duration', 0):.2f}s, Size: {len(recording['audio_data'])} bytes")
                    print(f"Download: {download_link}")
                    
                    # Clean up
                    del self.active_recordings[websocket]
                    
            elif message_type == 'download_audio':
                # Send the most recent audio file back (chunked for large files)
                if hasattr(websocket, 'last_audio_file') and websocket.last_audio_file:
                    await self.send_audio_chunked(websocket, websocket.last_audio_file)
                else:
                    await websocket.send(json.dumps({
                        "type": "error", 
                        "message": "No audio file available"
                    }))
                    
        except json.JSONDecodeError:
            # This is binary audio data (streaming chunks)
            if websocket in self.active_recordings:
                recording = self.active_recordings[websocket]
                recording['audio_data'].extend(message)
                
                # Send acknowledgment
                response = {
                    "type": "chunk_received",
                    "size": len(message),
                    "total_size": len(recording['audio_data'])
                }
                await websocket.send(json.dumps(response))
                
                # Show progress
                duration = len(recording['audio_data']) / (
                    recording['metadata']['sample_rate'] * 
                    recording['metadata']['channels'] * 
                    (recording['metadata']['bits_per_sample'] // 8)
                )
                print(f"Received chunk: {len(message)} bytes, Total: {duration:.1f}s", end='\r')
            else:
                print("Received audio data without active recording session")
    
    def save_streaming_audio(self, recording):
        """Save streaming audio data as WAV file"""
        try:
            metadata = recording['metadata']
            sample_rate = metadata.get('sample_rate', 16000)
            bits_per_sample = metadata.get('bits_per_sample', 16)
            channels = metadata.get('channels', 1)
            
            with wave.open(recording['filepath'], 'wb') as wav_file:
                wav_file.setnchannels(channels)
                wav_file.setsampwidth(bits_per_sample // 8)
                wav_file.setframerate(sample_rate)
                wav_file.writeframes(bytes(recording['audio_data']))
                
            print(f"\nStreaming audio saved: {recording['filepath']}")
            
        except Exception as e:
            print(f"Error saving streaming audio: {e}")
    
    async def send_audio_chunked(self, websocket, filename):
        """Send audio file back in chunks to handle large files"""
        filepath = os.path.join("audio_files", filename)
        
        if not os.path.exists(filepath):
            await websocket.send(json.dumps({
                "type": "error",
                "message": "Audio file not found"
            }))
            return
        
        try:
            # Send playback start notification
            await websocket.send(json.dumps({"type": "playback_start"}))
            
            with open(filepath, 'rb') as f:
                # Skip WAV header (44 bytes) to send raw audio data
                f.seek(44)
                
                chunk_size = 4096  # 4KB chunks
                while True:
                    chunk = f.read(chunk_size)
                    if not chunk:
                        break
                    await websocket.send(chunk)
                    await asyncio.sleep(0.01)  # Small delay to prevent overwhelming
                    
            print(f"Sent audio file back to client: {filename}")
            
        except Exception as e:
            print(f"Error sending audio file: {e}")
            await websocket.send(json.dumps({
                "type": "error",
                "message": f"Error sending audio: {e}"
            }))
                
    async def websocket_handler(self, websocket, path):
        """Handle WebSocket connections"""
        await self.register_client(websocket)
        try:
            async for message in websocket:
                await self.handle_message(websocket, message)
        except websockets.exceptions.ConnectionClosed:
            print("Client connection closed")
        except Exception as e:
            print(f"Error in websocket handler: {e}")
        finally:
            await self.unregister_client(websocket)
    
    def start_http_server(self):
        """Start HTTP server for file downloads"""
        class CustomHandler(SimpleHTTPRequestHandler):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, directory=os.getcwd(), **kwargs)
                
        httpd = HTTPServer(('localhost', self.http_port), CustomHandler)
        print(f"HTTP server started on port {self.http_port}")
        httpd.serve_forever()
    
    def get_server_ip(self):
        """Get server IP address"""
        import socket
        try:
            # Connect to Google DNS to get local IP
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                s.connect(('8.8.8.8', 80))
                return s.getsockname()[0]
        except:
            return 'localhost'
    
    async def start_websocket_server(self):
        """Start WebSocket server"""
        server_ip = self.get_server_ip()
        print(f"WebSocket server starting on {server_ip}:{self.websocket_port}")
        print(f"ESP32 should connect to: ws://{server_ip}:{self.websocket_port}")
        
        server = await websockets.serve(
            self.websocket_handler, 
            '0.0.0.0',  # Listen on all interfaces
            self.websocket_port
        )
        
        print("WebSocket server started successfully!")
        print(f"Audio files will be saved in: {os.path.abspath('audio_files')}")
        print("\nWaiting for ESP32 connections...")
        
        await server.wait_closed()
    
    def run(self):
        """Run both HTTP and WebSocket servers"""
        # Start HTTP server in a separate thread
        http_thread = threading.Thread(target=self.start_http_server, daemon=True)
        http_thread.start()
        
        # Run WebSocket server
        try:
            asyncio.run(self.start_websocket_server())
        except KeyboardInterrupt:
            print("\nShutting down servers...")
        except Exception as e:
            print(f"Server error: {e}")

def main():
    print("=== ESP32 Audio WebSocket Server ===")
    print("This server receives audio from ESP32, saves it, and sends it back")
    print("Press Ctrl+C to stop\n")
    
    server = AudioServer(websocket_port=8765, http_port=8000)
    server.run()

if __name__ == "__main__":
    main()
