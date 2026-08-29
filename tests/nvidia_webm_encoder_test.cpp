#include "mkvcodec/mkvc.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

bool has_nvidia_av1_encoder() {
  size_t count = 0;
  assert(mkvc_get_backend_capabilities(nullptr, &count) == MKVC_OK);
  std::vector<mkvc_backend_capability> capabilities(count);
  if (count != 0) {
    assert(mkvc_get_backend_capabilities(capabilities.data(), &count) ==
           MKVC_OK);
  }
  return std::any_of(
      capabilities.begin(), capabilities.end(), [](const auto &item) {
        return item.backend == MKVC_BACKEND_NVIDIA &&
               item.codec == MKVC_CODEC_AV1 && item.can_encode != 0;
      });
}

void require_ok(mkvc_result result) { assert(result == MKVC_OK); }

} // namespace

int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string output_path = argv[1];
  std::filesystem::remove(output_path);

  constexpr uint32_t width = 160;
  constexpr uint32_t height = 128;
  constexpr uint32_t frame_count = 30;
  mkvc_encoder_config config{};
  config.struct_size = sizeof(config);
  config.struct_version = 1;
  config.output_path_utf8 = output_path.c_str();
  config.codec = MKVC_CODEC_AV1;
  config.backend = MKVC_BACKEND_NVIDIA;
  config.width = width;
  config.height = height;
  config.fps_num = 30;
  config.fps_den = 1;
  config.quality = 32;
  config.keyframe_interval_frames = 30;

  mkvc_encoder *encoder = nullptr;
  if (!has_nvidia_av1_encoder()) {
    assert(mkvc_encoder_create(&config, &encoder) == MKVC_ERROR_NOT_SUPPORTED);
    assert(encoder == nullptr);
    return 0;
  }

  require_ok(mkvc_encoder_create(&config, &encoder));
  const size_t y_size = static_cast<size_t>(width) * height;
  const size_t uv_size = y_size / 4;
  std::vector<uint8_t> image(y_size + 2 * uv_size);
  for (uint32_t index = 0; index < frame_count; ++index) {
    for (size_t offset = 0; offset < y_size; ++offset) {
      image[offset] = static_cast<uint8_t>((offset + index * 7) & 0xff);
    }
    std::fill(image.begin() + y_size, image.begin() + y_size + uv_size,
              static_cast<uint8_t>(96 + index));
    std::fill(image.begin() + y_size + uv_size, image.end(),
              static_cast<uint8_t>(160 - index));

    mkvc_frame_view frame{};
    frame.struct_size = sizeof(frame);
    frame.struct_version = 1;
    frame.pixel_format = MKVC_PIXEL_FORMAT_I420;
    frame.width = width;
    frame.height = height;
    frame.planes[0] = image.data();
    frame.planes[1] = image.data() + y_size;
    frame.planes[2] = image.data() + y_size + uv_size;
    frame.strides[0] = width;
    frame.strides[1] = width / 2;
    frame.strides[2] = width / 2;
    frame.pts = -1;
    require_ok(mkvc_encoder_write_frame(encoder, &frame));
  }

  require_ok(mkvc_encoder_flush(encoder));
  require_ok(mkvc_encoder_close(encoder));
  mkvc_pipeline_metrics metrics{};
  metrics.struct_size = sizeof(metrics);
  metrics.struct_version = 1;
  require_ok(mkvc_encoder_get_metrics(encoder, &metrics));
  assert(metrics.accepted_frames == frame_count);
  assert(metrics.completed_frames == frame_count);
  assert(metrics.hardware_pending_peak == 1);
  mkvc_encoder_destroy(encoder);
  assert(std::filesystem::exists(output_path));
  assert(std::filesystem::file_size(output_path) > 0);
  return 0;
}
