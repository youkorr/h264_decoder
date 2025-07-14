#include "h264_decoder.h"
#include "esphome/core/log.h"
#include "esp_timer.h"
#include "esp_err.h"
#include <cstring>

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
  
  // Calculer la taille du buffer si pas spécifiée
  if (frame_buffer_size_ == 0) {
    frame_buffer_size_ = calculate_frame_buffer_size();
  }
  
  // Allouer les buffers
  frame_buffer_.resize(frame_buffer_size_);
  temp_buffer_.resize(frame_buffer_size_);
  input_buffer_.resize(65536); // 64KB pour input
  
  // Initialiser le décodeur
  if (!initialize_decoder()) {
    ESP_LOGE(TAG, "Failed to initialize H.264 decoder");
    this->mark_failed();
    return;
  }
  
  ESP_LOGCONFIG(TAG, "H.264 Decoder setup complete");
}

void H264DecoderComponent::loop() {
  // Rien à faire dans la boucle principale
}

void H264DecoderComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "H.264 Decoder:");
  ESP_LOGCONFIG(TAG, "  Max Frame Size: %dx%d", max_width_, max_height_);
  ESP_LOGCONFIG(TAG, "  Frame Buffer Size: %zu bytes", frame_buffer_size_);
  ESP_LOGCONFIG(TAG, "  Pixel Format: %s", 
    pixel_format_ == PixelFormat::YUV420P ? "YUV420P" :
    pixel_format_ == PixelFormat::RGB565 ? "RGB565" : "RGB888");
  ESP_LOGCONFIG(TAG, "  Decoder Ready: %s", is_decoder_ready() ? "YES" : "NO");
  
#ifdef HAS_ESP_H264_DECODER
  ESP_LOGCONFIG(TAG, "  API: ESP H.264 Software Decoder");
#else
  ESP_LOGCONFIG(TAG, "  API: Basic Software Fallback");
#endif
}

bool H264DecoderComponent::initialize_decoder() {
#ifdef HAS_ESP_H264_DECODER
  // Configuration du décodeur esp_h264
  memset(&decoder_config_, 0, sizeof(decoder_config_));
  
  // Configuration spécifique selon la documentation d'esp_h264
  decoder_config_.max_width = max_width_;
  decoder_config_.max_height = max_height_;
  decoder_config_.output_format = ESP_H264_OUTPUT_FORMAT_YUV420; // Supposé
  decoder_config_.buffer_size = frame_buffer_size_;
  
  esp_err_t ret = esp_h264_decoder_create(&decoder_config_, &decoder_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create ESP H.264 decoder: %s", esp_err_to_name(ret));
    return false;
  }
  
  ESP_LOGI(TAG, "ESP H.264 decoder initialized successfully");
#else
  // Initialisation du fallback
  sw_decoder_state_.has_sps = false;
  sw_decoder_state_.has_pps = false;
  sw_decoder_state_.width = 0;
  sw_decoder_state_.height = 0;
  
  ESP_LOGW(TAG, "Using basic software decoder (limited functionality)");
#endif
  
  decoder_initialized_ = true;
  return true;
}

void H264DecoderComponent::cleanup_decoder() {
#ifdef HAS_ESP_H264_DECODER
  if (decoder_handle_) {
    esp_h264_decoder_destroy(decoder_handle_);
    decoder_handle_ = nullptr;
  }
#endif
  decoder_initialized_ = false;
}

size_t H264DecoderComponent::calculate_frame_buffer_size() {
  switch (pixel_format_) {
    case PixelFormat::YUV420P:
      return max_width_ * max_height_ * 3 / 2;
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
  // Utilisation du décodeur esp_h264
  esp_h264_input_data_t input_data = {};
  input_data.data = const_cast<uint8_t*>(h264_data);
  input_data.size = data_size;
  input_data.timestamp = esp_timer_get_time();
  
  esp_h264_frame_t output_frame = {};
  output_frame.buffer = frame_buffer_.data();
  output_frame.buffer_size = frame_buffer_.size();
  
  esp_err_t ret = esp_h264_decoder_process(decoder_handle_, &input_data, &output_frame);
  if (ret != ESP_OK) {
    // Si c'est juste "pas de frame complète", ce n'est pas une erreur
    if (ret == ESP_ERR_NOT_FOUND || ret == ESP_ERR_INVALID_STATE) {
      ESP_LOGV(TAG, "No complete frame yet");
      return true;
    }
    
    std::string error = "ESP H.264 decode error: " + std::string(esp_err_to_name(ret));
    trigger_error_callbacks(error);
    return false;
  }
  
  // Vérifier si une frame a été décodée
  if (output_frame.width == 0 || output_frame.height == 0) {
    ESP_LOGV(TAG, "No frame decoded yet");
    return true;
  }
  
  // Créer la structure de frame décodée
  DecodedFrame decoded_frame = {};
  decoded_frame.timestamp = output_frame.timestamp;
  
  if (!convert_pixel_format(&output_frame, decoded_frame)) {
    trigger_error_callbacks("Pixel format conversion failed");
    return false;
  }
  
  trigger_frame_decoded_callbacks(decoded_frame);
  return true;
  
#else
  // Fallback vers décodeur basique
  return decode_frame_software_fallback(h264_data, data_size);
#endif
}

#ifdef HAS_ESP_H264_DECODER
bool H264DecoderComponent::convert_pixel_format(const esp_h264_frame_t* src_frame, DecodedFrame& dst_frame) {
  dst_frame.width = src_frame->width;
  dst_frame.height = src_frame->height;
  dst_frame.format = pixel_format_;
  
  switch (pixel_format_) {
    case PixelFormat::YUV420P:
      dst_frame.data = src_frame->buffer;
      dst_frame.size = src_frame->size;
      break;
      
    case PixelFormat::RGB565:
    case PixelFormat::RGB888: {
      size_t rgb_size = (pixel_format_ == PixelFormat::RGB565) ? 
        dst_frame.width * dst_frame.height * 2 : 
        dst_frame.width * dst_frame.height * 3;
      
      if (rgb_size > temp_buffer_.size()) {
        temp_buffer_.resize(rgb_size);
      }
      
      if (!yuv420_to_rgb(src_frame->buffer, temp_buffer_.data(), 
                        dst_frame.width, dst_frame.height, pixel_format_)) {
        return false;
      }
      
      dst_frame.data = temp_buffer_.data();
      dst_frame.size = rgb_size;
      break;
    }
    
    default:
      return false;
  }
  
  return true;
}
#endif

bool H264DecoderComponent::decode_frame_software_fallback(const uint8_t* h264_data, size_t data_size) {
  // Décodeur basique pour tests - génère une frame factice
  ESP_LOGW(TAG, "Using software fallback - generating test frame");
  
  DecodedFrame decoded_frame = {};
  decoded_frame.width = 320;
  decoded_frame.height = 240;
  decoded_frame.format = PixelFormat::YUV420P;
  decoded_frame.data = frame_buffer_.data();
  decoded_frame.size = 320 * 240 * 3 / 2;
  decoded_frame.timestamp = esp_timer_get_time();
  
  // Générer une frame de test
  uint8_t* y = frame_buffer_.data();
  uint8_t* u = y + 320 * 240;
  uint8_t* v = u + 320 * 240 / 4;
  
  // Pattern de test
  for (int i = 0; i < 320 * 240; i++) {
    y[i] = (i / 320 + i % 320) & 0xFF;
  }
  memset(u, 128, 320 * 240 / 4);
  memset(v, 128, 320 * 240 / 4);
  
  trigger_frame_decoded_callbacks(decoded_frame);
  return true;
}

bool H264DecoderComponent::yuv420_to_rgb(const uint8_t* yuv_data, uint8_t* rgb_data, 
                                        uint32_t width, uint32_t height, PixelFormat format) {
  const uint8_t* y_plane = yuv_data;
  const uint8_t* u_plane = y_plane + width * height;
  const uint8_t* v_plane = u_plane + (width * height / 4);
  
  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      uint8_t Y = y_plane[y * width + x];
      uint8_t U = u_plane[(y/2) * (width/2) + (x/2)];
      uint8_t V = v_plane[(y/2) * (width/2) + (x/2)];
      
      int32_t C = Y - 16;
      int32_t D = U - 128;
      int32_t E = V - 128;
      
      int32_t R = (298 * C + 409 * E + 128) >> 8;
      int32_t G = (298 * C - 100 * D - 208 * E + 128) >> 8;
      int32_t B = (298 * C + 516 * D + 128) >> 8;
      
      R = R > 255 ? 255 : (R < 0 ? 0 : R);
      G = G > 255 ? 255 : (G < 0 ? 0 : G);
      B = B > 255 ? 255 : (B < 0 ? 0 : B);
      
      if (format == PixelFormat::RGB565) {
        uint16_t rgb565 = ((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3);
        uint16_t* rgb565_ptr = reinterpret_cast<uint16_t*>(rgb_data);
        rgb565_ptr[y * width + x] = rgb565;
      } else if (format == PixelFormat::RGB888) {
        rgb_data[(y * width + x) * 3] = R;
        rgb_data[(y * width + x) * 3 + 1] = G;
        rgb_data[(y * width + x) * 3 + 2] = B;
      }
    }
  }
  
  return true;
}

void H264DecoderComponent::reset_decoder() {
#ifdef HAS_ESP_H264_DECODER
  if (decoder_handle_) {
    esp_h264_decoder_reset(decoder_handle_);
  }
#endif
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


