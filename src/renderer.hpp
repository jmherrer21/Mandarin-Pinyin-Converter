#pragma once
#include <map>
#include <string>
#include <vector>

#include "dictionary.hpp"

struct AnnotatedWord {
  std::string text;
  std::string pinyin;  // empty if not in dict or not hanzi
  std::vector<std::string> definitions;
  std::vector<DictEntry> all_readings;  // all CEDICT entries
  bool is_hanzi = false;
};

std::vector<AnnotatedWord> annotate(const std::vector<std::string>& tokens,
                                    const Dictionary& dict);

// Each inner vector is one paragraph of annotated words.
// audio_map: word text -> relative WAV path (e.g. "audio/xxxx.wav")
std::string render_html(
    const std::vector<std::vector<AnnotatedWord>>& paragraphs,
    const std::map<std::string, std::string>& audio_map,
    const std::vector<int>& page_breaks);

// Simple, static HTML for wkhtmltopdf.
std::string render_pdf_html(
    const std::vector<std::vector<AnnotatedWord>>& paragraphs);
