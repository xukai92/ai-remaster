#include "remaster/PngArtifactCache.h"

#include <png.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string_view>
#include <system_error>
#include <vector>
#include <fcntl.h>
#include <unistd.h>

namespace remaster {
namespace {

constexpr std::string_view kMagic = "remaster-png-cache-v1";

std::string Field(std::string_view value) {
  return std::to_string(value.size()) + ":" + std::string(value);
}

std::string Hex(std::string_view value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(value.size() * 2);
  for (unsigned char byte : value) {
    result.push_back(digits[byte >> 4]);
    result.push_back(digits[byte & 15]);
  }
  return result;
}

bool ValidComponent(const std::string &value) {
  return !value.empty() && value.size() <= 96;
}

bool RegularFile(const std::filesystem::path &path, size_t maxBytes) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error || !std::filesystem::is_regular_file(status)) return false;
  const auto bytes = std::filesystem::file_size(path, error);
  return !error && bytes > 0 && bytes <= maxBytes;
}

bool SyncFile(const std::filesystem::path &path) {
  const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) return false;
  const bool okay = fsync(fd) == 0;
  close(fd);
  return okay;
}

bool SyncDirectory(const std::filesystem::path &path) {
  const int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) return false;
  const bool synced = fsync(fd) == 0;
  const bool closed = close(fd) == 0;
  return synced && closed;
}

class TemporaryFile {
public:
  explicit TemporaryFile(const std::filesystem::path &finalPath) {
    std::string pattern = finalPath.string() + ".tmp.XXXXXX";
    std::vector<char> bytes(pattern.begin(), pattern.end());
    bytes.push_back('\0');
    const int fd = mkstemp(bytes.data());
    if (fd >= 0) {
      close(fd);
      path_ = bytes.data();
    }
  }
  ~TemporaryFile() {
    if (!path_.empty()) {
      std::error_code error;
      std::filesystem::remove(path_, error);
    }
  }
  const std::filesystem::path &Path() const { return path_; }
  bool Valid() const { return !path_.empty(); }
private:
  std::filesystem::path path_;
};

bool Checksum(const std::filesystem::path &path, size_t maxBytes, uint64_t *output) {
  if (!RegularFile(path, maxBytes)) return false;
  std::ifstream input(path, std::ios::binary);
  if (!input) return false;
  uint64_t hash = 14695981039346656037ull;
  std::array<char, 8192> buffer{};
  while (input) {
    input.read(buffer.data(), buffer.size());
    for (std::streamsize i = 0; i < input.gcount(); ++i) {
      hash ^= static_cast<unsigned char>(buffer[static_cast<size_t>(i)]);
      hash *= 1099511628211ull;
    }
  }
  if (!input.eof()) return false;
  *output = hash;
  return true;
}

template <typename Integer>
bool ParseInteger(std::string_view text, Integer *value) {
  const auto *end = text.data() + text.size();
  const auto result = std::from_chars(text.data(), end, *value);
  return result.ec == std::errc{} && result.ptr == end;
}

bool SafeDirectories(const std::filesystem::path &root,
                     const std::filesystem::path &parent) {
  std::error_code error;
  std::filesystem::create_directories(root, error);
  if (error) return false;
  auto current = root;
  if (std::filesystem::is_symlink(std::filesystem::symlink_status(current, error)) || error)
    return false;
  for (const auto &part : parent.lexically_relative(root)) {
    current /= part;
    std::filesystem::create_directory(current, error);
    if (error && error != std::errc::file_exists) return false;
    error.clear();
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(current, error)) || error ||
        !std::filesystem::is_directory(current)) return false;
  }
  return true;
}

bool DecodeValidPng(const std::filesystem::path &path, int width, int height,
                    const CacheLimits &limits) {
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_file(&image, path.c_str())) return false;
  const bool dimensionsOkay = image.width == static_cast<unsigned>(width) &&
      image.height == static_cast<unsigned>(height) &&
      image.width <= static_cast<unsigned>(limits.maxDimension) &&
      image.height <= static_cast<unsigned>(limits.maxDimension) &&
      static_cast<size_t>(image.width) * image.height <= limits.maxPixelBytes / 4;
  if (!dimensionsOkay) { png_image_free(&image); return false; }
  image.format = PNG_FORMAT_RGBA;
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
  const bool okay = png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr) != 0;
  png_image_free(&image);
  return okay;
}

} // namespace

std::string FormatCacheKey(const CacheKey &key) {
  return std::to_string(key.schemaVersion) + ";" + Field(key.texture.emulator) +
      Field(key.texture.gameId) + Field(key.texture.contentKey) +
      std::to_string(key.texture.mipLevel) + ";" + Field(key.modelId) +
      Field(key.modelVersion) + Field(key.configVersion);
}

PngArtifactCache::PngArtifactCache(std::filesystem::path root, CacheLimits limits)
    : root_(std::move(root)), limits_(limits) {}

bool PngArtifactCache::Paths(const CacheKey &key, std::filesystem::path *png,
                             std::filesystem::path *meta) const {
  if (!png || !meta || root_.empty() || key.schemaVersion != kCacheSchemaVersion ||
      key.texture.mipLevel < 0 || !ValidComponent(key.texture.emulator) ||
      !ValidComponent(key.texture.gameId) || !ValidComponent(key.texture.contentKey) ||
      !ValidComponent(key.modelId) || !ValidComponent(key.modelVersion) ||
      !ValidComponent(key.configVersion)) return false;
  const auto parent = root_ / ("v" + std::to_string(key.schemaVersion)) /
      Hex(key.texture.emulator) / Hex(key.texture.gameId) /
      Hex(key.modelId) / Hex(key.modelVersion) / Hex(key.configVersion);
  *png = parent / (Hex(key.texture.contentKey) + "_m" +
                   std::to_string(key.texture.mipLevel) + ".png");
  *meta = *png;
  *meta += ".meta";
  return true;
}

bool PngArtifactCache::Lookup(const CacheKey &key, std::filesystem::path *artifact) {
  if (!artifact) return false;
  artifact->clear();
  try {
    std::filesystem::path png, meta;
    if (!Paths(key, &png, &meta) || !RegularFile(meta, 4096)) return false;
    std::ifstream input(meta, std::ios::binary);
    std::string magic, identity, widthText, heightText, checksumText, extra;
    if (!std::getline(input, magic) || !std::getline(input, identity) ||
        !std::getline(input, widthText) || !std::getline(input, heightText) ||
        !std::getline(input, checksumText) || std::getline(input, extra) ||
        magic != kMagic || identity != Hex(FormatCacheKey(key))) return false;
    int width = 0, height = 0;
    uint64_t expected = 0, actual = 0;
    if (!ParseInteger(widthText, &width) || !ParseInteger(heightText, &height) ||
        !ParseInteger(checksumText, &expected) || width <= 0 || height <= 0 ||
        width > limits_.maxDimension || height > limits_.maxDimension ||
        static_cast<size_t>(width) * height > limits_.maxPixelBytes / 4 ||
        !Checksum(png, limits_.maxPngBytes, &actual) || actual != expected ||
        !DecodeValidPng(png, width, height, limits_)) return false;
    *artifact = std::move(png);
    return true;
  } catch (...) { return false; }
}

bool PngArtifactCache::StoreAtomically(const CacheKey &key, const OwnedImage &image,
                                        std::filesystem::path *artifact) {
  if (!artifact) return false;
  artifact->clear();
  try {
    std::filesystem::path png, meta;
    if (!Paths(key, &png, &meta) || image.format != PixelFormat::RGBA8 ||
        image.width <= 0 || image.height <= 0 ||
        image.width > limits_.maxDimension || image.height > limits_.maxDimension ||
        image.width > std::numeric_limits<int>::max() / 4 ||
        static_cast<size_t>(image.width) * image.height > limits_.maxPixelBytes / 4 ||
        image.strideBytes < image.width * 4) return false;
    const size_t needed = static_cast<size_t>(image.height - 1) * image.strideBytes +
                          static_cast<size_t>(image.width) * 4;
    if (needed > image.pixels.size() || !SafeDirectories(root_, png.parent_path())) return false;
    OwnedImage packed;
    if (!CopyImage(image.View(), limits_.maxPixelBytes, &packed)) return false;
    TemporaryFile pngTemp(png), metaTemp(meta);
    if (!pngTemp.Valid() || !metaTemp.Valid()) return false;
    png_image output{};
    output.version = PNG_IMAGE_VERSION;
    output.width = static_cast<png_uint_32>(packed.width);
    output.height = static_cast<png_uint_32>(packed.height);
    output.format = PNG_FORMAT_RGBA;
    if (!png_image_write_to_file(&output, pngTemp.Path().c_str(), 0,
                                 packed.pixels.data(), 0, nullptr) ||
        !SyncFile(pngTemp.Path())) return false;
    uint64_t checksum = 0;
    if (!Checksum(pngTemp.Path(), limits_.maxPngBytes, &checksum)) return false;
    {
      std::ofstream out(metaTemp.Path(), std::ios::binary | std::ios::trunc);
      out << kMagic << '\n' << Hex(FormatCacheKey(key)) << '\n' << packed.width << '\n'
          << packed.height << '\n' << checksum << '\n';
      out.flush();
      if (!out) return false;
    }
    if (!SyncFile(metaTemp.Path())) return false;
    std::error_code error;
    std::filesystem::rename(pngTemp.Path(), png, error);
    if (error) return false;
    std::filesystem::rename(metaTemp.Path(), meta, error);
    if (error) return false;
    if (!SyncDirectory(png.parent_path())) return false;
    *artifact = std::move(png);
    return true;
  } catch (...) { return false; }
}

} // namespace remaster
