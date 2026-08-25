#include "Logic/BlockHitAudio.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(expression)                                                       \
  do {                                                                          \
    ++g_checks;                                                                 \
    if (!(expression)) {                                                        \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,           \
                   #expression);                                                \
      ++g_failures;                                                             \
    }                                                                           \
  } while (false)

void appendU16(std::vector<std::uint8_t> &bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void appendU32(std::vector<std::uint8_t> &bytes, std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void appendTag(std::vector<std::uint8_t> &bytes, const char (&tag)[5]) {
  bytes.insert(bytes.end(), tag, tag + 4);
}

void writeU32(std::vector<std::uint8_t> &bytes, std::size_t offset,
              std::uint32_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value);
  bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
  bytes[offset + 2] = static_cast<std::uint8_t>(value >> 16U);
  bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24U);
}

std::vector<std::uint8_t> makeWav(std::uint16_t encoding = 1U,
                                  std::uint16_t channels = 1U,
                                  std::uint32_t sampleRate = 44100U,
                                  std::uint16_t bits = 16U,
                                  std::uint32_t frames = 441U,
                                  bool includeData = true) {
  std::vector<std::uint8_t> bytes;
  appendTag(bytes, "RIFF");
  appendU32(bytes, 0U);
  appendTag(bytes, "WAVE");
  appendTag(bytes, "fmt ");
  appendU32(bytes, 16U);
  appendU16(bytes, encoding);
  appendU16(bytes, channels);
  appendU32(bytes, sampleRate);
  const std::uint16_t blockAlign =
      static_cast<std::uint16_t>(channels * (bits / 8U));
  appendU32(bytes, sampleRate * blockAlign);
  appendU16(bytes, blockAlign);
  appendU16(bytes, bits);
  if (includeData) {
    appendTag(bytes, "data");
    const std::uint32_t dataSize = frames * blockAlign;
    appendU32(bytes, dataSize);
    bytes.resize(bytes.size() + dataSize, 0x12U);
    if ((dataSize & 1U) != 0U) bytes.push_back(0U);
  }
  writeU32(bytes, 4U, static_cast<std::uint32_t>(bytes.size() - 8U));
  return bytes;
}

void testDefaultsAndVolume() {
  using namespace BlockHitAudio;
  CHECK(!kDefaultFeatureEnabled);
  CHECK(!kDefaultDebugLoggingEnabled);
  CHECK(kDefaultVolumePercent == 22.0f);
  CHECK(kDefaultCustomFilename == "block-hit.wav");
  CHECK(parseSoundSource("Default") == SoundSource::Default);

  CHECK(sanitizeVolumePercent(-20.0f) == 0.0f);
  CHECK(sanitizeVolumePercent(0.0f) == 0.0f);
  CHECK(sanitizeVolumePercent(37.5f) == 37.5f);
  CHECK(sanitizeVolumePercent(100.0f) == 100.0f);
  CHECK(sanitizeVolumePercent(180.0f) == 100.0f);
  CHECK(sanitizeVolumePercent(std::numeric_limits<float>::quiet_NaN()) ==
        kDefaultVolumePercent);
  CHECK(sanitizeVolumePercent(std::numeric_limits<float>::infinity()) ==
        kDefaultVolumePercent);
  CHECK(sanitizeVolumePercent(-std::numeric_limits<float>::infinity()) ==
        kDefaultVolumePercent);
}

void testSourceAndFilenameParsing() {
  using namespace BlockHitAudio;
  CHECK(parseSoundSource("Custom") == SoundSource::Custom);
  CHECK(parseSoundSource("custom") == SoundSource::Custom);
  CHECK(parseSoundSource("CUSTOM") == SoundSource::Custom);
  CHECK(parseSoundSource("garbage") == SoundSource::Default);
  CHECK(soundSourceName(SoundSource::Default) == std::string("Default"));
  CHECK(soundSourceName(SoundSource::Custom) == std::string("Custom"));

  CHECK(isSafeWavFilename("hit.wav"));
  CHECK(isSafeWavFilename("My Block Hit.WAV"));
  CHECK(!isSafeWavFilename(""));
  CHECK(!isSafeWavFilename(".wav"));
  CHECK(!isSafeWavFilename("../hit.wav"));
  CHECK(!isSafeWavFilename("folder\\hit.wav"));
  CHECK(!isSafeWavFilename("C:hit.wav"));
  CHECK(!isSafeWavFilename("hit.ogg"));
  CHECK(!isSafeWavFilename(std::string(129U, 'a') + ".wav"));
}

void testWavValidation() {
  using namespace BlockHitAudio;
  WavInfo info;
  const auto valid = makeWav();
  CHECK(validatePcmWav(valid, info) == WavError::None);
  CHECK(info.channels == 1U);
  CHECK(info.sampleRate == 44100U);
  CHECK(info.bitsPerSample == 16U);
  CHECK(info.dataSize == 882U);
  CHECK(info.durationMs == 10U);

  std::vector<std::uint8_t> shortHeader(valid.begin(), valid.begin() + 10);
  CHECK(validatePcmWav(shortHeader, info) == WavError::Truncated);

  auto truncatedData = valid;
  truncatedData.pop_back();
  CHECK(validatePcmWav(truncatedData, info) == WavError::Truncated);

  auto invalidContainer = valid;
  invalidContainer[0] = 'N';
  CHECK(validatePcmWav(invalidContainer, info) == WavError::InvalidContainer);

  CHECK(validatePcmWav(makeWav(3U), info) == WavError::UnsupportedEncoding);
  CHECK(validatePcmWav(makeWav(1U, 3U), info) ==
        WavError::UnsupportedChannels);
  CHECK(validatePcmWav(makeWav(1U, 1U, 44100U, 8U), info) ==
        WavError::UnsupportedBitDepth);
  CHECK(validatePcmWav(makeWav(1U, 1U, 44100U, 16U, 441U, false), info) ==
        WavError::MissingDataChunk);

  const std::uint32_t tooLongFrames = 44100U * 16U;
  CHECK(validatePcmWav(makeWav(1U, 1U, 44100U, 16U, tooLongFrames), info) ==
        WavError::TooLong);

  std::vector<std::uint8_t> oversized(kMaximumWavFileBytes + 1U, 0U);
  CHECK(validatePcmWav(oversized, info) == WavError::Oversized);
}

void testCacheAndReload() {
  using namespace BlockHitAudio;
  WavCache cache;
  CHECK(cache.reload("missing.wav", std::nullopt) == WavError::MissingFile);
  CHECK(!cache.ready());
  CHECK(cache.filename() == "missing.wav");

  CHECK(cache.reload("..\\escape.wav", makeWav()) ==
        WavError::UnsafeFilename);
  CHECK(!cache.ready());

  auto first = makeWav(1U, 1U, 22050U, 16U, 220U);
  CHECK(cache.reload("hit.wav", first) == WavError::None);
  CHECK(cache.ready());
  CHECK(cache.info().sampleRate == 22050U);
  const std::size_t firstSize = cache.info().dataSize;

  auto replacement = makeWav(1U, 2U, 48000U, 16U, 960U);
  CHECK(cache.reload("hit.wav", replacement) == WavError::None);
  CHECK(cache.ready());
  CHECK(cache.info().sampleRate == 48000U);
  CHECK(cache.info().channels == 2U);
  CHECK(cache.info().dataSize != firstSize);

  auto brokenReplacement = replacement;
  brokenReplacement.resize(20U);
  CHECK(cache.reload("hit.wav", brokenReplacement) == WavError::Truncated);
  CHECK(!cache.ready());
}

void testPlaybackPolicy() {
  using namespace BlockHitAudio;
  CHECK(choosePlayback(false, false, SoundSource::Default, false, 22.0f) ==
        PlaybackTarget::None);
  CHECK(choosePlayback(true, false, SoundSource::Default, false, 22.0f) ==
        PlaybackTarget::DefaultMinecraft);
  CHECK(choosePlayback(true, false, SoundSource::Custom, true, 22.0f) ==
        PlaybackTarget::CustomWav);
  CHECK(choosePlayback(true, false, SoundSource::Custom, false, 22.0f) ==
        PlaybackTarget::DefaultMinecraft);
  CHECK(choosePlayback(true, false, SoundSource::Custom, true, 0.0f) ==
        PlaybackTarget::None);

  // Preview is deliberately independent of the detector/module enable state.
  CHECK(choosePlayback(false, true, SoundSource::Default, false, 22.0f) ==
        PlaybackTarget::DefaultMinecraft);
  CHECK(choosePlayback(false, true, SoundSource::Custom, true, 22.0f) ==
        PlaybackTarget::CustomWav);
  CHECK(choosePlayback(false, true, SoundSource::Custom, false, 22.0f) ==
        PlaybackTarget::DefaultMinecraft);
}

} // namespace

int main() {
  testDefaultsAndVolume();
  testSourceAndFilenameParsing();
  testWavValidation();
  testCacheAndReload();
  testPlaybackPolicy();

  if (g_failures != 0) {
    std::fprintf(stderr, "%d of %d audio checks failed\n", g_failures,
                 g_checks);
    return 1;
  }
  std::printf("All %d block-hit audio checks passed\n", g_checks);
  return 0;
}
