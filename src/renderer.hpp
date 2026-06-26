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

// Encodes a string as a JSON string literal (with surrounding quotes).
std::string json_str(const std::string& s);

// Reader/edit config injected into the page as window._EDIT.
struct EditConfig {
  bool editable = false;
  std::string source_type;  // "txt" | "docx" | "pdf"
  std::string token;        // per-session token required by the edit API
};

// JSON fragments describing a set of paragraphs, in the reader's wire format.
//   paragraphs: a "[...]" array in window._P shape.
//   readings:   a "{...}" object in window._R shape (only words in paragraphs).
struct ParagraphsJson {
  std::string paragraphs;
  std::string readings;
};

// Builds window._P / window._R JSON for the given paragraphs. Used by the
// re-annotate API; edited paragraphs carry no audio, so no paths are emitted.
ParagraphsJson build_paragraphs_json(
    const std::vector<std::vector<AnnotatedWord>>& paragraphs);

// Each inner vector is one paragraph of annotated words.
// audio_map: word text -> relative WAV path (e.g. "audio/xxxx.wav")
std::string render_html(
    const std::vector<std::vector<AnnotatedWord>>& paragraphs,
    const std::map<std::string, std::string>& audio_map,
    const std::vector<int>& page_breaks, const EditConfig& edit);

// Simple, static HTML for wkhtmltopdf.
std::string render_pdf_html(
    const std::vector<std::vector<AnnotatedWord>>& paragraphs);
