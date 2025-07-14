#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include <vector>
#include <functional>
#include <string>

#if __has_include("esp_h264_decoder.h")
  #include "esp_h264_decoder.h"
  #define HAS_ESP_H264_DECODER
#endif

namespace esphome {
namespace h264_decoder {

// Forward declarations
class FrameDecodedTrigger;
class DecodeErrorTrigger;

enum class PixelFormat : uint8_t {
  RGB565 = 0,
  RGB888 = 1
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
  
  void set_frame_buffer_size(size_t size) { frame_buffer_size_ = size; }
  void set_max_frame_size(uint32_t width, uint32_t height) { 
    max_width_ = width;
    max_height_ = height; 
  }
  void set_pixel_format(PixelFormat format) { pixel_format_ = format; }
  
  void add_on_frame_decoded_callback(std::function<void(DecodedFrame&)> callback) {
    on_frame_decoded_callbacks_.push_back(callback);
  }
  
  void add_on_decode_error_callback(std::function<void(const std::string&)> callback) {
    on_decode_error_callbacks_.push_back(callback);
  }
  
  bool decode_frame(const uint8_t* h264_data, size_t data_size);
  bool is_decoder_ready() const { return decoder_initialized_; }
  void reset_decoder();

 protected:
  size_t frame_buffer_size_{0};
  uint32_t max_width_{640};
  uint32_t max_height_{480};
  PixelFormat pixel_format_{PixelFormat::RGB565};
  bool decoder_initialized_{false};

#ifdef HAS_ESP_H264_DECODER
  esp_h264_decoder_handle_t decoder_handle_{nullptr};
  esp_h264_decoder_config_t decoder_config_{};
#endif

  std::vector<uint8_t> frame_buffer_;
  std::vector<uint8_t> temp_buffer_;
  std::vector<uint8_t> input_buffer_;

  struct SoftwareDecoderState {
    bool has_sps;
    bool has_pps;
    uint32_t width;
    uint32_t height;
  } sw_decoder_state_{};

  std::vector<std::function<void(DecodedFrame&)>> on_frame_decoded_callbacks_;
  std::vector<std::function<void(const std::string&)>> on_decode_error_callbacks_;

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
};

class FrameDecodedTrigger : public Trigger<DecodedFrame&> {
 public:
  explicit FrameDecodedTrigger(H264DecoderComponent* parent) {
    parent->add_on_frame_decoded_callback([this](DecodedFrame& frame) {
      this->trigger(frame);
    });
  }
};

class DecodeErrorTrigger : public Trigger<const std::string&> {
 public:
  explicit DecodeErrorTrigger(H264DecoderComponent* parent) {
    parent->add_on_decode_error_callback([this](const std::string& error) {
      this->trigger(error);
    });
  }
};

}  // namespace h264_decoder
}  // namespace esphome



