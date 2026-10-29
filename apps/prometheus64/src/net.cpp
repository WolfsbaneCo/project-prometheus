#include "net.hpp"

#include <SDL3/SDL_log.h>
#include <curl/curl.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

#include <chrono>

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

std::vector<WebSocket::Event> WebSocket::poll() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Event> out;
  out.swap(events_);
  return out;
}

void WebSocket::push(Event::Kind kind, std::string data) {
  std::lock_guard<std::mutex> lock(mutex_);
  events_.push_back({kind, std::move(data)});
}

void WebSocket::run(std::string url) {
  // Some internet providers block workers.dev addresses by answering the name lookup with another
  // server (a block page), so the relay's certificate check fails or the name doesn't resolve.
  // Then the relay is looked up again over DNS-over-HTTPS, which such blocks don't reach.
  static const char* const kLookups[] = {nullptr, "https://cloudflare-dns.com/dns-query", "https://dns.google/dns-query"};
  CURL* c = nullptr;
  CURLcode res = CURLE_OK;
  long status = 0;
  std::string why;
  char detail[CURL_ERROR_SIZE] = {};  // curl's own words for a failure; the handle keeps writing here
  for (const char* doh : kLookups) {
    if (c) curl_easy_cleanup(c);
    c = curl_easy_init();
    if (!c) {
      push(Event::Closed, "out of memory");
      finished_ = true;
      return;
    }
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_CONNECT_ONLY, 2L);  // 2: WebSocket upgrade, then hand the stream to us
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(c, CURLOPT_NOPROXY, "localhost,127.0.0.1");
    curl_easy_setopt(c, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);
    if (doh) curl_easy_setopt(c, CURLOPT_DOH_URL, doh);
    secure_defaults(c);
    detail[0] = '\0';
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, detail);
    res = curl_easy_perform(c);
    status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (res == CURLE_OK || status != 0) break;
    // No answer from the relay at all: keep what went wrong (name lookup, connection, secure
    // connection), which tells a blocked address apart from an antivirus or firewall.
    if (!doh) why = detail[0] ? detail : curl_easy_strerror(res);
    SDL_Log("relay connection failed%s: %s", doh ? " (looked up over DNS-over-HTTPS)" : "",
            detail[0] ? detail : curl_easy_strerror(res));
    if (res != CURLE_PEER_FAILED_VERIFICATION && res != CURLE_COULDNT_RESOLVE_HOST && res != CURLE_SSL_CONNECT_ERROR) break;
  }
  if (res != CURLE_OK && status == 0) {
    push(Event::Closed, "Could not reach the relay (" + why + "). Check your internet connection");
    curl_easy_cleanup(c);
    finished_ = true;
    return;
  }
  if (res != CURLE_OK || (status != 0 && status != 101)) {
    push(Event::Closed, refusal(status));
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
  char buffer[64 * 1024];

  while (open && !stop_) {
    // Send everything queued.
    std::deque<std::string> out;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      out.swap(outgoing_);
    }
    for (const auto& msg : out) {
      size_t done = 0;
      while (open && done < msg.size()) {
        size_t sent = 0;
        // curl may take a long message in several calls: pass on what is left each time.
        const CURLcode r = curl_ws_send(c, msg.data() + done, msg.size() - done, &sent, 0, CURLWS_BINARY);
        if (r == CURLE_AGAIN) {
          wait_socket(sock, true, 50);
        } else if (r != CURLE_OK) {
          reason = lost(r, detail);
          open = false;
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
      if (meta->flags & CURLWS_CLOSE) {
        // Close frame: a 2-byte code, then the reason text.
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
    size_t sent = 0;
    curl_ws_send(c, "", 0, &sent, 0, CURLWS_CLOSE);
  } else {
    push(Event::Closed, reason);
  }
  curl_easy_cleanup(c);
  finished_ = true;
}

}  // namespace p64::net
