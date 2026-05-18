#include "segmenter.hpp"

Segmenter::Segmenter(const std::string& dict_dir)
    : jieba_(dict_dir + "/jieba.dict.utf8", dict_dir + "/hmm_model.utf8",
             dict_dir + "/user.dict.utf8") {}

std::vector<std::string> Segmenter::cut(const std::string& text) const {
  std::vector<std::string> words;
  jieba_.Cut(text, words);
  return words;
}
