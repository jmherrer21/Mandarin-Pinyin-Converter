#pragma once
#include <map>
#include <string>
#include <vector>

#include "renderer.hpp"

struct TtsConfig {
  std::string piper_exe;
  std::string model_path;
  std::string audio_dir;  // Per-document output dir. WAVs are placed here.
  std::string cache_dir;  // App-wide persistent cache.
};

// Pre-computes the audio filename map without running Piper.
// Allows embedding audio paths in HTML before TTS generation starts.
std::map<std::string, std::string> build_audio_map(
    const std::vector<std::vector<AnnotatedWord>>& paragraphs);

// Generates WAV files for each unique hanzi word.
std::map<std::string, std::string> generate_audio(
    const std::vector<std::vector<AnnotatedWord>>& paragraphs,
    const TtsConfig& cfg);
