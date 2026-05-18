#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "tts.hpp"

#include <windows.h>

#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

static std::string hex_encode(const std::string& s) {
  std::ostringstream oss;
  for (unsigned char c : s)
    oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
  return oss.str();
}

struct Worker {
  HANDLE process = nullptr;
  HANDLE thread = nullptr;
  fs::path tmp_dir;
  std::vector<std::string> words;  // Words assigned to worker, in order
};

static Worker spawn_worker(const std::string& piper_exe,
                           const std::string& model_path,
                           const fs::path& tmp_dir,
                           const std::vector<std::string>& words) {
  Worker w;
  w.tmp_dir = tmp_dir;
  w.words = words;
  fs::create_directories(tmp_dir);

  std::string cmd = "\"" + piper_exe + "\"" + " --model \"" + model_path +
                    "\"" + " --output_dir \"" + tmp_dir.string() + "\"";

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE pipe_read = nullptr, pipe_write = nullptr;
  if (!CreatePipe(&pipe_read, &pipe_write, &sa, 0)) return w;
  SetHandleInformation(pipe_write, HANDLE_FLAG_INHERIT, 0);

  // Open NUL so child process has a valid handle.
  HANDLE null_out =
      CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, &sa,
                  OPEN_EXISTING, 0, nullptr);

  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = pipe_read;
  si.hStdOutput = null_out;
  si.hStdError = null_out;

  PROCESS_INFORMATION pi{};
  std::vector<char> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back('\0');

  if (!CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    CloseHandle(pipe_read);
    CloseHandle(pipe_write);
    if (null_out != INVALID_HANDLE_VALUE) CloseHandle(null_out);
    return w;
  }
  CloseHandle(pipe_read);
  if (null_out != INVALID_HANDLE_VALUE) CloseHandle(null_out);

  for (const auto& word : words) {
    std::string line = word + "\n";
    DWORD written;
    WriteFile(pipe_write, line.c_str(), (DWORD)line.size(), &written, nullptr);
  }
  CloseHandle(pipe_write);

  w.process = pi.hProcess;
  w.thread = pi.hThread;
  return w;
}

std::map<std::string, std::string> build_audio_map(
    const std::vector<std::vector<AnnotatedWord>>& paragraphs) {
  std::set<std::string> seen;
  std::map<std::string, std::string> audio_map;
  for (const auto& para : paragraphs) {
    for (const auto& w : para) {
      if (!w.is_hanzi || w.pinyin.empty() || seen.insert(w.text).second) continue;
      audio_map[w.text] = "audio/" + hex_encode(w.text) + ".wav";
    }
  }
  return audio_map;
}

std::map<std::string, std::string> generate_audio(
    const std::vector<std::vector<AnnotatedWord>>& paragraphs,
    const TtsConfig& cfg) {
  fs::create_directories(cfg.audio_dir);
  if (!cfg.cache_dir.empty()) fs::create_directories(cfg.cache_dir);

  std::set<std::string> seen;
  std::map<std::string, std::string> audio_map;
  std::vector<std::string> to_generate;

  for (const auto& para : paragraphs) {
    for (const auto& w : para) {
      if (!w.is_hanzi || w.pinyin.empty()) continue;
      if (!seen.insert(w.text).second) continue;

      std::string filename = hex_encode(w.text) + ".wav";
      audio_map[w.text] = "audio/" + filename;

      fs::path cache_file = !cfg.cache_dir.empty()
                                ? fs::path(cfg.cache_dir) / filename
                                : fs::path{};
      fs::path audio_file = fs::path(cfg.audio_dir) / filename;

      if (!cache_file.empty() && fs::exists(cache_file)) {
        fs::copy_file(cache_file, audio_file,
                      fs::copy_options::overwrite_existing);
      } else if (!fs::exists(audio_file)) {
        to_generate.push_back(w.text);
      }
    }
  }

  if (to_generate.empty()) return audio_map;

  // Cap at 2 workers otherwise it uses crazy amounts of CPU.
  const int N =
      std::min({2, static_cast<int>(std::thread::hardware_concurrency()),
                static_cast<int>(to_generate.size())});
  std::vector<Worker> workers(N);
  for (int i = 0; i < N; ++i) {
    std::vector<std::string> chunk;
    for (int j = i; j < static_cast<int>(to_generate.size()); j += N)
      chunk.push_back(to_generate[j]);
    workers[i] = spawn_worker(
        cfg.piper_exe, cfg.model_path,
        fs::path(cfg.audio_dir) / ("_tmp" + std::to_string(i)), chunk);
  }

  // Wait for all workers to finish.
  std::vector<HANDLE> handles;
  for (auto& w : workers)
    if (w.process) handles.push_back(w.process);
  if (!handles.empty()) {
    int timeout = 10 * 60 * 1000;  // 10 minutes in ms.
    if (WaitForMultipleObjects((DWORD)handles.size(), handles.data(), TRUE,
                               timeout) == WAIT_TIMEOUT) {
      for (auto& wk : workers)
        if (wk.process) TerminateProcess(wk.process, 1);
      WaitForMultipleObjects((DWORD)handles.size(), handles.data(), TRUE, 5000);
    }
  }

  // Collect outputs: worker i wrote 0.wav, 1.wav, ... for its chunk.
  for (auto& w : workers) {
    for (size_t j = 0; j < w.words.size(); ++j) {
      fs::path src = w.tmp_dir / (std::to_string(j) + ".wav");
      std::string filename = hex_encode(w.words[j]) + ".wav";
      fs::path dst = fs::path(cfg.audio_dir) / filename;
      if (fs::exists(src)) {
        if (!cfg.cache_dir.empty()) {
          fs::path cache_file = fs::path(cfg.cache_dir) / filename;
          std::error_code mv_ec;
          fs::rename(src, cache_file, mv_ec);
          if (mv_ec) {
            fs::copy_file(src, cache_file,
                          fs::copy_options::overwrite_existing);
            fs::remove(src);
          }
          fs::copy_file(cache_file, dst, fs::copy_options::overwrite_existing);
        } else {
          fs::rename(src, dst);
        }
      } else {
        audio_map.erase(w.words[j]);
      }
    }
    if (w.process) {
      CloseHandle(w.process);
      CloseHandle(w.thread);
    }
    fs::remove_all(w.tmp_dir);
  }

  return audio_map;
}
