#include <WiFi.h>
#include <driver/i2s.h>
#include <ArduinoWebsockets.h>

using namespace websockets;

const char* ssid = "WiBelieveICanFi";
const char* password = "8209012896";

const char* websockets_server_host = "192.168.34.194"; // Flask server IP
const uint16_t websockets_server_port = 5000;

WebsocketsClient client;

// I2S pins
#define I2S_WS      25
#define I2S_SCK     32
#define I2S_SD      33

#define SAMPLE_RATE 16000
#define SAMPLE_BITS I2S_BITS_PER_SAMPLE_16BIT
#define CHANNEL_FORMAT I2S_CHANNEL_FMT_ONLY_LEFT
#define I2S_NUM I2S_NUM_0

bool recording = true;

void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("WiFi connected");

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = SAMPLE_BITS,
    .channel_format = CHANNEL_FORMAT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
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
  i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM);

  String ws_url = String("ws://") + websockets_server_host + ":" + String(websockets_server_port);
  client.connect(ws_url);
}

void loop() {
  // Check Serial input for stop command
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == '1') {
      recording = false;
      client.send("STOP_RECORD"); // Tell server to finalize file
      Serial.println("Recording stopped, telling server to compile/play file");
    }
  }

  if (!recording) {
    delay(100);
    return; // Skip audio capture
  }

  // Capture and send audio
  const int buffer_samples = 256;
  uint16_t i2s_buffer[buffer_samples];
  size_t bytes_read = 0;
  i2s_read(I2S_NUM, &i2s_buffer, buffer_samples * sizeof(uint16_t), &bytes_read, portMAX_DELAY);

  // DEBUG: Show one sample every loop to confirm mic is reading
  static uint32_t debugCounter = 0;
  if (debugCounter++ % 50 == 0) {  // every ~50 reads
      Serial.print("Sample[0]: ");
      Serial.println(i2s_buffer[0]);  // raw PCM value
  }

  client.sendBinary((const char*)i2s_buffer, bytes_read);
}
