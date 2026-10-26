#include "secret_store.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <wincred.h>
#else
#include <sys/stat.h>
#endif

#include <cctype>
#include <fstream>
#include <iterator>

namespace p64::secret {

#if defined(_WIN32)

namespace {
std::wstring wide(const std::string& s) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring out(static_cast<std::size_t>(n > 0 ? n - 1 : 0), L'\0');
  if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
  return out;
}
}  // namespace

bool save(const std::string& name, const std::string& value, const std::filesystem::path&) {
  std::wstring target = wide(name);
  CREDENTIALW cred{};
  cred.Type = CRED_TYPE_GENERIC;
  cred.TargetName = target.data();
  cred.CredentialBlobSize = static_cast<DWORD>(value.size());
  cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(value.data()));
  cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
  return CredWriteW(&cred, 0) != 0;
}

std::string load(const std::string& name, const std::filesystem::path&) {
  PCREDENTIALW cred = nullptr;
  if (!CredReadW(wide(name).c_str(), CRED_TYPE_GENERIC, 0, &cred)) return {};
  std::string value(reinterpret_cast<const char*>(cred->CredentialBlob), cred->CredentialBlobSize);
  CredFree(cred);
  return value;
}

void erase(const std::string& name, const std::filesystem::path&) { CredDeleteW(wide(name).c_str(), CRED_TYPE_GENERIC, 0); }

#else

namespace {
std::filesystem::path file_for(const std::string& name, const std::filesystem::path& dir) {
  std::string safe;
  for (char c : name) safe += std::isalnum(static_cast<unsigned char>(c)) ? c : '-';
  return dir / ("." + safe + ".secret");
}
}  // namespace

bool save(const std::string& name, const std::string& value, const std::filesystem::path& dir) {
  const auto file = file_for(name, dir);
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return false;
  }
  chmod(file.c_str(), S_IRUSR | S_IWUSR);  // only this user, before the secret goes in
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  out << value;
  return static_cast<bool>(out);
}

std::string load(const std::string& name, const std::filesystem::path& dir) {
  std::ifstream in(file_for(name, dir), std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void erase(const std::string& name, const std::filesystem::path& dir) {
  std::error_code ec;
  std::filesystem::remove(file_for(name, dir), ec);
}

#endif

}  // namespace p64::secret
