// Network access for online play, through libcurl: one-off HTTPS requests (the Cloudflare setup)
// and a WebSocket to the relay. The WebSocket runs on its own thread, so the game never waits on
// the network; the app collects what arrived once per frame with poll().
#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace p64::net {

void init();  // once, before any other call
void shutdown();

// For a URL's query: letters, digits, - and _ as they are, everything else %XX.
std::string url_encode(const std::string& s);

struct Part {
  std::string name, filename, content_type, data;
};

struct Response {
  long status = 0;    // HTTP status; 0 when the request never got an answer
  std::string body;
  std::string error;  // why there was no answer
};

// Blocking. A non-empty `parts` sends multipart/form-data instead of `body`.
Response request(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                 const std::string& body = {}, const std::vector<Part>& parts = {}, long timeout_s = 60);

class WebSocket {
 public:
  struct Event {
    enum Kind { Open, Text, Binary, Closed } kind;
    std::string data;  // message, or the reason for Closed
  };

  WebSocket() = default;
  WebSocket(const WebSocket&) = delete;
  WebSocket& operator=(const WebSocket&) = delete;
  ~WebSocket() { close(); }

  void open(const std::string& url);
  void send(std::string binary);  // queued; sent in order
  void close();
  std::vector<Event> poll();  // everything that happened since the last call
  bool active() const { return thread_.joinable() && !finished_; }

 private:
  void run(std::string url);
  void push(Event::Kind kind, std::string data);

  std::thread thread_;
  std::mutex mutex_;
  std::vector<Event> events_;
  std::deque<std::string> outgoing_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> finished_{false};
};

}  // namespace p64::net
