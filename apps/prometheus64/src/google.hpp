// Google sign-in and the few Google Drive calls the backup needs.
//
// Sign-in is Google's flow for installed apps: the browser opens Google's page, and Google sends
// the browser back to a one-time address on this computer (127.0.0.1 and a free port) with a code,
// which the app swaps for a refresh token (with PKCE, so the code is useless to anyone else). The
// app only asks for `drive.file`: it sees the files it made itself and nothing else in Drive.
//
// The app's Google client ID (and the client "secret", which Google gives installed apps but does
// not treat as secret) come from the build (P64_GOOGLE_CLIENT_ID, P64_GOOGLE_CLIENT_SECRET), never
// from the repository. Environment variables with the same names override them, and P64_GOOGLE_AUTH,
// P64_GOOGLE_TOKEN, P64_GOOGLE_API and P64_GOOGLE_UPLOAD point everything at a stand-in for tests.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net.hpp"

namespace p64::google {

bool configured();  // this build has a Google client ID

struct SignIn {
  bool ok = false;
  std::string refresh_token;
  std::string error;  // plain language; empty when cancelled
};

// Blocking (until the player finishes in the browser, gives up, or `cancel` is set): run it on a
// worker thread. `open_url` opens the browser at Google's page.
SignIn sign_in(const std::function<void(const std::string&)>& open_url, const std::atomic<bool>& cancel);

// Tells Google to forget the token (Disconnect). Best effort.
void revoke(const std::string& token);

class Drive {
 public:
  struct File {
    std::string id, name, sha256;  // sha256: of the content, recorded when the app uploaded it
    bool folder = false;
  };
  enum class Problem { None, Offline, SignedOut, Full, Other };

  explicit Drive(std::string refresh_token) : refresh_(std::move(refresh_token)) {}

  bool account(std::string& email);
  // Folders and files made by this app, inside `parent` ("root" is My Drive).
  bool list(const std::string& parent, std::vector<File>& out);
  // `tag` (optional) is stored on the folder as a hidden property, so it can be found again after the
  // player moves or renames it.
  bool create_folder(const std::string& parent, const std::string& name, std::string& id, const std::string& tag = {});
  // The app's folder carrying `tag`, anywhere in the Drive; or else, a folder of this app named `name`.
  // `id` is empty when there is none.
  bool find_folder(const std::string& tag, const std::string& name, std::string& id);
  // Whether a file or folder still exists and isn't in the bin. False with no problem() when it's gone.
  bool alive(const std::string& id);
  bool upload(const std::string& parent, const std::string& name, const std::string& data, std::string& id);
  bool update(const std::string& id, const std::string& data);
  bool download(const std::string& id, std::string& data);
  bool remove(const std::string& id);
  bool rename(const std::string& id, const std::string& name);

  Problem problem() const { return problem_; }
  const std::string& error() const { return error_; }  // plain language

 private:
  bool access();
  net::Response call(const std::string& method, const std::string& url, std::vector<std::string> headers,
                     const std::string& body = {});
  bool fail(const net::Response& r);

  std::string refresh_, access_;
  uint64_t access_until_ns_ = 0;
  Problem problem_ = Problem::None;
  std::string error_;
};

}  // namespace p64::google
