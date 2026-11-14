// Network access for online play, through libcurl: one-off HTTPS requests (the Cloudflare setup)
// and a WebSocket to the relay. The WebSocket runs on its own thread, so the game never waits on
// the network; the app collects what arrived once per frame with poll().
#pragma once

#include <atomic>
#include <cstdint>
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
    // For Closed: the relay's refusal (an HTTP status such as 404; 0: none), and the code of
    // its closing message (4000: the host left; 0: the connection just broke or never opened).
    long status = 0;
    int close_code = 0;
  };

  WebSocket() = default;
  WebSocket(const WebSocket&) = delete;
  WebSocket& operator=(const WebSocket&) = delete;
  ~WebSocket() { close(); }

  void open(const std::string& url);
  // Connects only to deliver `message`, then closes (a goodbye from a player whose connection had
  // broken when it left). Nothing to poll; it ends on its own, or with close() or the next open().
  void say_and_close(const std::string& url, std::string message);
  void send(std::string binary);  // queued; sent in order
  void close();
  std::vector<Event> poll();  // everything that happened since the last call
  bool active() const { return thread_.joinable() && !finished_; }
  // Messages queued and not yet handed to the connection (for the log: a line that can't keep up).
  std::size_t backlog() const;
  // How long the relay itself has been silent, in milliseconds: the connection asks it for a
  // sign of life every 3 seconds, whatever the other players do. -1 until its first answer (a
  // relay that never answers can't be judged this way).
  int silent_ms() const;

 private:
  void run(std::string url);
  void push(Event::Kind kind, std::string data, long status = 0, int close_code = 0);

  std::thread thread_;
  mutable std::mutex mutex_;
  std::vector<Event> events_;
  std::deque<std::string> outgoing_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> finished_{false};
  std::atomic<bool> farewell_{false};  // say_and_close: close once the queue is sent
  std::atomic<uint64_t> heard_ms_{0};  // when the relay was last heard (SDL ticks); 0: no answer yet
};

// Which name lookup reached the relay last time (0: the computer's own; 1 to 3: DNS-over-HTTPS
// by Cloudflare, Google, Quad9). It's tried first, so a computer whose internet provider blocks
// the relay's address doesn't wait out a timeout on every connection. The app keeps it in its
// settings.
int relay_lookup();
void set_relay_lookup(int which);

}  // namespace p64::net
