#include <Arduino.h>
#include "driver/i2s.h"

static const i2s_port_t i2s_num = I2S_NUM_0; // i2s port number

// how many samples to take per i2s_read()
const uint16_t sample_count = 1024;

// i2s constants, remember using both channels -> hence multiplying by num_channels == 2
const i2s_bits_per_sample_t i2s_bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
const uint8_t i2s_bytes_per_sample = i2s_bits_per_sample / 8;
const int num_channels = 2;
const uint16_t i2s_read_size_bytes = sample_count * i2s_bytes_per_sample * num_channels;

// i2s buffer variable
uint64_t sample_buffer[sample_count];

static const i2s_config_t i2s_config = {
    //master means esp32 generates clock signals, acts as both transmitter and receiver
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX |I2S_MODE_RX),
    .sample_rate = 44100, // standard CD quality sample rate
    // uses 32 bits to snapshot of one sample at one moment of time, 16 bits is standard for CD quality audio
    .bits_per_sample = i2s_bits_per_sample, //says each individual channel's sample is 32 bits wide
    
    //stereo two channel format (one 32-bit sample for left, one 32-bit for right, packed into 64 bit)
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,       // high interrupt priority
    
    // honestly don't know what these mean, might have to investigate these more
    .dma_buf_count = 8,                          
    .dma_buf_len = 256, //1024,                           
    .use_apll=0,
    .tx_desc_auto_clear= true, 
    .fixed_mclk=-1    
};

//pin configurations
static const i2s_pin_config_t pin_config = {
    .bck_io_num = 26,                                 // The bit clock connectiom, goes to pin 26 of ESP32
    .ws_io_num = 25,                                  // Word select, also known as word select or left right clock
    .data_out_num = 22,                               // Data out from the ESP32, connect to DIN on 38357A
    .data_in_num = 23                // we are not interested in I2S data into the ESP32
};

//QueueHandle_t i2s_queue = nullptr; // deals with event notifications -> optional debugging tool

void setup() {
  
  Serial.begin(115200);

  //check for if driver_install succeeded
  esp_err_t err = i2s_driver_install(i2s_num, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("i2s_driver_install failed: %s (%d)\n", esp_err_to_name(err), err);
  }
  i2s_set_pin(i2s_num, &pin_config);     
  Serial.println(i2s_read_size_bytes);

}

void loop() {
  
  //get data from INMP441 mic
  size_t BytesRead;
  //i2s_read only stops once the raw_sample array is full -> &sample_buffer gives pointer, and i2s_read_size_bytes tells the size
  i2s_read(i2s_num, sample_buffer, i2s_read_size_bytes, &BytesRead, portMAX_DELAY);


  //int32_t left_channel  = (int32_t)(raw_sample >> 32);         // upper 32 bits
  //int32_t right_channel = (int32_t)(raw_sample & 0xFFFFFFFF);  // lower 32 bits

  //process data with fft before outputting below...


  // write data to max98357 DAC/amp
  size_t BytesWritten;
  i2s_write(i2s_num, sample_buffer, i2s_read_size_bytes, &BytesWritten, portMAX_DELAY ); 
}