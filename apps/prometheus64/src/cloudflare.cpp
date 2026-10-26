#include "cloudflare.hpp"

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <cctype>
#include <cstdlib>

#include "json.hpp"
#include "net.hpp"
#include "relay_script.hpp"

namespace p64::cloudflare {
namespace {

constexpr const char* kScriptName = "prometheus64-relay";
constexpr const char* kCompatibilityDate = "2026-09-01";

std::string api_base() {
  // Tests point this at a stand-in for Cloudflare's API.
  if (const char* v = std::getenv("P64_CLOUDFLARE_API")) return v;
  return "https://api.cloudflare.com/client/v4";
}

std::string url_encode(const std::string& s) {
  std::string out;
  static const char* hex = "0123456789ABCDEF";
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

struct Api {
  std::string token;
  json::Value last;  // body of the last answer
  long status = 0;
  std::string error;

  bool call(const std::string& method, const std::string& path, const std::string& body = {},
            const std::vector<net::Part>& parts = {}, std::vector<std::string> headers = {}) {
    headers.push_back("Authorization: Bearer " + token);
    if (parts.empty() && !body.empty()) headers.push_back("Content-Type: application/json");
    const net::Response r = net::request(method, api_base() + path, headers, body, parts);
    status = r.status;
    error = r.error;
    json::parse(r.body, last);
    return r.status >= 200 && r.status < 300 && last["success"].truthy();
  }

  // Cloudflare's first error code, 0 if none.
  int code() const { return static_cast<int>(last["errors"][0]["code"].num()); }
  std::string what() const {
    if (!error.empty()) return "Could not reach Cloudflare (" + error + "). Check your internet connection";
    const std::string m = last["errors"][0]["message"].str();
    return m.empty() ? "Cloudflare answered HTTP " + std::to_string(status) : m;
  }
};

// A workers.dev name from the account name: lowercase letters, digits and dashes.
std::string suggest_subdomain(const std::string& account_name) {
  std::string out;
  for (unsigned char c : account_name) {
    if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    else if (!out.empty() && out.back() != '-') out += '-';
    if (out.size() >= 20) break;
  }
  while (!out.empty() && out.back() == '-') out.pop_back();
  if (out.size() < 3) out = "prometheus";
  return out + "-" + std::to_string(1000 + SDL_rand(9000));
}

}  // namespace

std::string signup_url() { return "https://dash.cloudflare.com/sign-up"; }

std::string token_page_url() {
  const std::string permissions = R"([{"key":"workers_scripts","type":"edit"},{"key":"account_settings","type":"read"}])";
  return "https://dash.cloudflare.com/profile/api-tokens?permissionGroupKeys=" + url_encode(permissions) +
         "&accountId=*&zoneId=all&name=" + url_encode("Prometheus 64 online play");
}

std::string relay_url_for(const std::string& subdomain) {
  return std::string("wss://") + kScriptName + "." + subdomain + ".workers.dev";
}

Result install_relay(const std::string& raw_token, const std::function<void(const std::string&)>& progress) {
  Result res;
  std::string token;
  for (char c : raw_token) {
    if (!std::isspace(static_cast<unsigned char>(c))) token += c;  // pasting often adds spaces or a line break
  }
  if (token.size() < 20) {
    res.message = "That doesn't look like a whole key. Copy it again from Cloudflare and paste it here";
    return res;
  }
  Api api;
  api.token = token;

  progress("Checking your key...");
  if (!api.call("GET", "/accounts?per_page=50")) {
    if (api.status == 401 || api.status == 403 || api.code() == 10000 || api.code() == 9109) {
      res.message = "Cloudflare didn't accept this key. Create it again with the button above and copy all of it";
    } else {
      res.message = api.what();
    }
    return res;
  }
  const std::string account_id = api.last["result"][0]["id"].str();
  const std::string account_name = api.last["result"][0]["name"].str();  // copied: api.last changes with every call
  if (account_id.empty()) {
    res.message = "This key can't see your Cloudflare account. Create it again with the button above: it needs "
                  "\"Account Settings: Read\"";
    return res;
  }
  const std::string base = "/accounts/" + account_id + "/workers";

  progress("Finding your free workers.dev address...");
  std::string subdomain;
  if (api.call("GET", base + "/subdomain")) {
    subdomain = api.last["result"]["subdomain"].str();
  } else if (api.code() == 10007) {
    // New accounts have none yet: pick one.
    for (int attempt = 0; attempt < 3 && subdomain.empty(); ++attempt) {
      const std::string name = suggest_subdomain(account_name);
      if (api.call("PUT", base + "/subdomain", "{\"subdomain\":" + json::quote(name) + "}")) {
        subdomain = api.last["result"]["subdomain"].str();
      }
    }
    if (subdomain.empty()) {
      res.message = "Your account doesn't have a workers.dev address yet. Open the page below once (it creates one), "
                    "then come back and press Set up again";
      res.fix_url = "https://dash.cloudflare.com/" + account_id + "/workers/onboarding";
      return res;
    }
  } else {
    res.message = api.what();
    return res;
  }

  progress("Installing the relay...");
  // The room storage is created once (migration "v1"); later installs only update the code.
  bool exists = api.call("GET", base + "/services/" + kScriptName);
  const std::string tag = exists ? api.last["result"]["default_environment"]["script"]["migration_tag"].str() : "";
  std::string metadata = std::string("{\"main_module\":\"index.js\",\"compatibility_date\":\"") + kCompatibilityDate +
                         "\",\"bindings\":[{\"type\":\"durable_object_namespace\",\"name\":\"ROOMS\",\"class_name\":\"Room\"}]";
  if (tag != "v1") metadata += ",\"migrations\":{\"new_tag\":\"v1\",\"steps\":[{\"new_sqlite_classes\":[\"Room\"]}]}";
  metadata += "}";
  const std::vector<net::Part> parts = {
      {"metadata", "", "application/json", metadata},
      {"index.js", "index.js", "application/javascript+module", kRelayScript},
  };
  if (!api.call("PUT", base + "/scripts/" + kScriptName, {}, parts)) {
    res.message = "Cloudflare refused the relay: " + api.what();
    return res;
  }
  if (!api.call("POST", base + "/scripts/" + kScriptName + "/subdomain",
                "{\"enabled\":true,\"previews_enabled\":false}",
                {}, {"Cloudflare-Workers-Script-Api-Date: 2025-08-01"})) {
    res.message = "The relay is installed but couldn't be switched on: " + api.what();
    return res;
  }

  res.ok = true;
  res.subdomain = subdomain;
  res.relay_host = std::string(kScriptName) + "." + subdomain + ".workers.dev";

  // A brand-new address can take a minute to appear on the internet.
  progress("Testing the relay...");
  const bool stand_in = std::getenv("P64_CLOUDFLARE_API") != nullptr;  // tests: there is no real address
  for (int i = 0; i < 12 && !stand_in; ++i) {
    const net::Response r = net::request("GET", "https://" + res.relay_host + "/", {}, {}, {}, 10);
    if (r.status == 200 && r.body.find("Prometheus 64 relay") != std::string::npos) {
      res.message = "Online play is ready";
      return res;
    }
    SDL_Delay(5000);
  }
  res.message = "Online play is set up. Your new address can take a few minutes to start working";
  return res;
}

}  // namespace p64::cloudflare
