#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include "driver/i2s.h"
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "kws_model_data.h" 

const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
const char* WEBSOCKET_URL = "ws://192.168.1.100:8000/ws/audio";

using namespace websockets;
WebsocketsClient wsClient;

uint8_t tensor_arena[40 * 1024]; 
uint8_t circular_buffer[32000];   
size_t buffer_head = 0;

tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input_tensor = nullptr;
TfLiteTensor* output_tensor = nullptr;

void update_buffer(uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        circular_buffer[buffer_head] = data[i];
        buffer_head = (buffer_head + 1) % 32000;
    }
}

void send_audio_to_cloud() {
    if (!wsClient.connect(WEBSOCKET_URL)) return;

    wsClient.sendBinary((char*)circular_buffer, 32000);

    int16_t audio_chunk[512];
    size_t bytes_read = 0;
    unsigned long start = millis();

    while (millis() - start < 3000) {
        i2s_read(I2S_NUM_0, audio_chunk, sizeof(audio_chunk), &bytes_read, portMAX_DELAY);
        wsClient.sendBinary((char*)audio_chunk, bytes_read);
    }
    wsClient.close();
}

void setup() {
    Serial.begin(115200);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    auto model = tflite::GetModel(g_kws_model_data);
    static tflite::AllOpsResolver resolver;
    static tflite::MicroInterpreter static_interpreter(model, resolver, tensor_arena, 40 * 1024, nullptr);
    interpreter = &static_interpreter;
    interpreter->AllocateTensors();

    input_tensor = interpreter->input(0);
    output_tensor = interpreter->output(0);

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .dma_buf_count = 4,
        .dma_buf_len = 512
    };
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
}

void loop() {
    int16_t pcm_buffer[512];
    size_t bytes_read = 0;

    i2s_read(I2S_NUM_0, pcm_buffer, sizeof(pcm_buffer), &bytes_read, portMAX_DELAY);
    update_buffer((uint8_t*)pcm_buffer, bytes_read);

    if (interpreter->Invoke() == kTfLiteOk) {
        if (output_tensor->data.int8[2] > 200) { 
            Serial.println("Wake Word Detected!");
            send_audio_to_cloud();
        }
    }
}