#include "http_server.hpp"

#include "httplib.h"  // header-only; confined to this TU (large header)

#include <fstream>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;

struct HttpServer::Impl {
  httplib::Server svr;
  std::thread thread;
  std::mutex life_mtx;  // guards start/stop (port_ + thread)
  std::mutex mtx;       // guards html + audio_dir
  std::string html;
  fs::path audio_dir;
};

HttpServer::HttpServer() : impl_(std::make_unique<Impl>()) {}

HttpServer::~HttpServer() { stop(); }

void HttpServer::set_document(std::string html, fs::path audio_dir) {
  std::lock_guard<std::mutex> lk(impl_->mtx);
  impl_->html = std::move(html);
  impl_->audio_dir = std::move(audio_dir);
}

int HttpServer::start() {
  std::lock_guard<std::mutex> life(impl_->life_mtx);
  if (port_) return port_;
  Impl* impl = impl_.get();

  impl->svr.Get("/", [impl](const httplib::Request&, httplib::Response& res) {
    std::lock_guard<std::mutex> lk(impl->mtx);
    res.set_content(impl->html, "text/html; charset=utf-8");
  });

  // Filenames are ASCII hex (see tts.cpp hex_encode); [^/] blocks traversal.
  impl->svr.Get(R"(/audio/([^/]+\.wav))",
                [impl](const httplib::Request& req, httplib::Response& res) {
                  fs::path dir;
                  {
                    std::lock_guard<std::mutex> lk(impl->mtx);
                    dir = impl->audio_dir;
                  }
                  // Require the resolved file to stay inside the audio dir.
                  // lexically_relative yields an empty path for a different
                  // root (e.g. an absolute "C:\..."), or one starting with
                  // ".." for an escape; both are rejected. This holds on
                  // Windows, where '\' is also a separator that the [^/] route
                  // pattern does not exclude.
                  std::string name = req.matches[1];
                  fs::path base = dir.lexically_normal();
                  fs::path rel = (dir / name).lexically_normal()
                                     .lexically_relative(base);
                  if (dir.empty() || rel.empty() || *rel.begin() == "..") {
                    res.status = 404;
                    return;
                  }
                  std::ifstream f(dir / name, std::ios::binary);
                  if (!f) {
                    res.status = 404;
                    return;
                  }
                  std::string data((std::istreambuf_iterator<char>(f)), {});
                  res.set_content(std::move(data), "audio/wav");
                });

  int port = impl->svr.bind_to_any_port("127.0.0.1");
  if (port <= 0) return 0;
  port_ = port;
  impl->thread = std::thread([impl]() { impl->svr.listen_after_bind(); });
  return port_;
}

void HttpServer::stop() {
  std::lock_guard<std::mutex> life(impl_->life_mtx);
  if (impl_->thread.joinable()) {
    impl_->svr.stop();
    impl_->thread.join();
  }
  port_ = 0;
}
