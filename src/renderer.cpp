#include "renderer.hpp"

#include <cstdint>
#include <map>
#include <sstream>

static const std::pair<uint32_t, uint32_t> hanzi_unicode[] = {
    {0x4E00, 0x9FFF},    // CJK Unified Ideographs
    {0x3400, 0x4DBF},    // CJK Extension A
    {0x20000, 0x2A6DF},  // CJK Extension B
    {0x2A700, 0x2B73F},  // CJK Extension C
    {0x2B740, 0x2B81F},  // CJK Extension D
    {0x2B820, 0x2CEAF},  // CJK Extension E
    {0x2CEB0, 0x2EBEF},  // CJK Extension F
    {0x2EBF0, 0x2EE5F},  // CJK Extension I
    {0x30000, 0x3134F},  // CJK Extension G
    {0x31350, 0x323AF},  // CJK Extension H
    {0xF900, 0xFAFF},    // CJK Compatibility Ideographs
};

static bool has_hanzi(const std::string& s) {
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = (unsigned char)s[i];
    uint32_t cp = 0;
    size_t len = 1;
    if (c < 0x80) {
      cp = c;
      len = 1;
    } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
      cp = ((c & 0x1F) << 6) | ((unsigned char)s[i + 1] & 0x3F);
      len = 2;
    } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
      cp = ((c & 0x0F) << 12) | (((unsigned char)s[i + 1] & 0x3F) << 6) |
           ((unsigned char)s[i + 2] & 0x3F);
      len = 3;
    } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
      cp = ((c & 0x07) << 18) | (((unsigned char)s[i + 1] & 0x3F) << 12) |
           (((unsigned char)s[i + 2] & 0x3F) << 6) |
           ((unsigned char)s[i + 3] & 0x3F);
      len = 4;
    } else {
      ++i;
      continue;
    }
    for (const auto& range : hanzi_unicode) {
      if (cp >= range.first && cp <= range.second) return true;
    }
    i += len;
  }
  return false;
}

static std::string html_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        out += static_cast<char>(c);
    }
  }
  return out;
}

std::string json_str(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    if (c == '"')
      out += "\\\"";
    else if (c == '\\')
      out += "\\\\";
    else if (c == '\n')
      out += "\\n";
    else if (c == '\r')
      out += "\\r";
    else if (c == '\t')
      out += "\\t";
    else
      out += static_cast<char>(c);
  }
  out += '"';
  return out;
}

static std::vector<std::string> split_utf8_chars(const std::string& s) {
  std::vector<std::string> chars;
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = (unsigned char)s[i];
    size_t len = 1;
    if (c < 0x80)
      len = 1;
    else if ((c & 0xE0) == 0xC0)
      len = 2;
    else if ((c & 0xF0) == 0xE0)
      len = 3;
    else if ((c & 0xF8) == 0xF0)
      len = 4;
    chars.push_back(s.substr(i, len));
    i += len;
  }
  return chars;
}

std::vector<AnnotatedWord> annotate(const std::vector<std::string>& tokens,
                                    const Dictionary& dict) {
  std::vector<AnnotatedWord> result;
  result.reserve(tokens.size());
  for (const auto& token : tokens) {
    AnnotatedWord w;
    w.text = token;
    w.is_hanzi = has_hanzi(token);
    if (!w.is_hanzi) {
      result.push_back(std::move(w));
      continue;
    }
    auto it = dict.find(token);
    if (it != dict.end()) {
      w.pinyin = it->second[0].pinyin;
      w.definitions = it->second[0].definitions;
      w.all_readings = it->second;
      result.push_back(std::move(w));
      continue;
    }
    // Fall back to per-character lookup when multi-character fails.
    auto chars = split_utf8_chars(token);
    if (chars.size() <= 1) {
      result.push_back(std::move(w));
      continue;
    }
    for (const auto& ch : chars) {
      AnnotatedWord cw;
      cw.text = ch;
      cw.is_hanzi = has_hanzi(ch);
      if (cw.is_hanzi) {
        auto cit = dict.find(ch);
        if (cit != dict.end()) {
          cw.pinyin = cit->second[0].pinyin;
          cw.definitions = cit->second[0].definitions;
          cw.all_readings = cit->second;
        }
      }
      result.push_back(std::move(cw));
    }
  }
  // Merge single token hanzi if a compound hanzi exists (e.g. ["否","定"] →
  // "否定 [fou3 ding4]" instead of 否定 [pi3, ding4]).
  std::vector<AnnotatedWord> merged;
  merged.reserve(result.size());
  for (size_t i = 0; i < result.size();) {
    bool remapped = false;
    if (result[i].is_hanzi && split_utf8_chars(result[i].text).size() == 1) {
      for (size_t len = 4; len >= 2 && !remapped; --len) {
        if (i + len > result.size()) continue;
        std::string combined;
        bool all_single_hanzi = true;
        for (size_t j = i; j < i + len; ++j) {
          if (!result[j].is_hanzi ||
              split_utf8_chars(result[j].text).size() != 1) {
            all_single_hanzi = false;
            break;
          }
          combined += result[j].text;
        }
        if (!all_single_hanzi) continue;
        auto it = dict.find(combined);
        if (it != dict.end()) {
          AnnotatedWord cw;
          cw.text = combined;
          cw.is_hanzi = true;
          cw.pinyin = it->second[0].pinyin;
          cw.definitions = it->second[0].definitions;
          cw.all_readings = it->second;
          merged.push_back(std::move(cw));
          i += len;
          remapped = true;
        }
      }
    }
    if (!remapped) {
      merged.push_back(std::move(result[i]));
      ++i;
    }
  }
  return merged;
}

static std::string apply_tone(std::string syl, int tone) {
  // u: in CEDICT represents ü; replace with placeholder 'v'
  std::string s;
  for (size_t i = 0; i < syl.size(); i++) {
    if (syl[i] == 'u' && i + 1 < syl.size() && syl[i + 1] == ':') {
      s += 'v';
      ++i;
    } else
      s += syl[i];
  }

  static const char* M[6][4] = {
      {"\xC4\x81", "\xC3\xA1", "\xC7\x8E", "\xC3\xA0"},  // ā á ǎ à
      {"\xC4\x93", "\xC3\xA9", "\xC4\x9B", "\xC3\xA8"},  // ē é ě è
      {"\xC4\xAB", "\xC3\xAD", "\xC7\x90", "\xC3\xAC"},  // ī í ǐ ì
      {"\xC5\x8D", "\xC3\xB3", "\xC7\x92", "\xC3\xB2"},  // ō ó ǒ ò
      {"\xC5\xAB", "\xC3\xBA", "\xC7\x94", "\xC3\xB9"},  // ū ú ǔ ù
      {"\xC7\x96", "\xC7\x98", "\xC7\x9A", "\xC7\x9C"},  // ǖ ǘ ǚ ǜ
  };
  static const char* UU = "\xC3\xBC";  // ü (neutral)

  int pos = -1, vi = -1;
  if (tone >= 1 && tone <= 4) {
    // a or e
    for (int i = 0; i < static_cast<int>(s.size()) && pos == -1; i++) {
      if (s[i] == 'a') {
        pos = i;
        vi = 0;
      } else if (s[i] == 'e') {
        pos = i;
        vi = 1;
      }
    }
    // ou -> o
    if (pos == -1)
      for (int i = 0; i + 1 < static_cast<int>(s.size()); i++)
        if (s[i] == 'o' && s[i + 1] == 'u') {
          pos = i;
          vi = 3;
          break;
        }
    // last vowel
    if (pos == -1)
      for (int i = static_cast<int>(s.size()) - 1; i >= 0; i--) {
        if (s[i] == 'i') {
          pos = i;
          vi = 2;
          break;
        } else if (s[i] == 'o') {
          pos = i;
          vi = 3;
          break;
        } else if (s[i] == 'u') {
          pos = i;
          vi = 4;
          break;
        } else if (s[i] == 'v') {
          pos = i;
          vi = 5;
          break;
        }
      }
  }

  std::string out;
  for (int i = 0; i < static_cast<int>(s.size()); i++) {
    if (i == pos)
      out += M[vi][tone - 1];
    else if (s[i] == 'v')
      out += UU;
    else
      out += s[i];
  }
  return out;
}

static std::string pinyin_to_accented(const std::string& numbered) {
  std::string result;
  std::istringstream iss(numbered);
  std::string syl;
  bool first = true;
  while (iss >> syl) {
    if (!first) result += ' ';
    first = false;
    int tone = 0;
    if (!syl.empty() && syl.back() >= '1' && syl.back() <= '5') {
      tone = syl.back() - '0';
      syl.pop_back();
    }
    result += apply_tone(syl, tone);
  }
  return result;
}

static std::string readings_to_json(const std::vector<DictEntry>& readings) {
  std::string out = "[";
  for (size_t i = 0; i < readings.size(); ++i) {
    if (i) out += ',';
    out += "{\"py\":" + json_str(pinyin_to_accented(readings[i].pinyin));
    out += ",\"defs\":[";
    for (size_t j = 0; j < readings[i].definitions.size(); ++j) {
      if (j) out += ',';
      out += json_str(readings[i].definitions[j]);
    }
    out += "]}";
  }
  out += ']';
  return out;
}

// Builds the window._P-shaped "[...]" array for paragraphs, accumulating any
// readings encountered into readings_dict (word -> readings JSON).
static std::string paragraphs_array_js(
    const std::vector<std::vector<AnnotatedWord>>& paragraphs,
    const std::map<std::string, std::string>& audio_map,
    std::map<std::string, std::string>& readings_dict) {
  std::string out = "[";
  bool fp = true;
  for (const auto& para : paragraphs) {
    if (!fp) out += ',';
    fp = false;
    out += '[';
    bool fw = true;
    for (const auto& w : para) {
      if (!fw) out += ',';
      fw = false;
      if (!w.is_hanzi || w.pinyin.empty()) {
        out += json_str(w.text);
      } else {
        std::string py = pinyin_to_accented(w.pinyin);
        if (!readings_dict.count(w.text))
          readings_dict[w.text] = readings_to_json(w.all_readings);
        auto ait = audio_map.find(w.text);
        out += '[' + json_str(w.text) + ',' + json_str(py);
        if (ait != audio_map.end()) out += ',' + json_str(ait->second);
        out += ']';
      }
    }
    out += ']';
  }
  out += ']';
  return out;
}

// Builds the window._R-shaped "{...}" object from an accumulated readings map.
static std::string readings_object_js(
    const std::map<std::string, std::string>& readings_dict) {
  std::string out = "{";
  bool fe = true;
  for (const auto& [word, json] : readings_dict) {
    if (!fe) out += ',';
    fe = false;
    out += json_str(word) + ':' + json;
  }
  out += '}';
  return out;
}

ParagraphsJson build_paragraphs_json(
    const std::vector<std::vector<AnnotatedWord>>& paragraphs) {
  std::map<std::string, std::string> readings_dict;
  ParagraphsJson r;
  r.paragraphs = paragraphs_array_js(paragraphs, {}, readings_dict);
  r.readings = readings_object_js(readings_dict);
  return r;
}

std::string render_html(
    const std::vector<std::vector<AnnotatedWord>>& paragraphs,
    const std::map<std::string, std::string>& audio_map,
    const std::vector<int>& page_breaks, const EditConfig& edit) {
  // Build compact paragraph data (window._P), deduplicated readings
  // (window._R), and PDF page-break indices (window._PB) in one pass. The JS
  // paginator uses _PB to show exactly one PDF page at a time.
  //
  // window._P format — array of paragraphs; each paragraph is an array of
  // tokens:
  //   non-hanzi: JSON string
  //   hanzi:     [text, pinyin] or [text, pinyin, audio]
  // window._R format — { word: readings_array } looked up at click time.
  // window._PB format — [0, 5, 12, ...] index into _P of first para on each PDF
  // page.
  std::map<std::string, std::string> readings_dict;
  std::string paras_js =
      "window._P=" + paragraphs_array_js(paragraphs, audio_map, readings_dict) +
      ";";
  std::string readings_js =
      "window._R=" + readings_object_js(readings_dict) + ";";

  // page-break index array; guarantee at least one entry.
  std::string pb_js = "window._PB=[";
  if (page_breaks.empty()) {
    pb_js += "0";
  } else {
    for (size_t i = 0; i < page_breaks.size(); ++i) {
      if (i) pb_js += ',';
      pb_js += std::to_string(page_breaks[i]);
    }
  }
  pb_js += "];";

  // Reader/edit config (window._EDIT). Carries the per-session token the edit
  // API requires; the edit UI itself is added in a later stage.
  std::string edit_js = "window._EDIT={editable:";
  edit_js += edit.editable ? "true" : "false";
  edit_js += ",sourceType:" + json_str(edit.source_type);
  edit_js += ",token:" + json_str(edit.token) + "};";

  std::string html;
  html += "<!DOCTYPE html>\n<html lang=\"zh-Hans\">\n<head>\n";
  html += "<meta charset=\"UTF-8\">\n<title>Mandarin Reader</title>\n";
  html += "<style>\n";
  html +=
      "body{font-family:sans-serif;font-size:1.3em;line-height:1.8em;"
      "max-width:900px;margin:1.0em auto;padding:0 1em;}\n";
  html += "p{margin-bottom:2.0em;}\n";
  html += ".word{cursor:pointer;}\n";
  html += ".word ruby:hover{background:#e8f4fd;border-radius:3px;}\n";
  html += "rt{font-size:0.5em;color:#555;}\n";
  html +=
      "#popup{display:none;position:fixed;background:#fff;"
      "border:1px solid #ccc;border-radius:6px;padding:.8em 1em;"
      "box-shadow:0 4px 14px rgba(0,0,0,.18);max-width:340px;z-index:1000;}\n";
  html += "#popup h3{margin:0 0 .25em;}\n";
  html +=
      "#popup .py{color:#666;font-style:italic;margin:0 0 "
      ".5em;font-size:.9em;}\n";
  html += "#popup ul{margin:0;padding-left:1.2em;font-size:.82em;}\n";
  html += "#popup li{margin-bottom:.2em;}\n";
  html +=
      "#play-btn{display:none;margin-bottom:.5em;cursor:pointer;"
      "font-size:.9em;padding:.2em .6em;}\n";
  html +=
      "#nav{margin-bottom:1.5em;display:flex;align-items:center;gap:.8em;font-"
      "size:.85em;}\n";
  html += "#nav button{padding:.25em .7em;cursor:pointer;}\n";
  html +=
      "#nav input[type=number]{width:4em;text-align:center;padding:.1em "
      ".2em;}\n";
  html += "#pg-tot{color:#555;}\n";
  html += "#speed-lbl{color:#555;margin-left:.5em;}\n";
  html += "#speed{width:80px;vertical-align:middle;}\n";
  html += "#speed-val{display:inline-block;min-width:3.5em;}\n";
  html +=
      ".para-play{cursor:pointer;font-size:.7em;vertical-align:middle;"
      "margin-right:.4em;padding:.1em .35em;opacity:.6;border:1px solid #ccc;"
      "border-radius:3px;background:#f5f5f5;}\n";
  html += ".para-play:hover{opacity:1;background:#e8f4fd;}\n";
  html += "#stop-btn{padding:.25em .35em;cursor:pointer;}\n";
  html += "</style>\n";
  html +=
      "<script>" + paras_js + readings_js + pb_js + edit_js + "</script>\n";
  html += "</head>\n<body>\n";
  html +=
      "<div id=\"nav\">"
      "<button id=\"prev\">&#8592; Prev</button>"
      "Page <input id=\"pg-in\" type=\"number\" min=\"1\"> of <span "
      "id=\"pg-tot\"></span>"
      "<button id=\"next\">Next &#8594;</button>"
      "<label id=\"speed-lbl\">Speed: "
      "<input id=\"speed\" type=\"range\" min=\"0.5\" max=\"1.5\" "
      "step=\"0.05\" value=\"0.85\">"
      " <span id=\"speed-val\">0.85&#215;</span></label>"
      "<button id=\"stop-btn\" onclick=\"_stopAudio()\">&#9646;&#9646;</button>"
      "</div>\n";
  html += "<div id=\"content\"></div>\n";
  html +=
      "<div id=\"popup\">"
      "<h3 id=\"pw\"></h3>"
      "<p class=\"py\" id=\"pp\"></p>"
      "<button id=\"play-btn\">&#9654; Play</button>"
      "<ul id=\"pd\"></ul>"
      "</div>\n";
  html += "<script>\n";
  html +=
      "var _zhVoice=null;\n"
      "var _speed=0.85;\n"
      "var _curAudio=null;\n"
      "var _stopped=false;\n"
      "document.getElementById('speed').addEventListener('input',function(){\n"
      "  _speed=parseFloat(this.value);\n"
      "  "
      "document.getElementById('speed-val').textContent=_speed.toFixed(2)+'×';"
      "\n"
      "});\n"
      "function _stopAudio(){\n"
      "  _stopped=true;\n"
      "  if(_curAudio){_curAudio.pause();_curAudio=null;}\n"
      "  if(window.speechSynthesis)window.speechSynthesis.cancel();\n"
      "}\n"
      "function _loadVoices(){\n"
      "  var vs=window.speechSynthesis.getVoices();\n"
      "  for(var i=0;i<vs.length;i++){\n"
      "    if(vs[i].lang.indexOf('zh')===0){_zhVoice=vs[i];break;}\n"
      "  }\n"
      "}\n"
      "_loadVoices();\n"
      "if(window.speechSynthesis)window.speechSynthesis.addEventListener('"
      "voiceschanged',_loadVoices);\n"
      "function _speakZh(text){\n"
      "  var u=new SpeechSynthesisUtterance(text);\n"
      "  u.lang='zh-CN';\n"
      "  if(_zhVoice)u.voice=_zhVoice;\n"
      "  u.rate=_speed;\n"
      "  window.speechSynthesis.cancel();\n"
      "  window.speechSynthesis.speak(u);\n"
      "}\n";
  html +=
      "function _readPara(pi){\n"
      "  _stopped=false;\n"
      "  var p=window._P[pi],paths=[],seen={},txt='';\n"
      "  for(var i=0;i<p.length;i++){\n"
      "    var w=p[i];\n"
      "    if(typeof w==='string'){txt+=w;continue;}\n"
      "    txt+=w[0];\n"
      "    if(w[2]&&!seen[w[2]]){seen[w[2]]=true;paths.push(w[2]);}\n"
      "  }\n"
      "  if(paths.length>0){\n"
      "    var ai=0,anyPlayed=false;\n"
      "    (function next(){\n"
      "      if(_stopped)return;\n"
      "      if(ai>=paths.length){if(!anyPlayed)_speakZh(txt);return;}\n"
      "      var a=new Audio(paths[ai++]);a.playbackRate=_speed;\n"
      "      _curAudio=a;\n"
      "      var adv=false;\n"
      "      function safe(){if(!adv){adv=true;if(!_stopped)next();}}\n"
      "      a.onended=function(){anyPlayed=true;safe();};\n"
      "      a.onerror=safe;a.play().catch(safe);\n"
      "    })();\n"
      "  }else{\n"
      "    _speakZh(txt);\n"
      "  }\n"
      "}\n";
  html +=
      "var _cur=0;\n"
      "function _esc(s){return "
      "String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/"
      "g,'&gt;').replace(/\"/g,'&quot;');}\n"
      "function _render(){\n"
      "  var PB=window._PB,tot=PB.length;\n"
      "  var s=PB[_cur],e=(_cur+1<tot)?PB[_cur+1]:window._P.length,h='';\n"
      "  for(var i=s;i<e;i++){\n"
      "    h+='<p><button class=\"para-play\" "
      "onclick=\"_readPara('+i+')\">&#9654;</button>';\n"
      "    var p=window._P[i];\n"
      "    for(var j=0;j<p.length;j++){\n"
      "      var w=p[j];\n"
      "      if(typeof w==='string'){h+=_esc(w);}\n"
      "      else{\n"
      "        h+='<span class=\"word\" data-word=\"'+_esc(w[0])+'\" "
      "data-pinyin=\"'+_esc(w[1])+'\"';\n"
      "        if(w[2])h+=' data-audio=\"'+_esc(w[2])+'\"';\n"
      "        "
      "h+='><ruby>'+_esc(w[0])+'<rt>'+_esc(w[1])+'</rt></ruby></span>';\n"
      "      }\n"
      "    }\n"
      "    h+='</p>';\n"
      "  }\n"
      "  document.getElementById('content').innerHTML=h;\n"
      "  var "
      "pgIn=document.getElementById('pg-in');pgIn.value=_cur+1;pgIn.max=tot;\n"
      "  document.getElementById('pg-tot').textContent=tot;\n"
      "  document.getElementById('prev').disabled=(_cur===0);\n"
      "  document.getElementById('next').disabled=(_cur>=tot-1);\n"
      "  window.scrollTo(0,0);\n"
      "}\n"
      "document.getElementById('prev').addEventListener('click',function(){if(_"
      "cur>0){_cur--;_render();}});\n"
      "document.getElementById('next').addEventListener('click',function(){\n"
      "  if(_cur<window._PB.length-1){_cur++;_render();}\n"
      "});\n"
      "_render();\n"
      "document.getElementById('pg-in').addEventListener('change',function(){\n"
      "  var tot=window._PB.length;\n"
      "  var v=parseInt(this.value,10);\n"
      "  v=Math.max(1,Math.min(tot,isNaN(v)?1:v));\n"
      "  _cur=v-1;_render();\n"
      "});\n";
  html +=
      "document.getElementById('content').addEventListener('click',function(e){"
      "\n"
      "  "
      "if(window.getSelection&&window.getSelection().toString().length>0)"
      "return;\n"
      "  var el=e.target.closest('.word');\n"
      "  if(!el)return;\n"
      "  var pop=document.getElementById('popup');\n"
      "  document.getElementById('pw').textContent=el.dataset.word;\n"
      "  var readings=window._R[el.dataset.word]||[];\n"
      "  document.getElementById('pp').textContent=readings.length>1\n"
      "    ?readings.map(function(r){return r.py;}).join(' / ')\n"
      "    :el.dataset.pinyin;\n"
      "  var ul=document.getElementById('pd');\n"
      "  ul.innerHTML='';\n"
      "  if(readings.length===1){\n"
      "    readings[0].defs.forEach(function(d){\n"
      "      var li=document.createElement('li');\n"
      "      li.textContent=d;\n"
      "      ul.appendChild(li);\n"
      "    });\n"
      "  }else{\n"
      "    readings.forEach(function(r){\n"
      "      var li=document.createElement('li');\n"
      "      var s=document.createElement('strong');s.textContent=r.py;\n"
      "      li.appendChild(s);\n"
      "      li.appendChild(document.createTextNode(': '+r.defs.join(' / "
      "')));\n"
      "      ul.appendChild(li);\n"
      "    });\n"
      "  }\n"
      "  var btn=document.getElementById('play-btn');\n"
      "  btn.style.display='inline-block';\n"
      "  if(el.dataset.audio){\n"
      "    btn.onclick=function(){_stopped=false;var a=new "
      "Audio(el.dataset.audio);"
      "_curAudio=a;"
      "var "
      "_f=false,_fb=function(){if(!_f){_f=true;if(!_stopped)_speakZh(el."
      "dataset.word);}};"
      "a.playbackRate=_speed;a.onerror=_fb;a.play().catch(_fb);};\n"
      "  } else {\n"
      "    btn.onclick=function(){_stopped=false;_speakZh(el.dataset.word);};\n"
      "  }\n"
      "  var x=Math.min(e.clientX+12,window.innerWidth-360);\n"
      "  var y=Math.min(e.clientY+12,window.innerHeight-260);\n"
      "  pop.style.left=x+'px';\n"
      "  pop.style.top=y+'px';\n"
      "  pop.style.display='block';\n"
      "  e.stopPropagation();\n"
      "});\n"
      "document.addEventListener('click',function(){\n"
      "  document.getElementById('popup').style.display='none';\n"
      "});\n"
      "document.addEventListener('keydown',function(e){\n"
      "  "
      "if(e.key==='Escape')document.getElementById('popup').style.display='"
      "none';\n"
      "});\n";
  html +=
      "(function(){\n"
      "  var selWords=[];\n"
      "  var menu=document.createElement('div');\n"
      "  "
      "menu.style.cssText='display:none;position:fixed;background:#fff;border:"
      "1px solid #ccc;"
      "border-radius:6px;padding:.3em 0;"
      "box-shadow:0 4px 14px rgba(0,0,0,.18);z-index:2000;';\n"
      "  var item=document.createElement('div');\n"
      "  item.style.cssText='padding:.4em "
      "1em;font-size:.9em;white-space:nowrap;cursor:pointer;';\n"
      "  item.textContent='\\u25B6 Play pronunciation';\n"
      "  menu.appendChild(item);\n"
      "  document.body.appendChild(menu);\n"
      "  "
      "item.addEventListener('mouseenter',function(){item.style.background='#"
      "e8f4fd';});\n"
      "  "
      "item.addEventListener('mouseleave',function(){item.style.background='';}"
      ");\n"
      "  document.addEventListener('contextmenu',function(e){\n"
      "    selWords=[];\n"
      "    var s=window.getSelection();\n"
      "    if(!s||!s.rangeCount)return;\n"
      "    var range=s.getRangeAt(0);\n"
      "    var frag=range.cloneContents();\n"
      "    var tmp=document.createElement('div');\n"
      "    tmp.appendChild(frag);\n"
      "    tmp.querySelectorAll('rt').forEach(function(rt){rt.remove();});\n"
      "    if(!tmp.textContent.trim())return;\n"
      "    selWords=Array.from(document.querySelectorAll('.word[data-pinyin]'))"
      ".filter(function(w){return range.intersectsNode(w);});\n"
      "    if(!selWords.length)return;\n"
      "    e.preventDefault();\n"
      "    menu.style.left=Math.min(e.clientX,window.innerWidth-200)+'px';\n"
      "    menu.style.top=Math.min(e.clientY,window.innerHeight-60)+'px';\n"
      "    menu.style.display='block';\n"
      "  });\n"
      "  item.addEventListener('click',function(e){\n"
      "    menu.style.display='none';\n"
      "    _stopped=false;\n"
      "    var _seen={};var "
      "audioWords=selWords.filter(function(w){if(!w.dataset.audio||_seen[w."
      "dataset.audio])return false;_seen[w.dataset.audio]=true;return "
      "true;});\n"
      "    if(audioWords.length>0){\n"
      "      var idx=0,anyPlayed=false;\n"
      "      (function next(){\n"
      "        if(_stopped)return;\n"
      "        if(idx>=audioWords.length){\n"
      "          if(!anyPlayed&&selWords.length>0)"
      "_speakZh(selWords.map(function(w){return w.dataset.word;}).join(''));\n"
      "          return;}\n"
      "        var a=new Audio(audioWords[idx++].dataset.audio),adv=false;\n"
      "        _curAudio=a;\n"
      "        function safe(){if(!adv){adv=true;if(!_stopped)next();}}\n"
      "        a.onended=function(){anyPlayed=true;safe();};\n"
      "        "
      "a.playbackRate=_speed;a.onerror=safe;a.play().catch(safe);})();\n"
      "    } else if(selWords.length>0){\n"
      "      _speakZh(selWords.map(function(w){return "
      "w.dataset.word;}).join(''));\n"
      "    }\n"
      "    e.stopPropagation();\n"
      "  });\n"
      "  "
      "document.addEventListener('click',function(){menu.style.display='none';}"
      ");\n"
      "  document.addEventListener('keydown',function(e){\n"
      "    if(e.key==='Escape')menu.style.display='none';\n"
      "  });\n"
      "})();\n";
  html += "</script>\n</body>\n</html>\n";
  return html;
}

std::string render_pdf_html(
    const std::vector<std::vector<AnnotatedWord>>& paragraphs) {
  std::string html;
  html += "<!DOCTYPE html>\n<html lang=\"zh-Hans\">\n<head>\n";
  html += "<meta charset=\"UTF-8\">\n";
  html += "<style>\n";
  html +=
      "body{font-family:'SimSun','STSong',serif;font-size:13pt;"
      "line-height:2.8em;margin:1.5cm 2cm;}\n";
  html += "p{margin:0 0 0.8em;text-indent:2em;}\n";
  html += "rt{font-size:0.55em;color:#333;}\n";
  html += "</style>\n";
  html += "</head>\n<body>\n";

  for (const auto& para : paragraphs) {
    html += "<p>";
    for (const auto& w : para) {
      if (!w.is_hanzi || w.pinyin.empty()) {
        html += html_escape(w.text);
      } else {
        html += "<ruby>" + html_escape(w.text) + "<rt>" +
                html_escape(pinyin_to_accented(w.pinyin)) + "</rt></ruby>";
      }
    }
    html += "</p>\n";
  }

  html += "</body>\n</html>\n";
  return html;
}
