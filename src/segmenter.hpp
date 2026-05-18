#pragma once
#include <string>
#include <vector>

#include "cppjieba/Jieba.hpp"

class Segmenter {
 public:
  explicit Segmenter(const std::string& dict_dir);
  std::vector<std::string> cut(const std::string& text) const;

 private:
  cppjieba::Jieba jieba_;
};
