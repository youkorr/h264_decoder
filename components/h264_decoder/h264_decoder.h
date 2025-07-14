#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"

// Vérification de la disponibilité du décodeur vidéo pour ESP32-P4
#ifdef CONFIG_ESP_VIDEO_ENABLE
#include "esp_video_dec.h"
#define HAS_H264_DECODER
#else
#error "Video decoder support not enabled. Please add CONFIG_ESP_VIDEO_ENABLE: 'y' to sdkconfig_options."
#endif

#include <vector>
#include <functional>
#include <string>

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
  bool is_decoder_ready() const { return decoder_handle_ != nullptr; }
  void reset_decoder();
  
 protected:
  // Configuration
  size_t frame_buffer_size_{0};
  uint32_t max_width_{640};
  uint32_t max_height_{480};
  PixelFormat pixel_format_{PixelFormat::YUV420P};
  
  // ESP-IDF decoder
  esp_video_dec_handle_t decoder_handle_{nullptr};
  esp_video_dec_cfg_t decoder_config_{};
  
  // Buffers
  std::vector<uint8_t> frame_buffer_;
  std::vector<uint8_t> temp_buffer_;
  
  // Callbacks
  std::vector<std::function<void(DecodedFrame&)>> on_frame_decoded_callbacks_;
  std::vector<std::function<void(const std::string&)>> on_decode_error_callbacks_;
  
  // Méthodes privées
  bool initialize_decoder();
  void cleanup_decoder();
  size_t calculate_frame_buffer_size();
  bool convert_pixel_format(const esp_video_dec_out_frame_t* src_frame, DecodedFrame& dst_frame);
  bool yuv420_to_rgb(const uint8_t* yuv_data, uint8_t* rgb_data, 
                     uint32_t width, uint32_t height, PixelFormat format);
  void trigger_frame_decoded_callbacks(DecodedFrame& frame);
  void trigger_error_callbacks(const std::string& error);
};

// Actions pour l'automation
template<typename... Ts>
class DecodeFrameAction : public Action<Ts...> {
 public:
  DecodeFrameAction(H264DecoderComponent* parent) : parent_(parent) {}
  
  TEMPLATABLE_VALUE(const uint8_t*, h264_data)
  TEMPLATABLE_VALUE(size_t, data_size)
  
  void play(Ts... x) override {
    auto data = this->h264_data_.value(x...);
    auto size = this->data_size_.value(x...);
    this->parent_->decode_frame(data, size);
  }
  
 protected:
  H264DecoderComponent* parent_;
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
