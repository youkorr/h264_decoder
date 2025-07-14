esphome:
  name: esp32-p4-h264-decoder
  platform: ESP32
  board: esp32-p4-function-ev-board

# Configuration WiFi
wifi:
  ssid: "YOUR_WIFI_SSID"
  password: "YOUR_WIFI_PASSWORD"

# API pour Home Assistant
api:
  encryption:
    key: "YOUR_API_KEY"

# OTA Updates
ota:
  password: "YOUR_OTA_PASSWORD"

# Logger
logger:
  level: INFO

# Composant externe H.264 decoder
external_components:
  - source: https://github.com/youkorr/h264_decoder
    type: GIT
    components: [h264_decoder]

# Configuration du décodeur H.264
h264_decoder:
  id: my_h264_decoder
  max_frame_width: 640
  max_frame_height: 480
  pixel_format: RGB565
  frame_buffer_size: 614400  # 640x480x2 pour RGB565
  
  # Callback quand une frame est décodée
  on_frame_decoded:
    - lambda: |-
        ESP_LOGI("H264", "Frame décodée: %dx%d, taille: %zu bytes", 
                 width, height, size);
        
        // Exemple: afficher sur un écran LCD
        // display->draw_image(0, 0, width, height, data);
        
        // Exemple: envoyer via WiFi
        // send_frame_to_server(data, size);
        
        // Exemple: sauvegarder sur SD
        // save_frame_to_sd(data, size);
  
  # Callback en cas d'erreur
  on_decode_error:
    - lambda: |-
        ESP_LOGE("H264", "Erreur de décodage: %s", error_msg.c_str());

# Exemple d'utilisation avec une action
script:
  - id: decode_test_frame
    then:
      - h264_decoder.decode_frame:
          id: my_h264_decoder
          h264_data: !lambda "return test_h264_data;"
          data_size: !lambda "return sizeof(test_h264_data);"

# Exemple avec un bouton pour tester
button:
  - platform: template
    name: "Test H264 Decode"
    on_press:
      - script.execute: decode_test_frame

# Exemple d'intégration avec un serveur de streaming
http_request:
  useragent: esphome/h264_decoder
  timeout: 10s

interval:
  - interval: 100ms
    then:
      - lambda: |-
          // Exemple: récupérer des données H.264 depuis un serveur
          static uint8_t frame_counter = 0;
          
          // Simuler la réception de données H.264
          if (frame_counter++ % 10 == 0) {
            // Décoder une frame de test
            id(my_h264_decoder).decode_frame(test_h264_data, sizeof(test_h264_data));
          }

# Exemple d'intégration avec un display
spi:
  clk_pin: GPIO18
  mosi_pin: GPIO19

display:
  - platform: ili9341
    model: TFT_24
    cs_pin: GPIO5
    dc_pin: GPIO16
    reset_pin: GPIO17


font:
  - file: "gfonts://Roboto"
    id: font
    size: 12

# Exemple avec capteur de performance
sensor:
  - platform: template
    name: "Decoder Frame Rate"
    id: decoder_fps
    unit_of_measurement: "fps"
    accuracy_decimals: 1
    lambda: |-
      static unsigned long last_time = 0;
      static int frame_count = 0;
      
      unsigned long now = millis();
      if (now - last_time >= 1000) {
        float fps = frame_count / ((now - last_time) / 1000.0);
        frame_count = 0;
        last_time = now;
        return fps;
      }
      return {};
    
  - platform: template
    name: "Decoder Memory Usage"
    unit_of_measurement: "KB"
    lambda: |-
      return heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024.0;
