#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

// Loopback HTTP server for the reader. Serves the current document at GET /
// and its TTS audio at GET /audio/<file>.wav, bound to 127.0.0.1 on an
// ephemeral port. Serving over HTTP (instead of file://) lets the browser play
// the generated WAVs, which file:// blocks.
class HttpServer {
 public:
  // Handles POST /api/paragraph: receives the raw request body and returns the
  // JSON response body.
  using ParagraphHandler = std::function<std::string(const std::string& body)>;
  // Handles GET /api/status: returns the JSON response body.
  using StatusHandler = std::function<std::string()>;

  HttpServer();
  ~HttpServer();

  // Sets the document served at GET / and the directory served at /audio/*.
  // Safe to call repeatedly; the latest document wins.
  void set_document(std::string html, std::filesystem::path audio_dir);

  // Registers the edit-API handlers. Call before start().
  void set_api_handlers(ParagraphHandler paragraph, StatusHandler status);

  // Starts the server if not already running. Returns the bound port, or 0 on
  // failure. Idempotent: returns the existing port once started.
  int start();

  // Stops the server and joins its thread. Safe to call when not running.
  void stop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  int port_ = 0;
};
