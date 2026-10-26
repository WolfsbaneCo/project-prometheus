// Keeps one small secret (the Google refresh token) in the operating system's credential store:
// Windows Credential Manager on Windows; elsewhere a file only this user can read, in the app's
// own data folder. Brief 9.4.
#pragma once

#include <filesystem>
#include <string>

namespace p64::secret {

// `name` identifies the secret ("Prometheus 64/Google Drive"); `fallback_dir` is where the file
// goes on systems without a credential store.
bool save(const std::string& name, const std::string& value, const std::filesystem::path& fallback_dir);
std::string load(const std::string& name, const std::filesystem::path& fallback_dir);
void erase(const std::string& name, const std::filesystem::path& fallback_dir);

}  // namespace p64::secret
