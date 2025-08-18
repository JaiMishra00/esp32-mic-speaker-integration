#include <driver/i2s.h>
#include <math.h>

// Pin connections to MAX98357A
#define I2S_DOUT 14 // DIN pin on MAX98357A
#define I2S_BCLK 27 // BCLK pin on MAX98357A  
#define I2S_LRC 26  // LRC pin on MAX98357A

#define I2S_NUM I2S_NUM_0

const int sampleRate = 44100;
const int amplitude = 30000; // Max amplitude for 16-bit audio
const float testFreq = 440.0; // A4 note frequency

void setup() {
  Serial.begin(115200);
  Serial.println("MAX98357A Speaker Test Starting...");
  
  setupI2S();
  
  // Test sequence
  testSpeakerBasic();
  delay(1000);
  testSpeakerSweep();
  
  Serial.println("All tests complete!");
}

void loop() {
  // Empty - tests run once
}

void setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = sampleRate,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,  // FIXED: Use single format
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM, &pin_config);
  i2s_set_clk(I2S_NUM, sampleRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
}

void testSpeakerBasic() {
  Serial.println("Test 1: Playing 440Hz tone for 3 seconds at max amplitude");
  playTone(testFreq, 3000);
  Serial.println("Basic tone test complete");
}

void testSpeakerSweep() {
  Serial.println("Test 2: Frequency sweep 200Hz to 2kHz");
  
  float startFreq = 200.0;
  float endFreq = 2000.0;
  int sweepDuration = 5000; // 5 seconds
  int steps = 50;
  
  for (int i = 0; i < steps; i++) {
    float freq = startFreq + (endFreq - startFreq) * i / steps;
    Serial.print("Playing: ");
    Serial.print(freq);
    Serial.println("Hz");
    playTone(freq, sweepDuration / steps);
  }
  
  Serial.println("Frequency sweep complete");
}

void playTone(float freq, int duration_ms) {
  int samples_count = (sampleRate * duration_ms) / 1000;
  int16_t buffer[512];
  size_t bytes_written;

  for (int i = 0; i < samples_count; ) {
    int to_write = (samples_count - i) < 512 ? (samples_count - i) : 512;

    for (int j = 0; j < to_write; j++) {
      float sample = sin(2 * PI * freq * (i + j) / sampleRate);
      buffer[j] = (int16_t)(sample * amplitude);
    }

    i2s_write(I2S_NUM, (const char *)buffer, to_write * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    i += to_write;
  }
}
