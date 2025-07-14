#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include <vector>
#include <functional>
#include <string>

// Utilisation du composant esp_h264 d'Espressif si disponible
#if __has_include("esp_h264_decoder.h")
  #include "esp_h264_decoder.h"
  #define HAS_ESP_H264_DECODER
#else
  #define HAS_SOFTWARE_DECODER_ONLY
  #include "esp_log.h"
#endif

namespace esphome {
namespace h264_decoder {

enum class PixelFormat {
  RGB565, 
  RGB888
};

struct DecodedFrame {
  uint8_t* data;
  size_t size;
  uint32_t width;
  uint32_t height;
  PixelFormat format;
  uint64_t timestamp;
};

class H264DecoderComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  
  // Configuration
  void set_frame_buffer_size(size_t size) { frame_buffer_size_ = size; }
  void set_max_frame_size(uint32_t width, uint32_t height) { 
    max_width_ = width; 
    max_height_ = height; 
  }
  void set_pixel_format(PixelFormat format) { pixel_format_ = format; }
  
  // Callbacks
  void add_on_frame_decoded_callback(std::function<void(DecodedFrame&)> callback) {
    on_frame_decoded_callbacks_.push_back(callback);
  }
  void add_on_decode_error_callback(std::function<void(const std::string&)> callback) {
    on_decode_error_callbacks_.push_back(callback);
  }
  
  // Méthodes publiques
  bool decode_frame(const uint8_t* h264_data, size_t data_size);
  bool is_decoder_ready() const { return decoder_initialized_; }
  void reset_decoder();
  
 protected:
  // Configuration
  size_t frame_buffer_size_{0};
  uint32_t max_width_{640};
  uint32_t max_height_{480};
  PixelFormat pixel_format_{PixelFormat::RGB565}; // Changé de YUV420P à RGB565 par défaut
  bool decoder_initialized_{false};
  
#ifdef HAS_ESP_H264_DECODER
  // Handle pour le décodeur esp_h264
  esp_h264_decoder_handle_t decoder_handle_{nullptr};
  esp_h264_decoder_config_t decoder_config_{};
#else
  void* decoder_handle_{nullptr};
#endif
  
  // Buffers
  std::vector<uint8_t> frame_buffer_;
  std::vector<uint8_t> temp_buffer_;
  std::vector<uint8_t> input_buffer_;
  
  // Software decoder fallback state
  struct SoftwareDecoderState {
    bool has_sps;
    bool has_pps;
    uint32_t width;
    uint32_t height;
  } sw_decoder_state_{};
  
  // Callbacks
  std::vector<std::function<void(DecodedFrame&)>> on_frame_decoded_callbacks_;
  std::vector<std::function<void(const std::string&)>> on_decode_error_callbacks_;
  
  // Méthodes privées
  bool initialize_decoder();
  void cleanup_decoder();
  size_t calculate_frame_buffer_size();
  
#ifdef HAS_ESP_H264_DECODER
  bool convert_pixel_format(const esp_h264_frame_t* src_frame, DecodedFrame& dst_frame);
#endif
  
  bool decode_frame_software_fallback(const uint8_t* h264_data, size_t data_size);
  bool yuv420_to_rgb(const uint8_t* yuv_data, uint8_t* rgb_data, 
                     uint32_t width, uint32_t height, PixelFormat format);
  
  void trigger_frame_decoded_callbacks(DecodedFrame& frame);
  void trigger_error_callbacks(const std::string& error);
  
  // Parsing utilities
  std::vector<size_t> find_nal_units(const uint8_t* data, size_t size);
  uint8_t get_nal_type(const uint8_t* nal_data);
};

// Actions pour l'automation
template<typename... Ts>
class DecodeFrameAction : public Action<Ts...> {
 public:
  DecodeFrameAction(H264DecoderComponent* parent) : parent_(parent) {}
  
  TEMPLATABLE_VALUE(std::vector<uint8_t>, h264_data)
  TEMPLATABLE_VALUE(size_t, data_size)
  
  void play(Ts... x) override {
    auto data_vector = this->h264_data_.value(x...);
    auto size_val = this->data_size_.value(x...);
    
    const uint8_t* data_ptr = data_vector.empty() ? nullptr : data_vector.data();
    size_t actual_size = size_val > 0 ? size_val : data_vector.size();
    
    if (data_ptr && actual_size > 0) {
      this->parent_->decode_frame(data_ptr, actual_size);
    }
  }
  
 protected:
  H264DecoderComponent* parent_;
};

// Triggers pour l'automation
class FrameDecodedTrigger : public Trigger<DecodedFrame&> {
 public:
  explicit FrameDecodedTrigger(H264DecoderComponent* parent) {
    parent->add_on_frame_decoded_callback([this](DecodedFrame& frame) {
      this->trigger(frame);
    });
  }
};

class DecodeErrorTrigger : public Trigger<const char*> {
 public:
  explicit DecodeErrorTrigger(H264DecoderComponent* parent) {
    parent->add_on_decode_error_callback([this](const std::string& error) {
      this->trigger(error.c_str());
    });
  }
};

}  // namespace h264_decoder

// Export des types dans le namespace esphome pour ESPHome
using h264_decoder::PixelFormat;
using h264_decoder::DecodedFrame;
using h264_decoder::H264DecoderComponent;
using h264_decoder::DecodeFrameAction;
using h264_decoder::FrameDecodedTrigger;
using h264_decoder::DecodeErrorTrigger;

}  // namespace esphome



