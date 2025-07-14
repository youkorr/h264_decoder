#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include <vector>
#include <functional>
#include <string>

// Détection des APIs vidéo disponibles
#if defined(CONFIG_ESP_VIDEO_ENABLE) || defined(CONFIG_ESP32P4_ENABLE_VIDEO)
  #if __has_include("esp_video_dec.h")
    #include "esp_video_dec.h"
    #define HAS_ESP_VIDEO_DEC
  #elif __has_include("esp_mm_dec.h")
    #include "esp_mm_dec.h"
    #define HAS_ESP_MM_DEC
  #else
    #define HAS_SOFTWARE_DECODER_ONLY
  #endif
#else
  #define HAS_SOFTWARE_DECODER_ONLY
#endif

namespace esphome {
namespace h264_decoder {

enum class PixelFormat {
  YUV420P,
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
  PixelFormat pixel_format_{PixelFormat::YUV420P};
  bool decoder_initialized_{false};
  
  // Handles pour les différentes APIs
#ifdef HAS_ESP_VIDEO_DEC
  esp_video_dec_handle_t decoder_handle_{nullptr};
  esp_video_dec_cfg_t decoder_config_{};
#elif defined(HAS_ESP_MM_DEC)
  esp_mm_dec_handle_t mm_decoder_handle_{nullptr};
  esp_mm_dec_cfg_t mm_decoder_config_{};
#else
  void* decoder_handle_{nullptr}; // Placeholder pour software decoder
#endif
  
  // Buffers
  std::vector<uint8_t> frame_buffer_;
  std::vector<uint8_t> temp_buffer_;
  
  // Software decoder state (fallback)
  struct SoftwareDecoderState {
    bool initialized;
    std::vector<uint8_t> sps_pps_buffer;
    bool has_sps_pps;
  } sw_decoder_state_{false, {}, false};
  
  // Callbacks
  std::vector<std::function<void(DecodedFrame&)>> on_frame_decoded_callbacks_;
  std::vector<std::function<void(const std::string&)>> on_decode_error_callbacks_;
  
  // Méthodes privées
  bool initialize_decoder();
  void cleanup_decoder();
  size_t calculate_frame_buffer_size();
  
#ifdef HAS_ESP_VIDEO_DEC
  bool convert_pixel_format(const esp_video_dec_out_frame_t* src_frame, DecodedFrame& dst_frame);
#else
  bool convert_pixel_format_software(const uint8_t* yuv_data, uint32_t width, uint32_t height, DecodedFrame& dst_frame);
#endif
  
  bool yuv420_to_rgb(const uint8_t* yuv_data, uint8_t* rgb_data, 
                     uint32_t width, uint32_t height, PixelFormat format);
  void trigger_frame_decoded_callbacks(DecodedFrame& frame);
  void trigger_error_callbacks(const std::string& error);
  
  // Software decoder methods (fallback)
  bool decode_frame_software(const uint8_t* h264_data, size_t data_size);
  bool parse_h264_nal(const uint8_t* data, size_t size);
  bool is_keyframe(const uint8_t* nal_data, size_t nal_size);
};

// Actions pour l'automation (version simplifiée)
template<typename... Ts>
class DecodeFrameAction : public Action<Ts...> {
 public:
  DecodeFrameAction(H264DecoderComponent* parent) : parent_(parent) {}
  
  void set_h264_data(const std::vector<uint8_t>& data) { 
    h264_data_ = data; 
  }
  
  void set_h264_data(const std::string& data_str) {
    h264_data_.assign(data_str.begin(), data_str.end());
  }
  
  void set_data_size(size_t size) { 
    data_size_ = size; 
  }
  
  void play(Ts... x) override {
    const uint8_t* data_ptr = h264_data_.empty() ? nullptr : h264_data_.data();
    size_t actual_size = data_size_ > 0 ? data_size_ : h264_data_.size();
    
    if (data_ptr && actual_size > 0) {
      this->parent_->decode_frame(data_ptr, actual_size);
    }
  }
  
 protected:
  H264DecoderComponent* parent_;
  std::vector<uint8_t> h264_data_;
  size_t data_size_{0};
};

// Triggers pour l'automation
class FrameDecodedTrigger : public Trigger<DecodedFrame&> {
 public:
  FrameDecodedTrigger(H264DecoderComponent* parent) {
    parent->add_on_frame_decoded_callback([this](DecodedFrame& frame) {
      this->trigger(frame);
    });
  }
};

class DecodeErrorTrigger : public Trigger<const std::string&> {
 public:
  DecodeErrorTrigger(H264DecoderComponent* parent) {
    parent->add_on_decode_error_callback([this](const std::string& error) {
      this->trigger(error);
    });
  }
};

}  // namespace h264_decoder
}  // namespace esphome


