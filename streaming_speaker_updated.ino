#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include <driver/i2s.h>
#include <ArduinoJson.h>

using namespace websockets;

// WiFi credentials - REPLACE WITH YOUR NETWORK
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// WebSocket server - REPLACE WITH YOUR SERVER IP
const char* websocket_server = "ws://192.168.1.100:8765";  // Change this IP

WebsocketsClient client;

// I2S pins for INMP441 (MIC)
#define I2S_WS_MIC    25    // LRCL/WS
#define I2S_SCK_MIC   26    // BCLK/SCK  
#define I2S_SD_MIC    22    // DOUT/SD

// I2S pins for MAX98357A (Speaker)
#define I2S_WS_SPK    27    // LRCL/WS
#define I2S_SCK_SPK   14    // BCLK/SCK
#define I2S_SD_SPK    12    // DIN/SD

// Audio configuration
#define SAMPLE_RATE   16000
#define SAMPLE_BITS   16
#define CHANNELS      1
#define BUFFER_SIZE   1024
#define MAX_AUDIO_BUFFER 64000  // ~4 seconds at 16kHz

// Audio buffers
int16_t micBuffer[BUFFER_SIZE];
uint8_t audioData[MAX_AUDIO_BUFFER];
uint32_t audioDataSize = 0;
bool isRecording = false;
bool isPlaying = false;

// States
enum SystemState {
  IDLE,
  RECORDING,
  UPLOADING,
  DOWNLOADING,
  PLAYING
};

SystemState currentState = IDLE;

void setupI2SMic() {
  i2s_config_t i2s_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = i2s_bits_per_sample_t(SAMPLE_BITS),
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_SIZE,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK_MIC,
    .ws_io_num = I2S_WS_MIC,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD_MIC
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

void setupI2SSpeaker() {
  i2s_config_t i2s_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = i2s_bits_per_sample_t(SAMPLE_BITS),
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_SIZE,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK_SPK,
    .ws_io_num = I2S_WS_SPK,
    .data_out_num = I2S_SD_SPK,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_1, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM_1);
}

void onMessageCallback(WebsocketsMessage message) {
  if (message.isText()) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, message.data());
    
    String type = doc["type"];
    
    if (type == "audio_ready") {
      Serial.println("Audio processed on server, downloading...");
      currentState = DOWNLOADING;
      client.send("{\"type\":\"download_audio\"}");
    }
    else if (type == "download_link") {
      String link = doc["link"];
      Serial.println("Download link: " + link);
    }
  } else if (message.isBinary()) {
    // Receiving audio data back from server
    Serial.println("Receiving audio data from server...");
    
    size_t dataSize = message.length();
    if (dataSize <= MAX_AUDIO_BUFFER) {
      memcpy(audioData, message.c_str(), dataSize);
      audioDataSize = dataSize;
      currentState = PLAYING;
      Serial.println("Starting playback...");
    } else {
      Serial.println("Error: Audio data too large");
    }
  }
}

void onEventsCallback(WebsocketsEvent event, String data) {
  if (event == WebsocketsEvent::ConnectionOpened) {
    Serial.println("WebSocket Connected!");
  } else if (event == WebsocketsEvent::ConnectionClosed) {
    Serial.println("WebSocket Disconnected!");
  } else if (event == WebsocketsEvent::GotPing) {
    Serial.println("Got ping from server!");
  } else if (event == WebsocketsEvent::GotPong) {
    Serial.println("Got pong from server!");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Audio System Starting...");

  // Setup WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("WiFi Connected!");
  Serial.println("IP Address: " + WiFi.localIP().toString());

  // Setup I2S
  setupI2SMic();
  setupI2SSpeaker();

  // Setup WebSocket
  client.onMessage(onMessageCallback);
  client.onEvent(onEventsCallback);
  
  Serial.println("Connecting to WebSocket server...");
  client.connect(websocket_server);

  Serial.println("\nCommands:");
  Serial.println("'start' - Start streaming recording (unlimited time)");
  Serial.println("'stop' - Stop recording and send to server");
  Serial.println("'status' - Show current status");
  Serial.println("System ready! Recording can now be much longer than 4 seconds!");
}

void startRecording() {
  if (currentState != IDLE) return;
  
  Serial.println("Starting recording...");
  isRecording = true;
  currentState = RECORDING;
  audioDataSize = 0;
  
  // Clear audio buffer
  memset(audioData, 0, MAX_AUDIO_BUFFER);
}

void stopRecording() {
  if (currentState != RECORDING) return;
  
  Serial.println("Stopping recording...");
  isRecording = false;
  currentState = UPLOADING;
  
  if (audioDataSize > 0) {
    Serial.printf("Sending %d bytes of audio to server...\n", audioDataSize);
    
    // Send metadata first
    DynamicJsonDocument doc(512);
    doc["type"] = "audio_upload";
    doc["sample_rate"] = SAMPLE_RATE;
    doc["bits_per_sample"] = SAMPLE_BITS;
    doc["channels"] = CHANNELS;
    doc["size"] = audioDataSize;
    
    String metadata;
    serializeJson(doc, metadata);
    client.send(metadata);
    
    // Send binary audio data
    client.sendBinary((const char*)audioData, audioDataSize);
    
    Serial.println("Audio data sent!");
  } else {
    Serial.println("No audio data to send");
    currentState = IDLE;
  }
}

void recordAudio() {
  if (!isRecording) return;
  
  size_t bytesRead;
  esp_err_t result = i2s_read(I2S_NUM_0, micBuffer, sizeof(micBuffer), &bytesRead, 100);
  
  if (result == ESP_OK && bytesRead > 0) {
    // Check if we have space in buffer
    if (audioDataSize + bytesRead <= MAX_AUDIO_BUFFER) {
      memcpy(audioData + audioDataSize, micBuffer, bytesRead);
      audioDataSize += bytesRead;
    } else {
      Serial.println("Audio buffer full!");
      stopRecording();
    }
  }
}

void playAudio() {
  if (currentState != PLAYING || audioDataSize == 0) return;
  
  size_t bytesWritten;
  size_t bytesToWrite = min((size_t)BUFFER_SIZE * 2, audioDataSize);
  
  esp_err_t result = i2s_write(I2S_NUM_1, audioData, bytesToWrite, &bytesWritten, 100);
  
  if (result == ESP_OK && bytesWritten > 0) {
    // Shift remaining data
    audioDataSize -= bytesWritten;
    if (audioDataSize > 0) {
      memmove(audioData, audioData + bytesWritten, audioDataSize);
    } else {
      Serial.println("Playback finished!");
      currentState = IDLE;
    }
  }
}

void handleSerialCommands() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toLowerCase();
    
    if (command == "start") {
      startRecording();
    }
    else if (command == "stop") {
      stopRecording();
    }
    else if (command == "status") {
      Serial.printf("State: %d, Buffer size: %d bytes\n", currentState, audioDataSize);
    }
    else {
      Serial.println("Unknown command. Use 'start' or 'stop'");
    }
  }
}

void loop() {
  client.poll();
  
  handleSerialCommands();
  recordAudio();
  playAudio();
  
  delay(1); // Small delay for stability
}
