#include "archive.hpp"

#include <algorithm>
#include <cctype>
#include <memory>

#include <archive.h>
#include <archive_entry.h>

namespace fs = std::filesystem;

namespace p64::archive {
namespace {

struct Closer {
  void operator()(struct archive* a) const { archive_read_free(a); }
};
using Reader = std::unique_ptr<struct archive, Closer>;

std::string message(struct archive* a, const char* fallback) {
  const char* m = archive_error_string(a);
  std::string s = m && *m ? m : fallback;
  std::string lower = s;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower.find("passphrase") != std::string::npos || lower.find("encrypt") != std::string::npos) {
    return "the archive is password-protected";
  }
  return s;
}

std::string open(const fs::path& file, Reader& reader) {
  reader.reset(archive_read_new());
  struct archive* a = reader.get();
  if (!a) return "out of memory";
  archive_read_support_format_zip(a);
  archive_read_support_format_7zip(a);
  archive_read_support_format_rar(a);
  archive_read_support_format_rar5(a);
#ifdef _WIN32
  const int r = archive_read_open_filename_w(a, file.c_str(), 1 << 16);
#else
  const int r = archive_read_open_filename(a, file.c_str(), 1 << 16);
#endif
  if (r != ARCHIVE_OK) return message(a, "could not open the archive");
  return {};
}

std::string entry_name(struct archive_entry* e) {
  if (const char* n = archive_entry_pathname_utf8(e)) return n;
  if (const char* n = archive_entry_pathname(e)) return n;
  return {};
}

}  // namespace

bool is_archive(const fs::path& file) {
  std::string ext = file.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext == ".zip" || ext == ".7z" || ext == ".rar";
}

std::string scan(const fs::path& file, std::size_t head,
                 const std::function<bool(const Entry&, const std::vector<uint8_t>&)>& visit) {
  Reader reader;
  if (std::string err = open(file, reader); !err.empty()) return err;
  struct archive* a = reader.get();
  struct archive_entry* e = nullptr;
  std::vector<uint8_t> bytes;
  for (;;) {
    const int r = archive_read_next_header(a, &e);
    if (r == ARCHIVE_EOF) return {};
    if (r < ARCHIVE_WARN) return message(a, "the archive is damaged");
    if (archive_entry_filetype(e) != AE_IFREG) continue;
    Entry entry{entry_name(e), archive_entry_size_is_set(e) ? archive_entry_size(e) : -1};
    bytes.assign(head, 0);
    std::size_t got = 0;
    while (got < head) {
      const la_ssize_t n = archive_read_data(a, bytes.data() + got, head - got);
      if (n < 0) return message(a, "could not read the archive");
      if (n == 0) break;
      got += static_cast<std::size_t>(n);
    }
    bytes.resize(got);
    if (!visit(entry, bytes)) return {};
  }
}

std::string extract(const fs::path& file, const std::string& name, std::size_t max_size, std::vector<uint8_t>& out) {
  Reader reader;
  if (std::string err = open(file, reader); !err.empty()) return err;
  struct archive* a = reader.get();
  struct archive_entry* e = nullptr;
  for (;;) {
    const int r = archive_read_next_header(a, &e);
    if (r == ARCHIVE_EOF) return "the game is no longer in the archive";
    if (r < ARCHIVE_WARN) return message(a, "the archive is damaged");
    if (archive_entry_filetype(e) != AE_IFREG || entry_name(e) != name) continue;
    if (archive_entry_size_is_set(e) && archive_entry_size(e) > static_cast<la_int64_t>(max_size)) {
      return "the file in the archive is too large to be a game";
    }
    out.clear();
    uint8_t buffer[1 << 16];
    for (;;) {
      const la_ssize_t n = archive_read_data(a, buffer, sizeof buffer);
      if (n < 0) return message(a, "could not unpack the game");
      if (n == 0) return {};
      if (out.size() + static_cast<std::size_t>(n) > max_size) return "the file in the archive is too large to be a game";
      out.insert(out.end(), buffer, buffer + n);
    }
  }
}

}  // namespace p64::archive
