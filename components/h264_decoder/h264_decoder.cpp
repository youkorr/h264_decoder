#include "h264_decoder.h"
#include "esphome/core/log.h"
#include "esp_timer.h"
#include "esp_err.h"

namespace esphome {
namespace h264_decoder {

static const char* TAG = "h264_decoder";

void H264DecoderComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up H.264 Decoder...");
  
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
}

bool H264DecoderComponent::initialize_decoder() {
  // Configuration du décodeur pour ESP32-P4
  memset(&decoder_config_, 0, sizeof(decoder_config_));
  decoder_config_.codec = ESP_VIDEO_CODEC_H264;
  decoder_config_.hw_accel = false;  // Décodage logiciel sur P4
  decoder_config_.output_type = ESP_VIDEO_DEC_OUTPUT_TYPE_YUV420;
  decoder_config_.max_width = max_width_;
  decoder_config_.max_height = max_height_;
  
  // Configuration de la mémoire
  decoder_config_.flags = 0;
  
  // Créer le décodeur
  esp_err_t ret = esp_video_dec_create(&decoder_config_, &decoder_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create decoder: %s", esp_err_to_name(ret));
    return false;
  }
  
  ESP_LOGI(TAG, "H.264 decoder initialized successfully");
  return true;
}

void H264DecoderComponent::cleanup_decoder() {
  if (decoder_handle_) {
    esp_video_dec_destroy(decoder_handle_);
    decoder_handle_ = nullptr;
  }
}

size_t H264DecoderComponent::calculate_frame_buffer_size() {
  switch (pixel_format_) {
    case PixelFormat::YUV420P:
      return max_width_ * max_height_ * 3 / 2;  // 1.5 bytes per pixel
    case PixelFormat::RGB565:
      return max_width_ * max_height_ * 2;      // 2 bytes per pixel
    case PixelFormat::RGB888:
      return max_width_ * max_height_ * 3;      // 3 bytes per pixel
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
  
  // Préparer les structures pour le décodage
  esp_video_dec_in_frame_t input_frame = {};
  input_frame.buffer = const_cast<uint8_t*>(h264_data);
  input_frame.len = data_size;
  input_frame.pts = esp_timer_get_time();
  
  esp_video_dec_out_frame_t output_frame = {};
  output_frame.buffer = frame_buffer_.data();
  output_frame.buffer_size = frame_buffer_.size();
  
  // Décoder la frame
  esp_err_t ret = esp_video_dec_process(decoder_handle_, &input_frame, &output_frame);
  if (ret != ESP_OK) {
    std::string error = "Decode error: " + std::string(esp_err_to_name(ret));
    trigger_error_callbacks(error);
    return false;
  }
  
  // Vérifier si une frame a été décodée
  if (output_frame.consumed == 0) {
    // Pas de frame complète encore
    return true;
  }
  
  // Créer la structure de frame décodée
  DecodedFrame decoded_frame = {};
  decoded_frame.width = output_frame.width;
  decoded_frame.height = output_frame.height;
  decoded_frame.timestamp = output_frame.pts;
  
  // Convertir le format de pixel si nécessaire
  if (!convert_pixel_format(&output_frame, decoded_frame)) {
    trigger_error_callbacks("Pixel format conversion failed");
    return false;
  }
  
  // Déclencher les callbacks
  trigger_frame_decoded_callbacks(decoded_frame);
  
  return true;
}

bool H264DecoderComponent::convert_pixel_format(const esp_video_dec_out_frame_t* src_frame, DecodedFrame& dst_frame) {
  dst_frame.format = pixel_format_;
  
  switch (pixel_format_) {
    case PixelFormat::YUV420P:
      // Pas de conversion nécessaire, c'est déjà en YUV420
      dst_frame.data = src_frame->buffer;
      dst_frame.size = src_frame->len;
      break;
      
    case PixelFormat::RGB565:
    case PixelFormat::RGB888: {
      // Conversion YUV420 vers RGB
      // Utiliser temp_buffer_ pour la conversion
      size_t rgb_size = (pixel_format_ == PixelFormat::RGB565) ? 
        dst_frame.width * dst_frame.height * 2 : 
        dst_frame.width * dst_frame.height * 3;
      
      if (rgb_size > temp_buffer_.size()) {
        temp_buffer_.resize(rgb_size);
      }
      
      // Conversion YUV420 -> RGB
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

bool H264DecoderComponent::yuv420_to_rgb(const uint8_t* yuv_data, uint8_t* rgb_data, 
                                        uint32_t width, uint32_t height, PixelFormat format) {
  // Implémentation simplifiée de la conversion YUV420 vers RGB
  // Dans un projet réel, utilisez une bibliothèque optimisée
  
  const uint8_t* y_plane = yuv_data;
  const uint8_t* u_plane = y_plane + width * height;
  const uint8_t* v_plane = u_plane + (width * height / 4);
  
  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      // Récupérer les composantes YUV
      uint8_t Y = y_plane[y * width + x];
      uint8_t U = u_plane[(y/2) * (width/2) + (x/2)];
      uint8_t V = v_plane[(y/2) * (width/2) + (x/2)];
      
      // Conversion YUV vers RGB (ITU-R BT.601)
      int32_t C = Y - 16;
      int32_t D = U - 128;
      int32_t E = V - 128;
      
      int32_t R = (298 * C + 409 * E + 128) >> 8;
      int32_t G = (298 * C - 100 * D - 208 * E + 128) >> 8;
      int32_t B = (298 * C + 516 * D + 128) >> 8;
      
      // Clamping
      R = R > 255 ? 255 : (R < 0 ? 0 : R);
      G = G > 255 ? 255 : (G < 0 ? 0 : G);
      B = B > 255 ? 255 : (B < 0 ? 0 : B);
      
      // Stocker selon le format
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
  if (decoder_handle_) {
    esp_video_dec_reset(decoder_handle_);
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

