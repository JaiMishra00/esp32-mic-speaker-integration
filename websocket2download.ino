#include <WiFi.h>
#include <driver/i2s.h>
#include <ArduinoWebsockets.h>

using namespace websockets;

// Wi-Fi credentials
const char* ssid = "WiBelieveICanFi";
const char* password = "8209012896";

// Flask server address
const char* websockets_server_host = "192.168.34.194"; // Flask server IP
const uint16_t websockets_server_port = 8765;  // FIXED: WebSocket port, not Flask port

WebsocketsClient client;

// INMP441 -> ESP32 pin mapping (adjust to your wiring)
#define I2S_WS   25   // LRCLK / WS
#define I2S_SCK  33   // BCLK / SCK
#define I2S_SD   32   // DOUT / SD

// Audio format
#define SAMPLE_RATE   16000
#define SAMPLE_BITS   I2S_BITS_PER_SAMPLE_16BIT
#define CHANNEL_FMT   I2S_CHANNEL_FMT_ONLY_LEFT
#define I2S_NUM       I2S_NUM_0

bool recording = true;
bool websocket_connected = false;
unsigned long lastConnectionAttempt = 0;
unsigned long lastHeartbeat = 0;

static void connectWebSocket() {
  if (websocket_connected) {
    return; // Already connected
  }
  
  String ws_url = String("ws://") + websockets_server_host + ":" + String(websockets_server_port);
  Serial.printf("[WS] Attempting connection to: %s\n", ws_url.c_str());
  
  if (client.connect(ws_url)) {
    Serial.println("[WS] Connected successfully");
    websocket_connected = true;
  } else {
    Serial.println("[WS] Connect failed");
    websocket_connected = false;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);  // Give serial time to initialize

  // Wi-Fi
  Serial.printf("[WiFi] Connecting to %s\n", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());

  // I2S config
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = SAMPLE_BITS,
    .channel_format = CHANNEL_FMT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 512, // samples per DMA buffer
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };

  // Install and start I2S
  if (i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL) != ESP_OK) {
    Serial.println("[I2S] Driver install failed");
  }
  if (i2s_set_pin(I2S_NUM, &pin_config) != ESP_OK) {
    Serial.println("[I2S] Set pin failed");
  }
  i2s_zero_dma_buffer(I2S_NUM);

  // Explicitly set sample rate after install
  i2s_set_clk(I2S_NUM, SAMPLE_RATE, SAMPLE_BITS, I2S_CHANNEL_MONO);

  // WebSocket event handlers
  client.onEvent([](WebsocketsEvent event, String data) {
    switch (event) {
      case WebsocketsEvent::ConnectionOpened:
        Serial.println("[WS] Connection opened");
        websocket_connected = true;
        break;
      case WebsocketsEvent::ConnectionClosed:
        Serial.println("[WS] Connection closed");
        websocket_connected = false;
        break;
      case WebsocketsEvent::GotPing:
        Serial.println("[WS] Got Ping");
        break;
      case WebsocketsEvent::GotPong:
        Serial.println("[WS] Got Pong");
        break;
      default:
        break;
    }
  });

  // Initial connection
  connectWebSocket();
  
  Serial.println("[INFO] Setup complete. Send '1' to stop recording.");
  Serial.println("[INFO] Recording started...");
}

void loop() {
  unsigned long currentTime = millis();

  // Handle WebSocket connection management
  if (websocket_connected && client.available()) {
    client.poll();
  } else if (!websocket_connected) {
    // Try to reconnect every 10 seconds (less aggressive)
    if (currentTime - lastConnectionAttempt > 10000) {
      Serial.println("[WS] Attempting reconnection...");
      connectWebSocket();
      lastConnectionAttempt = currentTime;
    }
  }

  // Send periodic heartbeat when connected but not recording
  if (websocket_connected && !recording && (currentTime - lastHeartbeat > 30000)) {
    client.ping();
    lastHeartbeat = currentTime;
  }

  // Stop command from Serial Monitor
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == '1') {
      recording = false;
      if (websocket_connected) {
        bool ok = client.send("STOP_RECORD");
        Serial.println("[INFO] Recording stopped, telling server to save file");
        Serial.printf("[WS] STOP_RECORD sent: %s\n", ok ? "OK" : "FAIL");
      } else {
        Serial.println("[ERROR] Cannot send STOP_RECORD - WebSocket not connected");
      }
    } else if (cmd == '2') {
      // Restart recording
      recording = true;
      Serial.println("[INFO] Recording restarted");
    }
  }

  if (!recording) {
    delay(100);
    return;
  }

  // Skip audio capture if WebSocket not connected
  if (!websocket_connected) {
    delay(100);
    return;
  }

  // Capture and send audio
  const int buffer_samples = 256;
  uint16_t i2s_buffer[buffer_samples];
  size_t bytes_read = 0;

  // Non-blocking read with timeout
  esp_err_t res = i2s_read(I2S_NUM, (void*)i2s_buffer, buffer_samples * sizeof(uint16_t), &bytes_read, 100);
  if (res != ESP_OK) {
    Serial.printf("[I2S] Read error: %d\n", (int)res);
    delay(10);
    return;
  }

  if (bytes_read == 0) {
    delay(10);
    return;
  }

  // Debug: periodic sample check (less frequent)
  static uint32_t dbg = 0;
  if ((dbg++ % 200) == 0) {  // Less frequent debug output
    Serial.printf("[DEBUG] Sample[0]: %d, bytes_read: %d\n", i2s_buffer[0], bytes_read);
  }

  // Send raw PCM bytes as a binary WebSocket frame
  bool ok = client.sendBinary((const char*)i2s_buffer, bytes_read);
  if (!ok) {
    Serial.println("[WS] sendBinary failed - connection may be lost");
    websocket_connected = false;
  }

  // Small delay to prevent overwhelming the connection
  delay(1);  // Small delay to reduce data rate slightly
}