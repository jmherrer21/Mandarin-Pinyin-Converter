#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "dictionary.hpp"
#include "pdf_extractor.hpp"
#include "renderer.hpp"
#include "segmenter.hpp"
#include "tts.hpp"

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

#define WM_CONV_STATUS \
  (WM_APP + 1)  // wParam: heap wchar_t*, lParam: 0=info 1=done 2=error
#define WM_CONV_DONE (WM_APP + 2)

#define ID_EDIT_INPUT 101
#define ID_BTN_BROWSE_INPUT 102
#define ID_EDIT_OUTPUT 103
#define ID_BTN_BROWSE_OUTPUT 104
#define ID_CHK_PDF 105
#define ID_BTN_CONVERT 106
#define ID_LBL_STATUS 107
#define ID_BTN_OPEN 108
#define ID_CHK_TTS 109

static HWND g_hwnd = nullptr;
static HWND g_edit_in = nullptr;
static HWND g_edit_out = nullptr;
static HWND g_chk_pdf = nullptr;
static HWND g_chk_tts = nullptr;
static HWND g_btn_convert = nullptr;
static HWND g_lbl_status = nullptr;
static HWND g_btn_open = nullptr;
static std::atomic<bool> g_busy{false};
static std::wstring g_last_output;
static std::mutex g_output_mtx;
static std::string g_piper_exe;
static std::string g_model_path;

static std::filesystem::path exe_dir() {
  wchar_t buf[MAX_PATH];
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return std::filesystem::path(buf).parent_path();
}

static std::wstring find_wkhtmltopdf() {
  auto p = exe_dir() / "wkhtmltopdf.exe";
  if (std::filesystem::exists(p)) return p.wstring();
  return {};
}

// Returns empty string on success, or a human-readable error on failure.
static std::wstring run_wkhtmltopdf(const std::wstring& wk,
                                    const std::wstring& html_abs,
                                    const std::wstring& pdf_abs) {
  // wkhtmltopdf fails on unicode filenames. Copy to fixed ASCII temp paths, run
  // there, move result.
  wchar_t temp_dir_buf[MAX_PATH];
  GetTempPathW(MAX_PATH, temp_dir_buf);
  std::filesystem::path temp_dir(temp_dir_buf);
  auto tmp_html = temp_dir / "mpr_wkhtml_in.html";
  auto tmp_pdf = temp_dir / "mpr_wkhtml_out.pdf";

  auto log_path = exe_dir() / "wkhtmltopdf.log";
  auto write_log = [&](const std::string& text, bool append = true) {
    std::ofstream f(log_path, append ? std::ios::app : std::ios::trunc);
    f << text;
  };

  std::error_code ec;
  std::filesystem::copy_file(html_abs, tmp_html,
                             std::filesystem::copy_options::overwrite_existing,
                             ec);
  if (ec) {
    write_log("copy HTML to temp failed: " + ec.message() + "\n", false);
    return L"Could not copy HTML to temp: " +
           std::wstring(ec.message().begin(), ec.message().end());
  }
  std::filesystem::remove(tmp_pdf, ec);  // clear stale output

  std::wstring cmd =
      L"\"" + wk +
      L"\""
      L" --encoding utf-8 --enable-local-file-access --disable-javascript"
      L" \"" +
      tmp_html.wstring() +
      L"\""
      L" \"" +
      tmp_pdf.wstring() + L"\"";

  {
    std::string narrow_cmd;
    for (wchar_t c : cmd) narrow_cmd += (c < 128 ? static_cast<char>(c) : '?');
    write_log("CMD: " + narrow_cmd + "\n", false);
  }

  HANDLE hRead = nullptr, hWrite = nullptr;
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  CreatePipe(&hRead, &hWrite, &sa, 0);
  SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = hWrite;
  si.hStdError = hWrite;
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(L'\0');
  if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    DWORD err = GetLastError();
    CloseHandle(hRead);
    CloseHandle(hWrite);
    write_log("CreateProcessW failed, error=" + std::to_string(err) + "\n");
    return L"CreateProcess failed (error " + std::to_wstring(err) + L")";
  }
  CloseHandle(hWrite);

  std::string out;
  char tmp[512];
  DWORD nread;
  while (ReadFile(hRead, tmp, sizeof(tmp), &nread, nullptr) && nread)
    out.append(tmp, nread);
  CloseHandle(hRead);

  if (WaitForSingleObject(pi.hProcess, 60000) == WAIT_TIMEOUT) {
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    std::filesystem::remove(tmp_html, ec);
    std::filesystem::remove(tmp_pdf, ec);
    write_log("TIMED OUT after 60 seconds\nOUTPUT (partial):\n" + out + "\n");
    return L"wkhtmltopdf timed out after 60 seconds";
  }
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  bool tmp_pdf_exists = std::filesystem::exists(tmp_pdf);
  write_log("EXIT CODE: " + std::to_string(code) +
            "\n"
            "PDF exists: " +
            (tmp_pdf_exists ? "yes" : "no") +
            "\n"
            "OUTPUT:\n" +
            out + "\n");

  // Cleanup.
  if (!tmp_pdf_exists) {
    std::filesystem::remove(tmp_html, ec);
    while (!out.empty() &&
           (out.back() == '\r' || out.back() == '\n' || out.back() == ' '))
      out.pop_back();
    auto pos = out.find_last_of("\r\n");
    std::string last = (pos == std::string::npos) ? out : out.substr(pos + 1);
    std::wstring reason;
    int wn = MultiByteToWideChar(CP_UTF8, 0, last.c_str(), -1, nullptr, 0);
    if (wn > 1) {
      reason.resize(wn - 1);
      MultiByteToWideChar(CP_UTF8, 0, last.c_str(), -1, reason.data(), wn);
    }
    std::wstring msg = L"wkhtmltopdf exited " + std::to_wstring(code);
    if (!reason.empty()) msg += L": " + reason;
    return msg;
  }

  // Move result from temp to the intended destination.
  std::filesystem::rename(tmp_pdf, pdf_abs, ec);
  if (ec) {
    std::filesystem::copy_file(
        tmp_pdf, pdf_abs, std::filesystem::copy_options::overwrite_existing,
        ec);
    std::filesystem::remove(tmp_pdf);
  }
  std::filesystem::remove(tmp_html, ec);
  return {};
}

static void post_status(const std::wstring& msg, int type = 0) {
  HWND h = g_hwnd;
  if (!h) return;
  auto* s = new wchar_t[msg.size() + 1];
  wmemcpy(s, msg.c_str(), msg.size() + 1);
  PostMessageW(h, WM_CONV_STATUS, reinterpret_cast<WPARAM>(s),
               static_cast<LPARAM>(type));
}

static bool is_valid_utf8(const std::string& s) {
  for (size_t i = 0; i < s.size();) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    size_t len;
    if (c < 0x80)
      len = 1;
    else if ((c & 0xE0) == 0xC0 && c >= 0xC2)
      len = 2;
    else if ((c & 0xF0) == 0xE0)
      len = 3;
    else if ((c & 0xF8) == 0xF0 && c <= 0xF4)
      len = 4;
    else
      return false;
    for (size_t j = 1; j < len; ++j) {
      if (i + j >= s.size() ||
          (static_cast<unsigned char>(s[i + j]) & 0xC0) != 0x80)
        return false;
    }
    i += len;
  }
  return true;
}

static std::string wide_to_utf8(const std::wstring& ws) {
  if (ws.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr,
                              nullptr);
  std::string s(n - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, s.data(), n, nullptr,
                      nullptr);
  return s;
}

static bool do_convert(std::wstring in_w, std::wstring out_w, bool gen_pdf,
                       bool do_tts) {
  auto data_dir = exe_dir() / "data";
  post_status(L"Loading dictionary...");
  Dictionary dict = load_cedict((data_dir / "cedict_ts.u8").string());

  post_status(L"Loading segmenter...");
  Segmenter seg((data_dir / "jieba_dict").string());

  std::filesystem::path in_path(in_w);
  std::vector<std::vector<AnnotatedWord>> paragraphs;
  std::vector<int> page_breaks;
  auto ext = in_path.extension().wstring();
  for (auto& c : ext) c = towlower(c);

  if (ext == L".pdf") {
    post_status(L"Extracting text from PDF...");
    std::vector<std::string> pages;
    try {
      pages = extract_pages(wide_to_utf8(in_w));
    } catch (const std::exception& e) {
      post_status(L"PDF extraction failed: " +
                      std::wstring(e.what(), e.what() + strlen(e.what())),
                  2);
      return false;
    }
    post_status(L"Segmenting and annotating...");
    for (const auto& page_text : pages) {
      page_breaks.push_back(static_cast<int>(paragraphs.size()));
      std::istringstream ss(page_text);
      std::string line, buf;
      auto flush = [&]() {
        if (!buf.empty()) {
          auto words = annotate(seg.cut(buf), dict);
          if (!words.empty()) paragraphs.push_back(std::move(words));
          buf.clear();
        }
      };
      while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty())
          flush();
        else
          buf += (buf.empty() ? "" : " ") + line;
      }
      flush();
    }
  } else {
    post_status(L"Reading text file...");
    std::ifstream ifs(in_path, std::ios::binary);
    if (!ifs) {
      post_status(L"Cannot open input file.", 2);
      return false;
    }
    std::string text;
    text.assign(std::istreambuf_iterator<char>(ifs), {});
    // Strip UTF-8 BOM if present
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
      text.erase(0, 3);
    if (!is_valid_utf8(text)) {
      post_status(
          L"File is not UTF-8 encoded. Re-save it as UTF-8 and try again.", 2);
      return false;
    }
    post_status(L"Segmenting and annotating...");
    page_breaks = {0};
    std::istringstream stream(text);
    std::string line, buf;
    auto flush_buf = [&]() {
      if (!buf.empty()) {
        auto words = annotate(seg.cut(buf), dict);
        if (!words.empty()) paragraphs.push_back(std::move(words));
        buf.clear();
      }
    };
    while (std::getline(stream, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty()) {
        flush_buf();
        continue;
      }
      size_t s = line.find_first_not_of(" \t");
      if (s == std::string::npos) {
        flush_buf();
        continue;
      }
      buf += line.substr(s);
      flush_buf();
    }
  }

  std::filesystem::path out_path(out_w);
  if (auto p = out_path.parent_path(); !p.empty())
    std::filesystem::create_directories(p);

  // Pre-build audio map so paths are embedded in HTML before TTS runs.
  std::map<std::string, std::string> audio_map;
  do_tts = do_tts && !g_piper_exe.empty() && !g_model_path.empty();
  TtsConfig tts_cfg;
  if (do_tts) {
    tts_cfg.piper_exe = g_piper_exe;
    tts_cfg.model_path = g_model_path;
    tts_cfg.audio_dir = (out_path.parent_path() / "audio").string();
    tts_cfg.cache_dir = (exe_dir() / "audio_cache").string();
    std::filesystem::create_directories(tts_cfg.cache_dir);
    audio_map = build_audio_map(paragraphs);
  }

  post_status(L"Writing HTML...");
  std::ofstream ofs(out_path);
  if (!ofs) {
    post_status(L"Cannot write output file.", 2);
    return false;
  }
  ofs << render_html(paragraphs, audio_map, page_breaks);
  ofs.close();

  // Set g_last_output before the PDF block so "Open result" works even if PDF
  // export fails.
  {
    std::lock_guard<std::mutex> lk(g_output_mtx);
    g_last_output = out_path.wstring();
  }

  if (gen_pdf) {
    std::wstring wk = find_wkhtmltopdf();
    if (wk.empty()) {
      post_status(L"wkhtmltopdf.exe not found in app directory; skipping PDF.",
                  2);
      return false;
    }
    PWSTR dl_buf = nullptr;
    SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &dl_buf);
    std::filesystem::path dl_dir =
        dl_buf ? std::filesystem::path(dl_buf) : out_path.parent_path();
    CoTaskMemFree(dl_buf);
    auto pdf_path = dl_dir / (out_path.stem().wstring() + L".pdf");

    // Write static HTML for PDF generation.
    auto pdf_src_path = out_path.parent_path() /
                        (out_path.stem().wstring() + L"_print_tmp.html");
    {
      std::ofstream pdf_src_ofs(pdf_src_path);
      pdf_src_ofs << render_pdf_html(paragraphs);
    }
    auto html_abs = std::filesystem::absolute(pdf_src_path).wstring();
    auto pdf_abs = std::filesystem::absolute(pdf_path).wstring();
    post_status(L"Generating PDF... log: " +
                (exe_dir() / L"wkhtmltopdf.log").wstring());
    std::wstring pdf_err = run_wkhtmltopdf(wk, html_abs, pdf_abs);
    std::error_code ec_rm;
    std::filesystem::remove(pdf_src_path, ec_rm);
    if (!pdf_err.empty()) {
      post_status(L"PDF generation failed: " + pdf_err, 2);
      return false;
    }
    post_status(L"Done - HTML and PDF written.", 1);
  } else {
    post_status(L"Done - HTML written.", 1);
  }

  // Generate audio in background so the output is immediately readable.
  // TTS can take several minutes to fully load.
  if (do_tts) {
    std::thread([cfg = tts_cfg, paras = std::move(paragraphs)]() {
      post_status(L"Generating TTS in background...");
      try {
        generate_audio(paras, cfg);
        post_status(L"TTS done.", 1);
      } catch (const std::exception& e) {
        post_status(L"TTS error: " +
                        std::wstring(e.what(), e.what() + strlen(e.what())),
                    2);
      }
    }).detach();
  }

  return true;
}

static void convert_worker(std::wstring in_w, std::wstring out_w,
                           bool gen_pdf, bool do_tts) {
  try {
    do_convert(std::move(in_w), std::move(out_w), gen_pdf, do_tts);
  } catch (const std::exception& e) {
    post_status(
        L"Error: " + std::wstring(e.what(), e.what() + strlen(e.what())), 2);
  }
  g_busy = false;
  PostMessageW(g_hwnd, WM_CONV_DONE, 0, 0);
}

static std::wstring get_text(HWND h) {
  int n = GetWindowTextLengthW(h) + 1;
  std::wstring s(n, L'\0');
  GetWindowTextW(h, s.data(), n);
  s.resize(wcslen(s.c_str()));
  return s;
}

static void layout(HWND hwnd) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  int cw = rc.right;
  int ch = rc.bottom;
  int edit_w = cw - 200;
  int browse_x = cw - 82;
  int btn_y = ch - 73;
  int status_y = ch - 27;

  SetWindowPos(g_edit_in, nullptr, 110, 12, edit_w, 23, SWP_NOZORDER);
  SetWindowPos(GetDlgItem(hwnd, ID_BTN_BROWSE_INPUT), nullptr, browse_x, 12, 72,
               23, SWP_NOZORDER);
  SetWindowPos(g_edit_out, nullptr, 110, 46, edit_w, 23, SWP_NOZORDER);
  SetWindowPos(GetDlgItem(hwnd, ID_BTN_BROWSE_OUTPUT), nullptr, browse_x, 46,
               72, 23, SWP_NOZORDER);
  SetWindowPos(g_btn_convert, nullptr, 165, btn_y, 130, 30, SWP_NOZORDER);
  SetWindowPos(g_btn_open, nullptr, 308, btn_y, 100, 30, SWP_NOZORDER);
  SetWindowPos(g_lbl_status, nullptr, 10, status_y, cw - 20, 18, SWP_NOZORDER);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
      auto mk = [&](LPCWSTR cls, LPCWSTR txt, DWORD style, int x, int y, int w,
                    int h, int id) -> HWND {
        HWND c = CreateWindowW(
            cls, txt, WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), nullptr,
            nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return c;
      };

      mk(L"STATIC", L"Input file:", SS_LEFT, 10, 15, 95, 18, 0);
      g_edit_in = mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 110, 12, 260, 23,
                     ID_EDIT_INPUT);
      mk(L"BUTTON", L"Browse...", BS_PUSHBUTTON, 378, 12, 72, 23,
         ID_BTN_BROWSE_INPUT);

      mk(L"STATIC", L"Output HTML:", SS_LEFT, 10, 49, 95, 18, 0);
      g_edit_out = mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 110, 46, 260,
                      23, ID_EDIT_OUTPUT);
      mk(L"BUTTON", L"Browse...", BS_PUSHBUTTON, 378, 46, 72, 23,
         ID_BTN_BROWSE_OUTPUT);

      g_chk_pdf = mk(L"BUTTON", L"Also export PDF", BS_AUTOCHECKBOX, 110, 80,
                     160, 21, ID_CHK_PDF);

      bool tts_avail = !g_piper_exe.empty() && !g_model_path.empty();
      g_chk_tts = mk(L"BUTTON", L"Generate TTS audio", BS_AUTOCHECKBOX, 110,
                     103, 200, 21, ID_CHK_TTS);
      if (tts_avail) {
        SendMessageW(g_chk_tts, BM_SETCHECK, BST_CHECKED, 0);
      } else {
        EnableWindow(g_chk_tts, FALSE);
      }

      g_btn_convert = mk(L"BUTTON", L"Convert", BS_PUSHBUTTON | WS_DISABLED,
                         165, 135, 130, 30, ID_BTN_CONVERT);

      g_lbl_status = mk(L"STATIC", L"Load a PDF or text file to begin.",
                        SS_LEFT | SS_NOPREFIX, 10, 181, 440, 18, ID_LBL_STATUS);

      g_btn_open = mk(L"BUTTON", L"Open result", BS_PUSHBUTTON | WS_DISABLED,
                      308, 135, 100, 30, ID_BTN_OPEN);

      layout(hwnd);
      return 0;
    }

    case WM_SIZE:
      layout(hwnd);
      break;

    case WM_GETMINMAXINFO: {
      auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
      RECT r{0, 0, 460, 208};
      AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
      mm->ptMinTrackSize = {r.right - r.left, r.bottom - r.top};
      return 0;
    }

    case WM_COMMAND: {
      int id = LOWORD(wp);
      if (id == ID_BTN_BROWSE_INPUT) {
        wchar_t path[MAX_PATH]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = L"PDF and Text Files\0*.pdf;*.txt\0All Files\0*.*\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileNameW(&ofn)) {
          SetWindowTextW(g_edit_in, path);
          std::filesystem::path p(path);
          auto out = p.parent_path() / (p.stem().wstring() + L"_pinyin.html");
          SetWindowTextW(g_edit_out, out.wstring().c_str());
          EnableWindow(g_btn_convert, TRUE);
        }
      } else if (id == ID_BTN_BROWSE_OUTPUT) {
        wchar_t path[MAX_PATH]{};
        GetWindowTextW(g_edit_out, path, MAX_PATH);
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = L"HTML Files\0*.html\0All Files\0*.*\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"html";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (GetSaveFileNameW(&ofn)) SetWindowTextW(g_edit_out, path);
      } else if (id == ID_BTN_CONVERT && !g_busy) {
        auto in = get_text(g_edit_in);
        auto out = get_text(g_edit_out);
        if (in.empty() || out.empty()) {
          SetWindowTextW(g_lbl_status,
                         L"Please specify input and output paths.");
          break;
        }
        bool pdf = SendMessageW(g_chk_pdf, BM_GETCHECK, 0, 0) == BST_CHECKED;
        bool tts = SendMessageW(g_chk_tts, BM_GETCHECK, 0, 0) == BST_CHECKED;
        g_busy = true;
        {
          std::lock_guard<std::mutex> lk(g_output_mtx);
          g_last_output.clear();
        }
        EnableWindow(g_btn_convert, FALSE);
        EnableWindow(g_btn_open, FALSE);
        SetWindowTextW(g_lbl_status, L"Starting...");
        std::thread(convert_worker, in, out, pdf, tts).detach();
      } else if (id == ID_BTN_OPEN) {
        std::wstring open_path;
        {
          std::lock_guard<std::mutex> lk(g_output_mtx);
          open_path = g_last_output;
        }
        if (!open_path.empty())
          ShellExecuteW(hwnd, L"open", open_path.c_str(), nullptr, nullptr,
                        SW_SHOWNORMAL);
      }
      break;
    }

    case WM_CONV_STATUS: {
      auto* s = reinterpret_cast<wchar_t*>(wp);
      SetWindowTextW(g_lbl_status, s);
      delete[] s;
      break;
    }

    case WM_CONV_DONE: {
      EnableWindow(g_btn_convert, TRUE);
      bool has_out;
      {
        std::lock_guard<std::mutex> lk(g_output_mtx);
        has_out = !g_last_output.empty();
      }
      if (has_out) EnableWindow(g_btn_open, TRUE);
      break;
    }

    case WM_DESTROY:
      g_hwnd = nullptr;
      PostQuitMessage(0);
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
  SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

  {
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
      for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--piper" && i + 1 < argc)
          g_piper_exe = wide_to_utf8(argv[++i]);
        else if (a == L"--model" && i + 1 < argc)
          g_model_path = wide_to_utf8(argv[++i]);
      }
      LocalFree(argv);
    }
  }

  // Detect piper (installer places it in <exe_dir>\piper\)
  if (g_piper_exe.empty() || g_model_path.empty()) {
    namespace fs = std::filesystem;
    fs::path piper_dir = exe_dir() / "piper";
    fs::path auto_exe = piper_dir / "piper.exe";
    fs::path auto_model = piper_dir / "zh_CN-huayan-medium.onnx";
    if (fs::exists(auto_exe) && fs::exists(auto_model)) {
      g_piper_exe = auto_exe.string();
      g_model_path = auto_model.string();
    }
  }

  WNDCLASSW wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground =
      reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_BTNFACE + 1));
  wc.lpszClassName = L"MandarinConverterWnd";
  RegisterClassW(&wc);

  RECT r{0, 0, 460, 208};
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

  g_hwnd = CreateWindowExW(0, L"MandarinConverterWnd",
                           L"Mandarin Pinyin Converter", WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                           r.bottom - r.top, nullptr, nullptr, hInst, nullptr);

  ShowWindow(g_hwnd, nShow);
  UpdateWindow(g_hwnd);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}