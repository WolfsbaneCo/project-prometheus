#include "net.hpp"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>
#include <curl/curl.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>

namespace p64::net {
namespace {

constexpr const char* kUserAgent = "ProjectPrometheus";

size_t collect(char* data, size_t size, size_t count, void* user) {
  static_cast<std::string*>(user)->append(data, size * count);
  return size * count;
}

// Waits until the socket can be read (or written), up to `ms` milliseconds.
void wait_socket(curl_socket_t sock, bool for_write, int ms) {
  if (sock == CURL_SOCKET_BAD) return;
  fd_set set;
  FD_ZERO(&set);
  FD_SET(sock, &set);
  timeval tv{0, ms * 1000};
  select(static_cast<int>(sock + 1), for_write ? nullptr : &set, for_write ? &set : nullptr, nullptr, &tv);
}

// What a refused relay connection means, in plain words (the relay answers with these codes).
std::string refusal(long status) {
  switch (status) {
    case 400: return "This version of Project Prometheus doesn't match the group's relay. Everyone needs the same version";
    case 401: return "Your key file doesn't open this group's games. If the group was set up again, ask for the new key file";
    case 403: return "This game is full";
    case 404: return "No game is open with this code. Check the code with the host";
    case 423: return "This game has already started. Ask the host to open a new one";
    case 503: return "The group's relay isn't set up properly. Whoever set it up can do it again";
    default: return "The relay refused the connection (HTTP " + std::to_string(status) + ")";
  }
}

// The addresses curl tried, from its "Trying 1.2.3.4:443..." notes, for the log: a block page's
// address is told apart from Cloudflare's. Only those notes: others can hold the relay's address
// with the group's key in it.
int note_tried(CURL*, curl_infotype type, char* data, size_t size, void* user) {
  static constexpr char kTrying[] = "Trying ";
  constexpr size_t n = sizeof kTrying - 1;
  if (type != CURLINFO_TEXT) return 0;
  while (size > 0 && *data == ' ') ++data, --size;  // curl indents it
  if (size <= n || std::memcmp(data, kTrying, n) != 0) return 0;
  std::string address(data + n, size - n);
  while (!address.empty() && (address.back() == '\n' || address.back() == '\r' || address.back() == '.')) address.pop_back();
  std::string& tried = *static_cast<std::string*>(user);
  if (tried.size() < 200) tried += (tried.empty() ? "" : ", ") + address;
  return 0;
}

// Stops a connection attempt when the link is ended meanwhile (otherwise closing would wait for
// every lookup to time out).
int abort_if_stopped(void* stop, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  return static_cast<std::atomic<bool>*>(stop)->load() ? 1 : 0;
}

// Settings for Windows' own TLS (Schannel); other systems' TLS ignores them.
// "Connection lost", with what went wrong, so a problem on one computer can be told apart.
std::string lost(CURLcode r, const char* detail) {
  const std::string why = detail && detail[0] ? detail : curl_easy_strerror(r);
  SDL_Log("relay connection lost: %s", why.c_str());
  return "Connection lost (" + why + ")";
}

void secure_defaults(CURL* c) {
  // Windows checks whether a certificate was revoked by asking its issuer. When that can't be
  // answered (an antivirus that scans secure connections, a network that blocks the issuer's
  // server), the connection fails although the certificate is fine. Best effort still refuses
  // certificates known to be revoked.
  curl_easy_setopt(c, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_REVOKE_BEST_EFFORT));
#ifdef _WIN32
  // TLS 1.2: connections to the relay failed on Windows 11, the only Windows with TLS 1.3, which
  // curl's Schannel support handles less well; 1.2 is what every Windows version does reliably.
  curl_easy_setopt(c, CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_2 | CURL_SSLVERSION_MAX_TLSv1_2));
#endif
}

}  // namespace

void init() { curl_global_init(CURL_GLOBAL_DEFAULT); }

std::string url_encode(const std::string& s) {
  std::string out;
  static const char* hex = "0123456789ABCDEF";
  for (unsigned char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

void shutdown() { curl_global_cleanup(); }

Response request(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                 const std::string& body, const std::vector<Part>& parts, long timeout_s) {
  Response r;
  CURL* c = curl_easy_init();
  if (!c) {
    r.error = "out of memory";
    return r;
  }
  curl_slist* list = nullptr;
  for (const auto& h : headers) list = curl_slist_append(list, h.c_str());
  curl_mime* mime = nullptr;
  curl_easy_setopt(c, CURLOPT_URL, url.c_str());
  curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method.c_str());
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, list);
  curl_easy_setopt(c, CURLOPT_USERAGENT, kUserAgent);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, timeout_s);
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_NOPROXY, "localhost,127.0.0.1");
  secure_defaults(c);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, collect);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.body);
  if (!parts.empty()) {
    mime = curl_mime_init(c);
    for (const auto& p : parts) {
      curl_mimepart* part = curl_mime_addpart(mime);
      curl_mime_name(part, p.name.c_str());
      curl_mime_data(part, p.data.data(), p.data.size());
      if (!p.filename.empty()) curl_mime_filename(part, p.filename.c_str());
      if (!p.content_type.empty()) curl_mime_type(part, p.content_type.c_str());
    }
    curl_easy_setopt(c, CURLOPT_MIMEPOST, mime);
  } else if (!body.empty() || method == "POST" || method == "PUT") {
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
  }
  const CURLcode res = curl_easy_perform(c);
  if (res == CURLE_OK) {
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
  } else {
    r.error = curl_easy_strerror(res);
  }
  if (mime) curl_mime_free(mime);
  curl_slist_free_all(list);
  curl_easy_cleanup(c);
  return r;
}

void WebSocket::open(const std::string& url) {
  close();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.clear();  // nothing left over from a connection before this one
  }
  stop_ = false;
  finished_ = false;
  thread_ = std::thread(&WebSocket::run, this, url);
}

void WebSocket::send(std::string binary) {
  std::lock_guard<std::mutex> lock(mutex_);
  outgoing_.push_back(std::move(binary));
}

void WebSocket::close() {
  if (!thread_.joinable()) return;
  stop_ = true;
  thread_.join();
  std::lock_guard<std::mutex> lock(mutex_);
  outgoing_.clear();
}

std::size_t WebSocket::backlog() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return outgoing_.size();
}

std::vector<WebSocket::Event> WebSocket::poll() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Event> out;
  out.swap(events_);
  return out;
}

void WebSocket::push(Event::Kind kind, std::string data, long status, int close_code) {
  std::lock_guard<std::mutex> lock(mutex_);
  events_.push_back({kind, std::move(data), status, close_code});
}

int WebSocket::silent_ms() const {
  const uint64_t heard = heard_ms_;
  if (!heard || !active()) return -1;
  const uint64_t now = SDL_GetTicks();
  return now > heard ? static_cast<int>(std::min<uint64_t>(now - heard, 3'600'000)) : 0;
}

namespace {
std::atomic<int> g_lookup{0};
}
int relay_lookup() { return g_lookup; }
void set_relay_lookup(int which) { g_lookup = which >= 0 && which <= 3 ? which : 0; }

void WebSocket::run(std::string url) {
  // Some internet providers block workers.dev addresses by answering the name lookup with another
  // server (a block page): the relay's certificate check fails, the name doesn't resolve, or (as
  // in Indonesia) the block page's server never answers a secure connection, so it times out.
  // Then the relay is looked up again over DNS-over-HTTPS, which such blocks don't reach.
  static const char* const kLookups[] = {nullptr, "https://cloudflare-dns.com/dns-query", "https://dns.google/dns-query",
                                         "https://dns.quad9.net/dns-query"};
  CURL* c = nullptr;
  CURLcode res = CURLE_OK;
  long status = 0;
  std::string why, tried;
  char detail[CURL_ERROR_SIZE] = {};  // curl's own words for a failure; the handle keeps writing here
  // The lookup that worked last time goes first.
  int order[4] = {0, 1, 2, 3};
  if (const int first = g_lookup; first > 0) {
    order[0] = first;
    for (int i = 1, k = 0; i < 4; ++k) {
      if (k != first) order[i++] = k;
    }
  }
  heard_ms_ = 0;
  for (const int which : order) {
    const char* doh = kLookups[which];
    if (c) curl_easy_cleanup(c);
    c = curl_easy_init();
    if (!c) {
      push(Event::Closed, "out of memory");
      finished_ = true;
      return;
    }
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_CONNECT_ONLY, 2L);  // 2: WebSocket upgrade, then hand the stream to us
    // Long enough for a slow line (a secure connection to Cloudflare takes well under a second
    // even from the other side of the world), short enough that every lookup can be tried.
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(c, CURLOPT_NOPROXY, "localhost,127.0.0.1");
    curl_easy_setopt(c, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);
    if (doh) curl_easy_setopt(c, CURLOPT_DOH_URL, doh);
    tried.clear();
    curl_easy_setopt(c, CURLOPT_DEBUGFUNCTION, note_tried);
    curl_easy_setopt(c, CURLOPT_DEBUGDATA, &tried);
    curl_easy_setopt(c, CURLOPT_VERBOSE, 1L);  // to note_tried, not the console
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, abort_if_stopped);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &stop_);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    secure_defaults(c);
    detail[0] = '\0';
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, detail);
    res = curl_easy_perform(c);
    curl_easy_setopt(c, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 1L);
    if (stop_) {
      curl_easy_cleanup(c);
      finished_ = true;
      return;
    }
    status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (res == CURLE_OK || status != 0) {
      if (doh) SDL_Log("relay reached after looking it up over DNS-over-HTTPS (%s)", doh);
      if (res == CURLE_OK && status == 101) {
        g_lookup = which;
        // Where and how fast: the name lookup, the connection, the secure connection and the
        // WebSocket's upgrade, each counted from the start. The address only, never the
        // query (it carries the group key).
        const auto ms = [c](CURLINFO what) {
          curl_off_t us = 0;
          curl_easy_getinfo(c, what, &us);
          return static_cast<long long>(us / 1000);
        };
        const std::size_t a = url.find("://"), b = url.find_first_of("/?", a == std::string::npos ? 0 : a + 3);
        const std::string host = a == std::string::npos ? std::string() : url.substr(a + 3, b == std::string::npos ? b : b - a - 3);
        char* ip = nullptr;
        curl_easy_getinfo(c, CURLINFO_PRIMARY_IP, &ip);
        SDL_Log("relay connected: %s at %s; name %lld ms, connected %lld ms, secure %lld ms, ready %lld ms", host.c_str(),
                ip && ip[0] ? ip : "?", ms(CURLINFO_NAMELOOKUP_TIME_T), ms(CURLINFO_CONNECT_TIME_T),
                ms(CURLINFO_APPCONNECT_TIME_T), ms(CURLINFO_TOTAL_TIME_T));
      }
      break;
    }
    // No answer from the relay at all: keep what went wrong (name lookup, connection, secure
    // connection), which tells a blocked address apart from an antivirus or firewall.
    if (why.empty()) why = detail[0] ? detail : curl_easy_strerror(res);
    SDL_Log("relay connection failed%s: %s (tried %s)", doh ? " (looked up over DNS-over-HTTPS)" : "",
            detail[0] ? detail : curl_easy_strerror(res), tried.empty() ? "no address" : tried.c_str());
    if (res != CURLE_PEER_FAILED_VERIFICATION && res != CURLE_COULDNT_RESOLVE_HOST && res != CURLE_SSL_CONNECT_ERROR &&
        res != CURLE_OPERATION_TIMEDOUT && res != CURLE_COULDNT_CONNECT && res != CURLE_RECV_ERROR &&
        res != CURLE_SEND_ERROR)
      break;
  }
  if (res != CURLE_OK && status == 0) {
    push(Event::Closed, "Could not reach the relay (" + why + "). Check your internet connection");
    curl_easy_cleanup(c);
    finished_ = true;
    return;
  }
  if (res != CURLE_OK || (status != 0 && status != 101)) {
    push(Event::Closed, refusal(status), status);
    curl_easy_cleanup(c);
    finished_ = true;
    return;
  }
  push(Event::Open, {});

  curl_socket_t sock = CURL_SOCKET_BAD;
  curl_easy_getinfo(c, CURLINFO_ACTIVESOCKET, &sock);
  std::string message;  // a message can arrive in several pieces
  std::string reason = "Disconnected";
  bool open = true;
  int close_code = 0;
  char buffer[64 * 1024];
  uint64_t asked_ms = 0;
  bool ponged = false;

  // Sends one message whole (curl may take a long one in several calls); false: the connection broke.
  const auto send_all = [&](const std::string& msg, unsigned flags, int patience_ms) {
    size_t done = 0;
    do {
      size_t sent = 0;
      const CURLcode r = curl_ws_send(c, msg.data() + done, msg.size() - done, &sent, 0, flags);
      if (r == CURLE_AGAIN) {
        if (patience_ms <= 0) return true;  // a full line: give up on this one quietly
        wait_socket(sock, true, 50);
        patience_ms -= 50;
      } else if (r != CURLE_OK) {
        reason = lost(r, detail);
        return false;
      }
      done += sent;
    } while (done < msg.size());
    return true;
  };

  while (open && !stop_) {
    // Every 3 seconds, ask the relay for a sign of life (a WebSocket ping, which it answers
    // itself): silent_ms() then tells a dead connection from a quiet partner.
    if (const uint64_t now = SDL_GetTicks(); now - asked_ms >= 3000) {
      asked_ms = now;
      if (!send_all(std::string(), CURLWS_PING, 0)) break;
    }
    // Send everything queued.
    std::deque<std::string> out;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      out.swap(outgoing_);
    }
    for (const auto& msg : out) {
      if (!open) break;
      // A line that takes nothing for 20 seconds is as good as gone.
      size_t done = 0;
      int waited = 0;
      while (open && done < msg.size() && !stop_) {
        size_t sent = 0;
        const CURLcode r = curl_ws_send(c, msg.data() + done, msg.size() - done, &sent, 0, CURLWS_BINARY);
        if (r == CURLE_AGAIN) {
          wait_socket(sock, true, 50);
          if ((waited += 50) > 20000) {
            reason = "Connection lost (nothing could be sent for 20 seconds)";
            open = false;
          }
        } else if (r != CURLE_OK) {
          reason = lost(r, detail);
          open = false;
        } else {
          waited = 0;
        }
        done += sent;
      }
    }
    if (!open) break;

    // Read everything waiting.
    wait_socket(sock, false, 1);  // short, so inputs queued meanwhile go out within a millisecond
    for (;;) {
      size_t got = 0;
      const curl_ws_frame* meta = nullptr;
      const CURLcode r = curl_ws_recv(c, buffer, sizeof buffer, &got, &meta);
      if (r == CURLE_AGAIN) break;
      if (r != CURLE_OK) {
        reason = r == CURLE_GOT_NOTHING ? "The relay closed the connection" : lost(r, detail);
        open = false;
        break;
      }
      // Counted from the relay's first answer to a ping: a relay that never answers one would
      // look dead whenever the other players were quiet.
      if (meta->flags & CURLWS_PONG) ponged = true;
      if (ponged) heard_ms_ = std::max<uint64_t>(SDL_GetTicks(), 1);
      if (meta->flags & CURLWS_CLOSE) {
        // Close frame: a 2-byte code, then the reason text.
        if (got >= 2) close_code = static_cast<uint8_t>(buffer[0]) << 8 | static_cast<uint8_t>(buffer[1]);
        if (got > 2) reason.assign(buffer + 2, got - 2);
        open = false;
        break;
      }
      if (meta->flags & (CURLWS_PING | CURLWS_PONG)) continue;  // curl answers pings itself
      message.append(buffer, got);
      if (meta->bytesleft == 0 && !(meta->flags & CURLWS_CONT)) {
        push((meta->flags & CURLWS_TEXT) ? Event::Text : Event::Binary, std::move(message));
        message.clear();
      }
    }
  }

  if (stop_) {
    // Leaving: what was queued last (a goodbye to the others) still goes out, then the close.
    std::deque<std::string> out;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      out.swap(outgoing_);
    }
    for (const auto& msg : out) {
      if (!open || !send_all(msg, CURLWS_BINARY, 300)) break;
    }
    size_t sent = 0;
    curl_ws_send(c, "", 0, &sent, 0, CURLWS_CLOSE);
  } else {
    push(Event::Closed, reason, 0, close_code);
  }
  curl_easy_cleanup(c);
  finished_ = true;
}

}  // namespace p64::net
