#include "dictionary.hpp"
#include <fstream>
#include <sstream>

Dictionary load_cedict(const std::string& path)
{
    Dictionary dict;
    // ~124,000 entries in our dictionary.
    dict.reserve(125000);
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Cannot open dictionary: " + path);
    std::string line;

    // Example dictionary entry:
    // 癲狂 癫狂 [dian1 kuang2] /deranged/mad/cracked/zany/
    // Traditional Simplified [pinyin] /def1/def2/
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#')
            continue;

        auto bracket_open = line.find('[');
        auto bracket_close = line.find(']');
        auto slash_first = line.find('/');
        if (bracket_open == std::string::npos ||
            bracket_close == std::string::npos ||
            slash_first == std::string::npos)
            continue;

        std::string heads = line.substr(0, bracket_open - 1);
        std::istringstream hs(heads);
        DictEntry entry;
        hs >> entry.traditional >> entry.simplified;

        entry.pinyin = line.substr(bracket_open + 1, bracket_close - bracket_open - 1);

        // Get definitions.
        std::string defs = line.substr(slash_first + 1);
        std::string def;
        for (char c : defs)
        {
            if (c == '/')
            {
                if (!def.empty())
                {
                    entry.definitions.push_back(def);
                    def.clear();
                }
            }
            else
            {
                def += c;
            }
        }

        if (!entry.simplified.empty())
            dict[entry.simplified].push_back(std::move(entry));
    }

    return dict;
}
