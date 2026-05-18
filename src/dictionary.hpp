#pragma once
#include <string>
#include <vector>
#include <unordered_map>

struct DictEntry
{
    std::string traditional;
    std::string simplified;
    std::string pinyin;
    std::vector<std::string> definitions;
};

// keyed by simplified; may have multiple DictEntries per hanzi.
using Dictionary = std::unordered_map<std::string, std::vector<DictEntry>>;

Dictionary load_cedict(const std::string& path);
