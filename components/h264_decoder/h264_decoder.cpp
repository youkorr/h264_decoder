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
  
#ifdef HAS_ESP_VIDEO_DEC
  ESP_LOGI(TAG, "Using ESP Video Decoder API");
#elif defined(HAS_ESP_MM_DEC)
  ESP_LOGI(TAG, "Using ESP MM Decoder API");
#else
  ESP_LOGW(TAG, "Using software-only decoder (limited functionality)");
#endif
  
  // Calculer la taille du buffer si pas spécifiée
  if (frame_buffer_size_ == 0) {
    frame_buffer_size_ = calculate_frame_buffer_size();
  }
  
  // Allouer les buffers
  frame_buffer_.resize(frame_buffer_size_);
  temp_buffer_.resize(frame_buffer_size_);
  
  // Initialiser le décodeur
  if (!initialize_decoder()) {
    ESP_LOGE(TAG, "Failed to initialize H.264 decoder");
    this->mark_failed();
    return;
  }
  
  ESP_LOGCONFIG(TAG, "H.264 Decoder setup complete");
}

void H264DecoderComponent::loop() {
  // Rien à faire dans la boucle principale pour l'instant
}

void H264DecoderComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "H.264 Decoder:");
  ESP_LOGCONFIG(TAG, "  Max Frame Size: %dx%d", max_width_, max_height_);
  ESP_LOGCONFIG(TAG, "  Frame Buffer Size: %zu bytes", frame_buffer_size_);
  ESP_LOGCONFIG(TAG, "  Pixel Format: %s", 
    pixel_format_ == PixelFormat::YUV420P ? "YUV420P" :
    pixel_format_ == PixelFormat::RGB565 ? "RGB565" : "RGB888");
  ESP_LOGCONFIG(TAG, "  Decoder Ready: %s", is_decoder_ready() ? "YES" : "NO");
  
#ifdef HAS_ESP_VIDEO_DEC
  ESP_LOGCONFIG(TAG, "  API: ESP Video Decoder");
#elif defined(HAS_ESP_MM_DEC)
  ESP_LOGCONFIG(TAG, "  API: ESP MM Decoder");
#else
  ESP_LOGCONFIG(TAG, "  API: Software Only");
#endif
}

bool H264DecoderComponent::initialize_decoder() {
#ifdef HAS_ESP_VIDEO_DEC
  // Configuration du décodeur ESP Video
  memset(&decoder_config_, 0, sizeof(decoder_config_));
  decoder_config_.codec = ESP_VIDEO_CODEC_H264;
  decoder_config_.hw_accel = false;
  decoder_config_.output_type = ESP_VIDEO_DEC_OUTPUT_TYPE_YUV420;
  decoder_config_.max_width = max_width_;
  decoder_config_.max_height = max_height_;
  
  esp_err_t ret = esp_video_dec_create(&decoder_config_, &decoder_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create ESP video decoder: %s", esp_err_to_name(ret));
    return false;
  }
  
#elif defined(HAS_ESP_MM_DEC)
  // Configuration du décodeur ESP MM
  memset(&mm_decoder_config_, 0, sizeof(mm_decoder_config_));
  mm_decoder_config_.codec = ESP_MM_DEC_CODEC_H264;
  mm_decoder_config_.max_width = max_width_;
  mm_decoder_config_.max_height = max_height_;
  
  esp_err_t ret = esp_mm_dec_create(&mm_decoder_config_, &mm_decoder_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create ESP MM decoder: %s", esp_err_to_name(ret));
    return false;
  }
  
#else
  // Initialisation du décodeur logiciel
  sw_decoder_state_.initialized = false;
  sw_decoder_state_.has_sps_pps = false;
  ESP_LOGW(TAG, "Software decoder initialized (basic NAL parsing only)");
#endif
  
  decoder_initialized_ = true;
  ESP_LOGI(TAG, "H.264 decoder initialized successfully");
  return true;
}

void H264DecoderComponent::cleanup_decoder() {
#ifdef HAS_ESP_VIDEO_DEC
  if (decoder_handle_) {
    esp_video_dec_destroy(decoder_handle_);
    decoder_handle_ = nullptr;
  }
#elif defined(HAS_ESP_MM_DEC)
  if (mm_decoder_handle_) {
    esp_mm_dec_destroy(mm_decoder_handle_);
    mm_decoder_handle_ = nullptr;
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
  
#ifdef HAS_ESP_VIDEO_DEC
  // Décodage avec ESP Video API
  esp_video_dec_in_frame_t input_frame = {};
  input_frame.buffer = const_cast<uint8_t*>(h264_data);
  input_frame.len = data_size;
  input_frame.pts = esp_timer_get_time();
  
  esp_video_dec_out_frame_t output_frame = {};
  output_frame.buffer = frame_buffer_.data();
  output_frame.buffer_size = frame_buffer_.size();
  
  esp_err_t ret = esp_video_dec_process(decoder_handle_, &input_frame, &output_frame);
  if (ret != ESP_OK) {
    std::string error = "Decode error: " + std::string(esp_err_to_name(ret));
    trigger_error_callbacks(error);
    return false;
  }
  
  if (output_frame.consumed == 0) {
    return true; // Pas de frame complète
  }
  
  DecodedFrame decoded_frame = {};
  decoded_frame.width = output_frame.width;
  decoded_frame.height = output_frame.height;
  decoded_frame.timestamp = output_frame.pts;
  
  if (!convert_pixel_format(&output_frame, decoded_frame)) {
    trigger_error_callbacks("Pixel format conversion failed");
    return false;
  }
  
  trigger_frame_decoded_callbacks(decoded_frame);
  return true;
  
#else
  // Fallback vers décodeur logiciel
  return decode_frame_software(h264_data, data_size);
#endif
}

#ifdef HAS_ESP_VIDEO_DEC
bool H264DecoderComponent::convert_pixel_format(const esp_video_dec_out_frame_t* src_frame, DecodedFrame& dst_frame) {
  dst_frame.format = pixel_format_;
  
  switch (pixel_format_) {
    case PixelFormat::YUV420P:
      dst_frame.data = src_frame->buffer;
      dst_frame.size = src_frame->len;
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

bool H264DecoderComponent::decode_frame_software(const uint8_t* h264_data, size_t data_size) {
  // Décodeur logiciel basique - analyse NAL seulement
  // Cette implémentation est très simplifiée
  
  if (!parse_h264_nal(h264_data, data_size)) {
    trigger_error_callbacks("Failed to parse H.264 NAL units");
    return false;
  }
  
  // Pour l'instant, on génère une frame factice pour les tests
  DecodedFrame decoded_frame = {};
  decoded_frame.width = 320;  // Taille fixe pour test
  decoded_frame.height = 240;
  decoded_frame.format = PixelFormat::YUV420P;
  decoded_frame.data = frame_buffer_.data();
  decoded_frame.size = 320 * 240 * 3 / 2;
  decoded_frame.timestamp = esp_timer_get_time();
  
  // Générer une frame de test (gradient)
  uint8_t* y = frame_buffer_.data();
  uint8_t* u = y + 320 * 240;
  uint8_t* v = u + 320 * 240 / 4;
  
  for (int i = 0; i < 320 * 240; i++) {
    y[i] = (i % 256);
  }
  memset(u, 128, 320 * 240 / 4);
  memset(v, 128, 320 * 240 / 4);
  
  trigger_frame_decoded_callbacks(decoded_frame);
  return true;
}

bool H264DecoderComponent::parse_h264_nal(const uint8_t* data, size_t size) {
  // Analyse basique des NAL units
  for (size_t i = 0; i < size - 4; i++) {
    if (data[i] == 0x00 && data[i+1] == 0x00 && data[i+2] == 0x00 && data[i+3] == 0x01) {
      ESP_LOGV(TAG, "Found NAL start code at position %zu", i);
      return true;
    }
  }
  return false;
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
  if (decoder_initialized_) {
#ifdef HAS_ESP_VIDEO_DEC
    if (decoder_handle_) {
      esp_video_dec_reset(decoder_handle_);
    }
#elif defined(HAS_ESP_MM_DEC)
    if (mm_decoder_handle_) {
      esp_mm_dec_reset(mm_decoder_handle_);
    }
#endif
  }
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


