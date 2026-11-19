#include "kana_decoder/language_pack.hpp"

#include "language_pack_internal.hpp"
#include "surge/sha256.hpp"

#include <fstream>
#include <iterator>
#include <regex>
#include <string>

namespace kana_decoder {
namespace {

bool read_identity(const std::string& directory, LanguagePackIdentity& identity,
                   std::string* error) {
  std::string base = directory.empty() ? "." : directory;
  if (!base.empty() && base.back() != '/' && base.back() != '\\') base.push_back('/');
  std::ifstream input(base + "manifest.json", std::ios::binary);
  if (!input) {
    if (error) *error = "LANGUAGE_PACK_MANIFEST_OPEN_FAILED";
    return false;
  }
  std::string manifest((std::istreambuf_iterator<char>(input)), {});
  if (input.bad() || manifest.size() > 1024u * 1024u) {
    if (error) *error = "LANGUAGE_PACK_MANIFEST_INVALID";
    return false;
  }
  const auto capture = [&](const char* key, std::string& value) {
    const std::regex pattern(std::string("\\\"") + key +
        R"rx(\s*:\s*"([^"]+)")rx");
    std::smatch match;
    if (!std::regex_search(manifest, match, pattern) || match.size() < 2) return false;
    value = match[1].str();
    return !value.empty();
  };
  if (!capture("pack_id", identity.pack_id) ||
      !capture("language_id", identity.language_tag) ||
      !capture("pack_version", identity.version)) {
    if (error) *error = "LANGUAGE_PACK_IDENTITY_MISSING";
    return false;
  }
  identity.digest = ::surge::sha256_hex(manifest);
  return true;
}

}  // namespace

std::shared_ptr<const LanguagePack> load_language_pack_snapshot(
    const std::string& directory, std::string* error) {
  LanguagePackIdentity identity;
  if (!read_identity(directory, identity, error)) return {};
  if (identity.language_tag == "ja-JP") {
    return language_sets::load_japanese_language_pack(
        directory, std::move(identity), error);
  }
  if (error) *error = "LANGUAGE_PACK_LANGUAGE_UNSUPPORTED";
  return {};
}

}  // namespace kana_decoder
