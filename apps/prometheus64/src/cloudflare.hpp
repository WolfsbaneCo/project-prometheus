// One-time setup of a player's own relay for online play, on their free Cloudflare account.
// The app installs the relay (relay/src/index.js, built into the app) with a key the player
// creates on Cloudflare's site. The key is used for this setup only and never stored.
#pragma once

#include <functional>
#include <string>

namespace p64::cloudflare {

// Cloudflare's key page with the two permissions the setup needs already selected.
std::string token_page_url();
std::string signup_url();

struct Result {
  bool ok = false;
  std::string relay_host;  // e.g. "prometheus64-relay.alex.workers.dev"
  std::string subdomain;   // e.g. "alex": goes into invite codes
  std::string message;     // plain-language outcome or problem
  std::string fix_url;     // a page that fixes the problem, when there is one
};

// Blocking, a few seconds: run it on a worker thread. `progress` gets short status lines.
Result install_relay(const std::string& token, const std::function<void(const std::string&)>& progress);

// The relay's address from an invite subdomain ("alex" -> wss://prometheus64-relay.alex.workers.dev).
std::string relay_url_for(const std::string& subdomain);

}  // namespace p64::cloudflare
