#include "h264_decoder.h"
#include "esphome/core/log.h"
#include "esp_timer.h"

namespace esphome {
namespace h264_decoder {

static const char* TAG = "h264_decoder";

void H264DecoderComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up H.264 Decoder...");
  
#ifdef HAS_ESP_H264_DECODER
  ESP_LOGI(TAG, "Using ESP H.264 Software Decoder");
#else
  ESP_LOGW(TAG, "Using basic software decoder fallback");
#endif
  
  if (frame_buffer_size_ == 0) {
    frame_buffer_size_ = calculate_frame_buffer_size();
  }
  
  frame_buffer_.resize(frame_buffer_size_);
  temp_buffer_.resize(frame_buffer_size_);
  input_buffer_.resize(65536);

  if (!initialize_decoder()) {
    ESP_LOGE(TAG, "Failed to initialize H.264 decoder");
    this->mark_failed();
  }
}

void H264DecoderComponent::loop() {}

void H264DecoderComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "H.264 Decoder:");
  ESP_LOGCONFIG(TAG, "  Max Frame Size: %dx%d", max_width_, max_height_);
  ESP_LOGCONFIG(TAG, "  Frame Buffer Size: %zu bytes", frame_buffer_size_);
  ESP_LOGCONFIG(TAG, "  Pixel Format: %s", 
    pixel_format_ == PixelFormat::RGB565 ? "RGB565" : "RGB888");
}

bool H264DecoderComponent::initialize_decoder() {
#ifdef HAS_ESP_H264_DECODER
  memset(&decoder_config_, 0, sizeof(decoder_config_));
  decoder_config_.max_width = max_width_;
  decoder_config_.max_height = max_height_;
  decoder_config_.output_format = ESP_H264_OUTPUT_FORMAT_RGB565;
  decoder_config_.buffer_size = frame_buffer_size_;
  
  esp_err_t ret = esp_h264_decoder_create(&decoder_config_, &decoder_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create ESP H.264 decoder: %s", esp_err_to_name(ret));
    return false;
  }
#endif

  decoder_initialized_ = true;
  return true;
}

size_t H264DecoderComponent::calculate_frame_buffer_size() {
  switch (pixel_format_) {
    case PixelFormat::RGB565:
      return max_width_ * max_height_ * 2;
    case PixelFormat::RGB888:
      return max_width_ * max_height_ * 3;
    default:
      return max_width_ * max_height_ * 3;
  }
}

bool H264DecoderComponent::decode_frame(const uint8_t* h264_data, size_t data_size) {
  if (!is_decoder_ready()) {
    trigger_error_callbacks("Decoder not ready");
    return false;
  }
  
  if (!h264_data || data_size == 0) {
    trigger_error_callbacks("Invalid input data");
    return false;
  }

#ifdef HAS_ESP_H264_DECODER
  // Implementation with ESP H264 decoder
  // ... (add your implementation here)
#else
  // Software fallback implementation
  // ... (add your implementation here)
#endif

  return true;
}

void H264DecoderComponent::trigger_frame_decoded_callbacks(DecodedFrame& frame) {
  for (auto& callback : on_frame_decoded_callbacks_) {
    callback(frame);
  }
}

void H264DecoderComponent::trigger_error_callbacks(const std::string& error) {
  ESP_LOGE(TAG, "%s", error.c_str());
  for (auto& callback : on_decode_error_callbacks_) {
    callback(error);
  }
}

}  // namespace h264_decoder
}  // namespace esphome


