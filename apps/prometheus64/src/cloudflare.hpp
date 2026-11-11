// One-time setup of a group's relay for online play, on the player's free Cloudflare account.
// The app installs the relay (relay/src/index.js, built into the app) with an API key the player
// creates on Cloudflare's site; that API key is used for this setup only and never stored. The
// setup makes a new random group key and gives it to the relay: only apps holding it (from the
// group's key file) can use the relay.
#pragma once

#include <functional>
#include <string>

namespace p64::cloudflare {

// Cloudflare's key page with the two permissions the setup needs already selected.
std::string token_page_url();
std::string signup_url();

struct Result {
  bool ok = false;
  std::string relay_url;  // e.g. "wss://prometheus-relay.alex.workers.dev"
  std::string group_key;  // the new group key, for the key file
  std::string message;    // plain-language outcome or problem
  std::string fix_url;    // a page that fixes the problem, when there is one
};

// Blocking, a few seconds: run it on a worker thread. `progress` gets short status lines.
// Installing again replaces the group key, so older key files stop working.
// With `keep_key` (and the relay address the group's key file names, `expect_url`), it updates
// the relay instead: the key stays, so the key files do, and nothing is touched unless this
// account is the one the group's relay is on.
Result install_relay(const std::string& token, const std::function<void(const std::string&)>& progress,
                     const std::string& keep_key = {}, const std::string& expect_url = {});

// A new random group key: 32 bytes, as 43 URL-safe characters.
std::string new_group_key();

}  // namespace p64::cloudflare
