#include "remaster/FakeBackend.h"
#include "remaster/PngArtifactCache.h"

#include <png.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <unistd.h>

using namespace remaster;
namespace fs = std::filesystem;

static void Check(bool okay, const char *message) {
  if (!okay) throw std::runtime_error(message);
}

static uint64_t Checksum(std::string_view bytes) {
  uint64_t hash = 14695981039346656037ull;
  for (unsigned char byte : bytes) { hash ^= byte; hash *= 1099511628211ull; }
  return hash;
}

static CacheKey Key() {
  return {kCacheSchemaVersion, {"emu", "game", "source", 0}, "fake", "2x-v1", "cfg-v1"};
}

static OwnedImage Image() {
  OwnedImage image;
  image.width = 2;
  image.height = 1;
  image.strideBytes = 8;
  image.format = PixelFormat::RGBA8;
  image.pixels = {1, 2, 3, 4, 5, 6, 7, 8};
  return image;
}

static void TestUnit() {
  const fs::path root = fs::temp_directory_path() /
      ("remaster-cache-unit-" + std::to_string(getpid()));
  fs::remove_all(root);
  try {
    PngArtifactCache cache(root, {64, 1024, 1024});
    auto key = Key();
    fs::path artifact;
    Check(!cache.Lookup(key, &artifact) && artifact.empty(), "initial miss");
    Check(cache.StoreAtomically(key, Image(), &artifact), "store");
    Check(fs::exists(artifact) && artifact.extension() == ".png", "PNG artifact");
    const fs::path meta = artifact.string() + ".meta";
    fs::path hit;
    Check(cache.Lookup(key, &hit) && hit == artifact, "store/hit");
    png_image decoded{};
    decoded.version = PNG_IMAGE_VERSION;
    Check(png_image_begin_read_from_file(&decoded, artifact.c_str()) != 0,
          "PNG readable by libpng");
    Check(decoded.width == 2 && decoded.height == 1, "PNG dimensions");
    png_image_free(&decoded);

    auto variant = key;
    variant.modelVersion = "2x-v2";
    Check(!cache.Lookup(variant, &hit), "model version invalidation");
    variant = key;
    variant.configVersion = "cfg-v2";
    Check(!cache.Lookup(variant, &hit), "config version invalidation");
    variant = key;
    variant.texture.mipLevel = 1;
    Check(!cache.Lookup(variant, &hit), "mip separation");
    variant = key;
    variant.schemaVersion = 2;
    Check(!cache.Lookup(variant, &hit) &&
          !cache.StoreAtomically(variant, Image(), &hit), "unknown schema rejected");

    // Identity strings become hex path components, so traversal text is data.
    variant = key;
    variant.texture.contentKey = "../../escape";
    Check(cache.StoreAtomically(variant, Image(), &hit), "encoded traversal identity");
    Check(hit.string().find("../") == std::string::npos &&
          hit.parent_path() == artifact.parent_path() &&
          cache.Lookup(variant, &hit), "safe path encoding");
    variant.texture.contentKey = std::string(97, 'x');
    Check(!cache.StoreAtomically(variant, Image(), &hit), "oversized path component rejected");

    // An uncommitted temp file cannot masquerade as a hit.
    fs::remove(artifact);
    fs::remove(meta);
    { std::ofstream interrupted(artifact.string() + ".tmp.interrupted"); interrupted << "partial"; }
    Check(!cache.Lookup(key, &hit), "interrupted temp ignored");
    Check(cache.StoreAtomically(key, Image(), &artifact), "regenerate after interruption");
    Check(cache.Lookup(key, &hit), "regenerated artifact valid");

    std::ifstream beforeDamage(meta, std::ios::binary);
    const std::string originalMeta((std::istreambuf_iterator<char>(beforeDamage)),
                                   std::istreambuf_iterator<char>());
    { std::ofstream damaged(artifact, std::ios::binary | std::ios::trunc); damaged << "PNG"; }
    Check(!cache.Lookup(key, &hit), "truncated PNG rejected");
    const auto finalNewline = originalMeta.rfind('\n');
    const auto precedingNewline = originalMeta.rfind('\n', finalNewline - 1);
    Check(precedingNewline != std::string::npos, "checksum metadata fixture layout");
    const std::string forgedMeta = originalMeta.substr(0, precedingNewline + 1) +
                                   std::to_string(Checksum("PNG")) + "\n";
    { std::ofstream forged(meta, std::ios::binary | std::ios::trunc); forged << forgedMeta; }
    Check(!cache.Lookup(key, &hit), "invalid PNG rejected despite matching checksum");
    Check(cache.StoreAtomically(key, Image(), &artifact), "regenerate truncated PNG");
    { std::ofstream damaged(meta, std::ios::binary | std::ios::trunc); damaged << "bad metadata"; }
    Check(!cache.Lookup(key, &hit), "corrupt metadata rejected");
    Check(cache.StoreAtomically(key, Image(), &artifact), "regenerate metadata");
    Check(cache.Lookup(key, &hit), "recovered cache hit");

    std::ifstream original(meta, std::ios::binary);
    const std::string metadata((std::istreambuf_iterator<char>(original)),
                               std::istreambuf_iterator<char>());
    const auto checksumEnd = metadata.rfind('\n');
    const auto checksumStart = metadata.rfind('\n', checksumEnd - 1);
    Check(checksumStart != std::string::npos && checksumEnd > checksumStart + 1,
          "metadata fixture layout");
    auto wrongChecksum = metadata;
    wrongChecksum[checksumStart + 1] = wrongChecksum[checksumStart + 1] == '0' ? '1' : '0';
    { std::ofstream out(meta, std::ios::binary | std::ios::trunc); out << wrongChecksum; }
    Check(!cache.Lookup(key, &hit), "checksum mismatch rejected");
    { std::ofstream out(meta, std::ios::binary | std::ios::trunc); out << metadata; }
    Check(cache.Lookup(key, &hit), "checksum metadata restored");

    auto wrongDimension = metadata;
    const auto keyEnd = wrongDimension.find('\n', wrongDimension.find('\n') + 1);
    Check(keyEnd != std::string::npos && keyEnd + 1 < wrongDimension.size(),
          "dimension metadata fixture layout");
    wrongDimension[keyEnd + 1] = '9';
    { std::ofstream out(meta, std::ios::binary | std::ios::trunc); out << wrongDimension; }
    Check(!cache.Lookup(key, &hit), "dimension mismatch rejected");
    { std::ofstream out(meta, std::ios::binary | std::ios::trunc); out << metadata; }
    Check(cache.Lookup(key, &hit), "dimension metadata restored");

    auto badImage = Image();
    badImage.pixels.pop_back();
    Check(!cache.StoreAtomically(key, badImage, &hit), "short source rejected");
    badImage = Image();
    badImage.width = 65;
    Check(!cache.StoreAtomically(key, badImage, &hit), "oversized dimensions rejected");
    fs::remove_all(root);
  } catch (...) { fs::remove_all(root); throw; }
}

static void TestProcess(const std::string &mode, const fs::path &root) {
  PngArtifactCache cache(root, {64, 1024, 1024});
  const fs::path calls = root / "backend-calls";
  auto key = Key();
  fs::path artifact;
  if (mode == "write") {
    Check(!cache.Lookup(key, &artifact), "cold miss");
    FakeBackend backend;
    auto input = Image();
    const auto result = backend.Enhance({key.texture, input.View(), {}});
    Check(result.status == EnhancementResult::Status::Enhanced, "fake backend");
    Check(cache.StoreAtomically(key, result.image, &artifact), "cold store");
    std::ofstream counter(calls);
    counter << "1\n";
    Check(static_cast<bool>(counter), "count write");
  } else if (mode == "read") {
    Check(cache.Lookup(key, &artifact), "warm hit");
    png_image decoded{};
    decoded.version = PNG_IMAGE_VERSION;
    Check(png_image_begin_read_from_file(&decoded, artifact.c_str()) != 0,
          "warm PNG readable");
    Check(decoded.width == 4 && decoded.height == 2, "warm output dimensions");
    decoded.format = PNG_FORMAT_RGBA;
    std::vector<uint8_t> pixels(32);
    Check(png_image_finish_read(&decoded, nullptr, pixels.data(), 0, nullptr) != 0,
          "warm PNG decoded");
    png_image_free(&decoded);
    Check(pixels == std::vector<uint8_t>({
      1,2,3,4,1,2,3,4,5,6,7,8,5,6,7,8,
      1,2,3,4,1,2,3,4,5,6,7,8,5,6,7,8}), "warm pixels match cold output");
    std::ifstream counter(calls);
    int count = 0;
    counter >> count;
    Check(count == 1, "warm run has no backend invocation");
  } else throw std::runtime_error("unknown process mode");
}

int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "unit") TestUnit();
    else if (argc == 3) TestProcess(argv[1], argv[2]);
    else throw std::runtime_error("usage: cache-tests unit | write/read ROOT");
    std::cout << "cache test passed\n";
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
}
