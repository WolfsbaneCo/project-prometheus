#include "google.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <random>

#include "json.hpp"
#include "savekit/sha256.hpp"

namespace p64::google {
namespace {

#ifndef P64_GOOGLE_CLIENT_ID
#define P64_GOOGLE_CLIENT_ID ""
#endif
#ifndef P64_GOOGLE_CLIENT_SECRET
#define P64_GOOGLE_CLIENT_SECRET ""
#endif

constexpr const char* kScope = "https://www.googleapis.com/auth/drive.file";
constexpr const char* kFolderType = "application/vnd.google-apps.folder";

std::string env_or(const char* name, const char* fallback) {
  const char* v = std::getenv(name);
  return v && *v ? v : fallback;
}

std::string client_id() { return env_or("P64_GOOGLE_CLIENT_ID", P64_GOOGLE_CLIENT_ID); }
std::string client_secret() { return env_or("P64_GOOGLE_CLIENT_SECRET", P64_GOOGLE_CLIENT_SECRET); }
std::string auth_url() { return env_or("P64_GOOGLE_AUTH", "https://accounts.google.com/o/oauth2/v2/auth"); }
std::string token_url() { return env_or("P64_GOOGLE_TOKEN", "https://oauth2.googleapis.com/token"); }
std::string api_url() { return env_or("P64_GOOGLE_API", "https://www.googleapis.com/drive/v3"); }
std::string upload_url() { return env_or("P64_GOOGLE_UPLOAD", "https://www.googleapis.com/upload/drive/v3"); }

uint64_t now_ns() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string encode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

std::string decode(const std::string& s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '+') {
      out += ' ';
    } else if (s[i] == '%' && i + 2 < s.size()) {
      out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

// One parameter from "a=1&b=2".
std::string query_param(const std::string& query, const std::string& name) {
  std::size_t at = 0;
  while (at < query.size()) {
    const std::size_t end = std::min(query.find('&', at), query.size());
    const std::string pair = query.substr(at, end - at);
    const std::size_t eq = pair.find('=');
    if (pair.substr(0, eq) == name) return eq == std::string::npos ? std::string() : decode(pair.substr(eq + 1));
    at = end + 1;
  }
  return {};
}

std::string random_text(std::size_t n) {
  static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
  std::random_device rd;
  std::string out;
  for (std::size_t i = 0; i < n; ++i) out += chars[rd() % 66];
  return out;
}

std::string base64url(const std::vector<uint8_t>& data) {
  static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  uint32_t buf = 0;
  int bits = 0;
  for (uint8_t b : data) {
    buf = buf << 8 | b;
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      out += chars[(buf >> bits) & 63];
    }
  }
  if (bits > 0) out += chars[(buf << (6 - bits)) & 63];
  return out;
}

// PKCE: the challenge Google sees is the SHA-256 of a secret only this app knows.
std::string challenge_for(const std::string& verifier) {
  const std::string hex = savekit::sha256_hex(savekit::Bytes(verifier.begin(), verifier.end()));
  std::vector<uint8_t> raw;
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2) raw.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
  return base64url(raw);
}

// ---------------------------------------------------------------------------------------------
// The one-time address on this computer that Google sends the browser back to.

#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket kNoSocket = INVALID_SOCKET;
void close_socket(Socket s) { closesocket(s); }
#else
using Socket = int;
constexpr Socket kNoSocket = -1;
void close_socket(Socket s) { ::close(s); }
#endif

class Loopback {
 public:
  ~Loopback() {
    if (listener_ != kNoSocket) close_socket(listener_);
  }

  bool open() {
#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif
    listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener_ == kNoSocket) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // any free port
    if (bind(listener_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(listener_, 4) != 0) return false;
    socklen_t len = sizeof addr;
    if (getsockname(listener_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return false;
    port_ = ntohs(addr.sin_port);
    return true;
  }

  int port() const { return port_; }

  // Waits for the browser's request carrying `code` (or `error`); answers it with a short page.
  // Returns the query string, or "" when cancelled or after `timeout_s`.
  std::string wait(const std::atomic<bool>& cancel, int timeout_s) {
    const uint64_t until = now_ns() + static_cast<uint64_t>(timeout_s) * 1'000'000'000ull;
    while (!cancel && now_ns() < until) {
      fd_set set;
      FD_ZERO(&set);
      FD_SET(listener_, &set);
      timeval tv{0, 250000};
      if (select(static_cast<int>(listener_) + 1, &set, nullptr, nullptr, &tv) <= 0) continue;
      const Socket client = accept(listener_, nullptr, nullptr);
      if (client == kNoSocket) continue;
      std::string request;
      char buf[2048];
      while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384) {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(client, &rs);
        timeval rt{2, 0};
        if (select(static_cast<int>(client) + 1, &rs, nullptr, nullptr, &rt) <= 0) break;
        const int n = static_cast<int>(recv(client, buf, sizeof buf, 0));
        if (n <= 0) break;
        request.append(buf, static_cast<std::size_t>(n));
      }
      // "GET /?code=...&state=... HTTP/1.1"
      const std::size_t sp1 = request.find(' '), sp2 = request.find(' ', sp1 + 1);
      const std::string target = sp1 == std::string::npos || sp2 == std::string::npos ? std::string()
                                                                                       : request.substr(sp1 + 1, sp2 - sp1 - 1);
      const std::size_t q = target.find('?');
      const std::string query = q == std::string::npos ? std::string() : target.substr(q + 1);
      const bool answer = !query_param(query, "code").empty() || !query_param(query, "error").empty();
      const std::string page =
          answer ? "<!doctype html><meta charset=utf-8><title>Prometheus 64</title>"
                   "<body style=\"font-family:sans-serif;background:#17100f;color:#f4f1ee;text-align:center;padding-top:15vh\">"
                   "<h1>All set</h1><p>You can close this tab and go back to Prometheus 64.</p>"
                 : "Not found";
      const std::string reply = std::string(answer ? "HTTP/1.1 200 OK" : "HTTP/1.1 404 Not Found") +
                                "\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: " +
                                std::to_string(page.size()) + "\r\n\r\n" + page;
      send(client, reply.data(), static_cast<int>(reply.size()), 0);
      close_socket(client);
      if (answer) return query;
    }
    return {};
  }

 private:
  Socket listener_ = kNoSocket;
  int port_ = 0;
};

std::string token_error(const net::Response& r) {
  if (r.status == 0) return "Could not reach Google. Check your internet connection";
  json::Value v;
  json::parse(r.body, v);
  const std::string e = v["error"].str();
  if (e == "invalid_client" || e == "unauthorized_client") return "This build of Prometheus 64 isn't registered with Google";
  return "Google refused the sign-in (" + (e.empty() ? "HTTP " + std::to_string(r.status) : e) + "). Try again";
}

}  // namespace

bool configured() { return !client_id().empty(); }

SignIn sign_in(const std::function<void(const std::string&)>& open_url, const std::atomic<bool>& cancel) {
  SignIn res;
  if (!configured()) {
    res.error = "This build of Prometheus 64 doesn't have Google Drive backup switched on";
    return res;
  }
  Loopback lb;
  if (!lb.open()) {
    res.error = "Could not start the sign-in on this computer";
    return res;
  }
  const std::string redirect = "http://127.0.0.1:" + std::to_string(lb.port());
  const std::string verifier = random_text(64), state = random_text(24);
  open_url(auth_url() + "?client_id=" + encode(client_id()) + "&redirect_uri=" + encode(redirect) +
           "&response_type=code&scope=" + encode(kScope) + "&code_challenge=" + challenge_for(verifier) +
           "&code_challenge_method=S256&state=" + state + "&access_type=offline&prompt=consent");
  const std::string query = lb.wait(cancel, 600);
  if (query.empty()) return res;  // cancelled or gave up: nothing to say
  if (query_param(query, "state") != state) {
    res.error = "The sign-in didn't match. Try again";
    return res;
  }
  const std::string code = query_param(query, "code");
  if (code.empty()) {
    res.error = query_param(query, "error") == "access_denied" ? "Google Drive access wasn't allowed. Try again when you're ready"
                                                              : "Google didn't finish the sign-in. Try again";
    return res;
  }
  std::string body = "grant_type=authorization_code&code=" + encode(code) + "&client_id=" + encode(client_id()) +
                     "&redirect_uri=" + encode(redirect) + "&code_verifier=" + verifier;
  if (!client_secret().empty()) body += "&client_secret=" + encode(client_secret());
  const net::Response r = net::request("POST", token_url(), {"Content-Type: application/x-www-form-urlencoded"}, body);
  json::Value v;
  json::parse(r.body, v);
  res.refresh_token = v["refresh_token"].str();
  res.ok = r.status == 200 && !res.refresh_token.empty();
  if (!res.ok) res.error = token_error(r);
  return res;
}

void revoke(const std::string& token) {
  if (token.empty() || std::getenv("P64_GOOGLE_TOKEN")) return;  // tests have nothing to revoke
  net::request("POST", "https://oauth2.googleapis.com/revoke", {"Content-Type: application/x-www-form-urlencoded"},
               "token=" + encode(token), {}, 15);
}

// ---------------------------------------------------------------------------------------------
// Drive

bool Drive::access() {
  if (!access_.empty() && now_ns() < access_until_ns_) return true;
  std::string body = "grant_type=refresh_token&refresh_token=" + encode(refresh_) + "&client_id=" + encode(client_id());
  if (!client_secret().empty()) body += "&client_secret=" + encode(client_secret());
  const net::Response r = net::request("POST", token_url(), {"Content-Type: application/x-www-form-urlencoded"}, body, {}, 30);
  json::Value v;
  json::parse(r.body, v);
  if (r.status == 200 && !v["access_token"].str().empty()) {
    access_ = v["access_token"].str();
    const double life = v["expires_in"].num() > 0 ? v["expires_in"].num() : 3600;
    access_until_ns_ = now_ns() + static_cast<uint64_t>((life - 120) * 1e9);
    return true;
  }
  if (r.status == 0) {
    problem_ = Problem::Offline;
    error_ = "Offline. Your saves are kept here and back up when the connection returns";
  } else if (v["error"].str() == "invalid_grant") {
    problem_ = Problem::SignedOut;
    error_ = "Google signed this computer out of Drive. Connect again to keep backing up";
  } else {
    problem_ = Problem::Other;
    error_ = token_error(r);
  }
  return false;
}

net::Response Drive::call(const std::string& method, const std::string& url, std::vector<std::string> headers,
                          const std::string& body) {
  net::Response r;
  problem_ = Problem::None;
  error_.clear();
  if (!access()) return r;
  headers.push_back("Authorization: Bearer " + access_);
  r = net::request(method, url, headers, body, {}, 120);
  if (r.status == 401) {  // the access token ran out early: once more with a fresh one
    access_.clear();
    if (!access()) return r;
    headers.back() = "Authorization: Bearer " + access_;
    r = net::request(method, url, headers, body, {}, 120);
  }
  return r;
}

bool Drive::fail(const net::Response& r) {
  if (problem_ != Problem::None && r.status == 0 && r.error.empty()) return false;  // access() said why
  json::Value v;
  json::parse(r.body, v);
  const std::string reason = v["error"]["errors"][0]["reason"].str();
  if (r.status == 0) {
    problem_ = Problem::Offline;
    error_ = "Offline. Your saves are kept here and back up when the connection returns";
  } else if (reason == "storageQuotaExceeded") {
    problem_ = Problem::Full;
    error_ = "Your Google Drive is full. Free some space, then press Back up now";
  } else if (r.status == 401 || r.status == 403) {
    problem_ = Problem::SignedOut;
    error_ = "Google Drive refused access. Connect again to keep backing up";
  } else {
    problem_ = Problem::Other;
    const std::string m = v["error"]["message"].str();
    error_ = "Google Drive had a problem (" + (m.empty() ? "HTTP " + std::to_string(r.status) : m) + "). It tries again later";
  }
  return false;
}

bool Drive::account(std::string& email) {
  const net::Response r = call("GET", api_url() + "/about?fields=user(emailAddress)", {});
  if (r.status != 200) return fail(r);
  json::Value v;
  json::parse(r.body, v);
  email = v["user"]["emailAddress"].str();
  return true;
}

bool Drive::list(const std::string& parent, std::vector<File>& out) {
  out.clear();
  std::string page;
  do {
    const std::string q = "'" + parent + "' in parents and trashed = false";
    std::string url = api_url() + "/files?pageSize=1000&fields=" + encode("nextPageToken,files(id,name,mimeType,appProperties)") +
                      "&q=" + encode(q);
    if (!page.empty()) url += "&pageToken=" + encode(page);
    const net::Response r = call("GET", url, {});
    if (r.status != 200) return fail(r);
    json::Value v;
    if (!json::parse(r.body, v)) return fail(r);
    for (const auto& f : v["files"].array) {
      out.push_back({f["id"].str(), f["name"].str(), f["appProperties"]["sha256"].str(), f["mimeType"].str() == kFolderType});
    }
    page = v["nextPageToken"].str();
  } while (!page.empty());
  return true;
}

bool Drive::create_folder(const std::string& parent, const std::string& name, std::string& id, const std::string& tag) {
  std::string meta = "{\"name\":" + json::quote(name) + ",\"mimeType\":\"" + kFolderType + "\",\"parents\":[" +
                     json::quote(parent) + "]";
  if (!tag.empty()) meta += ",\"appProperties\":{\"prometheus\":" + json::quote(tag) + "}";
  meta += "}";
  const net::Response r = call("POST", api_url() + "/files?fields=id", {"Content-Type: application/json"}, meta);
  if (r.status != 200) return fail(r);
  json::Value v;
  json::parse(r.body, v);
  id = v["id"].str();
  return !id.empty() || fail(r);
}

namespace {

// Drive's multipart upload: the file's details and its content in one request.
std::string multipart(const std::string& meta, const std::string& data, const std::string& boundary) {
  return "--" + boundary + "\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n" + meta + "\r\n--" + boundary +
         "\r\nContent-Type: application/octet-stream\r\n\r\n" + data + "\r\n--" + boundary + "--\r\n";
}

std::string sha_props(const std::string& data) {
  return "\"appProperties\":{\"sha256\":\"" + savekit::sha256_hex(savekit::Bytes(data.begin(), data.end())) + "\"}";
}

}  // namespace

bool Drive::upload(const std::string& parent, const std::string& name, const std::string& data, std::string& id) {
  const std::string boundary = "p64-" + random_text(24);
  const std::string meta = "{\"name\":" + json::quote(name) + ",\"parents\":[" + json::quote(parent) + "]," + sha_props(data) + "}";
  const net::Response r = call("POST", upload_url() + "/files?uploadType=multipart&fields=id",
                               {"Content-Type: multipart/related; boundary=" + boundary}, multipart(meta, data, boundary));
  if (r.status != 200) return fail(r);
  json::Value v;
  json::parse(r.body, v);
  id = v["id"].str();
  return !id.empty() || fail(r);
}

bool Drive::update(const std::string& id, const std::string& data) {
  const std::string boundary = "p64-" + random_text(24);
  const std::string meta = "{" + sha_props(data) + "}";
  const net::Response r = call("PATCH", upload_url() + "/files/" + id + "?uploadType=multipart&fields=id",
                               {"Content-Type: multipart/related; boundary=" + boundary}, multipart(meta, data, boundary));
  return r.status == 200 || fail(r);
}

bool Drive::download(const std::string& id, std::string& data) {
  const net::Response r = call("GET", api_url() + "/files/" + id + "?alt=media", {});
  if (r.status != 200) return fail(r);
  data = r.body;
  return true;
}

bool Drive::find_folder(const std::string& tag, const std::string& name, std::string& id) {
  id.clear();
  // Only this app's files are visible (drive.file), so these searches never see anything else.
  const std::string by_tag = "appProperties has { key='prometheus' and value='" + tag + "' } and mimeType = '" +
                             kFolderType + "' and trashed = false";
  const std::string by_name = "name = '" + name + "' and mimeType = '" + kFolderType + "' and trashed = false";
  for (const std::string& q : {by_tag, by_name}) {
    const net::Response r = call("GET", api_url() + "/files?pageSize=10&orderBy=createdTime&fields=" +
                                            encode("files(id)") + "&q=" + encode(q), {});
    if (r.status != 200) return fail(r);
    json::Value v;
    json::parse(r.body, v);
    id = v["files"][0]["id"].str();  // the oldest one, if several
    if (!id.empty()) return true;
  }
  return true;
}

bool Drive::alive(const std::string& id) {
  const net::Response r = call("GET", api_url() + "/files/" + id + "?fields=id,trashed", {});
  if (r.status == 404) return false;
  if (r.status != 200) return fail(r);
  json::Value v;
  json::parse(r.body, v);
  return !v["trashed"].truthy();
}

bool Drive::rename(const std::string& id, const std::string& name) {
  const net::Response r = call("PATCH", api_url() + "/files/" + id + "?fields=id", {"Content-Type: application/json"},
                               "{\"name\":" + json::quote(name) + "}");
  return r.status == 200 || fail(r);
}

bool Drive::remove(const std::string& id) {
  const net::Response r = call("DELETE", api_url() + "/files/" + id, {});
  return r.status == 204 || r.status == 200 || r.status == 404 || fail(r);
}

}  // namespace p64::google
