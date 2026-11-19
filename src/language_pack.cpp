#include "kana_decoder/language_pack.hpp"

#include "kana_decoder/pack_manifest.hpp"
#include "surge/sha256.hpp"

#include <fstream>
#include <regex>
#include <sstream>
#include <utility>

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
  if (!input.eof() || manifest.size() > 1024u * 1024u) {
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

class JapaneseLanguagePack final : public LanguagePack {
 public:
  LanguagePackIdentity identity;
  LexiconTrie lexicon_data;
  MaskedNgramScorer scorer_data;
  ConnectionModel connections_data;
  CorrectionOptions corrections_data;
  surge::JapaneseLanguageAdapter adapter_data;

  const LanguagePackIdentity& identity() const override { return identity; }
  const LexiconTrie& lexicon() const override { return lexicon_data; }
  const MaskedNgramScorer& ngram_scorer() const override { return scorer_data; }
  const ConnectionModel& connections() const override { return connections_data; }
  const CorrectionOptions& correction_options() const override { return corrections_data; }
  const surge::LanguageAdapter& adapter() const override { return adapter_data; }
};

}  // namespace

std::shared_ptr<const LanguagePack> load_language_pack_snapshot(
    const std::string& directory, std::string* error) {
  if (!verify_pack_manifest(directory, error)) return {};
  auto pack = std::make_shared<JapaneseLanguagePack>();
  if (!read_identity(directory, pack->identity, error)) return {};
  if (pack->identity.language_tag != pack->adapter_data.language_tag()) {
    if (error) *error = "LANGUAGE_PACK_ADAPTER_MISMATCH";
    return {};
  }
  if (!load_language_pack(directory, pack->lexicon_data, pack->scorer_data,
                          nullptr, &pack->connections_data,
                          &pack->corrections_data)) {
    if (error) *error = "LANGUAGE_PACK_RESOURCE_LOAD_FAILED";
    return {};
  }
  if (error) error->clear();
  return pack;
}

}  // namespace kana_decoder
