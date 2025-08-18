#include <WiFi.h>
#include <driver/i2s.h>
#include <ArduinoWebsockets.h>

using namespace websockets;

// Wi-Fi credentials
const char* ssid = "WiBelieveICanFi";
const char* password = "8209012896";
const char* websockets_server_host = "192.168.34.194";
const uint16_t websockets_server_port = 8765;

WebsocketsClient client;

// I2S pins
#define I2S_WS_MIC   25
#define I2S_SCK_MIC  33
#define I2S_SD_MIC   32
#define I2S_BCLK_SPK 27
#define I2S_LRC_SPK  26
#define I2S_DIN_SPK  14

#define SAMPLE_RATE   16000
#define SAMPLE_BITS   I2S_BITS_PER_SAMPLE_16BIT

bool recording = true;
bool websocket_connected = false;

// Reduced buffer size to save memory
const int BUFFER_SIZE = 2048;
uint8_t playback_buffer[BUFFER_SIZE];
volatile int write_pos = 0;
volatile int read_pos = 0;
volatile bool audio_ready = false;

int getBufferUsed() {
  return (write_pos >= read_pos) ? (write_pos - read_pos) : (BUFFER_SIZE - read_pos + write_pos);
}

int getBufferFree() {
  return BUFFER_SIZE - getBufferUsed() - 1;
}

void setupI2S() {
  // Microphone setup
  i2s_config_t i2s_config_mic = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = SAMPLE_BITS,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false
  };

  i2s_pin_config_t pin_config_mic = {
    .bck_io_num = I2S_SCK_MIC,
    .ws_io_num = I2S_WS_MIC,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD_MIC
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config_mic, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config_mic);
  i2s_zero_dma_buffer(I2S_NUM_0);

  // Speaker setup
  i2s_config_t i2s_config_spk = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = SAMPLE_BITS,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false
  };

  i2s_pin_config_t pin_config_spk = {
    .bck_io_num = I2S_BCLK_SPK,
    .ws_io_num = I2S_LRC_SPK,
    .data_out_num = I2S_DIN_SPK,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_1, &i2s_config_spk, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &pin_config_spk);
  i2s_zero_dma_buffer(I2S_NUM_1);
}

void connectWebSocket() {
  if (websocket_connected) return;
  
  String ws_url = "ws://" + String(websockets_server_host) + ":" + String(websockets_server_port);
  websocket_connected = client.connect(ws_url);
}

void handleWebSocketMessage(WebsocketsMessage message) {
  if (message.isBinary()) {
    const char* data = message.c_str();
    int data_len = message.length();
    
    if (getBufferFree() < data_len) {
      // Drop data if buffer full
      return;
    }
    
    // Store audio data
    for (int i = 0; i < data_len; i++) {
      playback_buffer[write_pos] = data[i];
      write_pos = (write_pos + 1) % BUFFER_SIZE;
    }
    audio_ready = true;
  }
}

void playAudio() {
  if (!audio_ready || getBufferUsed() < 32) return;
  
  uint32_t speaker_buffer[16];
  int samples = 0;
  
  // Convert mono to stereo
  while (samples < 16 && getBufferUsed() >= 2) {
    uint8_t b1 = playback_buffer[read_pos];
    uint8_t b2 = playback_buffer[(read_pos + 1) % BUFFER_SIZE];
    uint16_t mono_sample = b1 | (b2 << 8);
    
    speaker_buffer[samples] = ((uint32_t)mono_sample << 16) | mono_sample;
    
    read_pos = (read_pos + 2) % BUFFER_SIZE;
    samples++;
  }
  
  if (samples > 0) {
    size_t bytes_written;
    i2s_write(I2S_NUM_1, speaker_buffer, samples * sizeof(uint32_t), &bytes_written, 10);
  }
  
  if (getBufferUsed() == 0) {
    audio_ready = false;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("ESP32 Audio System Starting...");
  
  // Connect WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected: " + WiFi.localIP().toString());
  
  setupI2S();
  
  client.onEvent([](WebsocketsEvent event, String data) {
    if (event == WebsocketsEvent::ConnectionOpened) {
      websocket_connected = true;
      Serial.println("WebSocket Connected");
    } else if (event == WebsocketsEvent::ConnectionClosed) {
      websocket_connected = false;
      Serial.println("WebSocket Disconnected");
    }
  });
  
  client.onMessage(handleWebSocketMessage);
  connectWebSocket();
  
  Serial.println("System Ready. Send '1' to stop recording and play back");
}

void loop() {
  // Handle WebSocket
  if (websocket_connected) {
    client.poll();
  } else {
    static unsigned long lastReconnect = 0;
    if (millis() - lastReconnect > 5000) {
      connectWebSocket();
      lastReconnect = millis();
    }
  }
  
  // Handle serial commands
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == '1' && websocket_connected) {
      recording = false;
      client.send("STOP_RECORD");
      delay(100);
      client.send("REQUEST_PLAYBACK");
      Serial.println("Playback requested");
    } else if (cmd == '2') {
      recording = true;
      write_pos = 0;
      read_pos = 0;
      audio_ready = false;
      Serial.println("Recording restarted");
    }
  }
  
  // Always try to play audio
  playAudio();
  
  if (!recording) {
    delay(1);
    return;
  }
  
  // Record audio
  if (websocket_connected) {
    uint16_t i2s_buffer[128];
    size_t bytes_read;
    
    esp_err_t result = i2s_read(I2S_NUM_0, i2s_buffer, sizeof(i2s_buffer), &bytes_read, 10);
    
    if (result == ESP_OK && bytes_read > 0) {
      client.sendBinary((const char*)i2s_buffer, bytes_read);
    }
  }
}