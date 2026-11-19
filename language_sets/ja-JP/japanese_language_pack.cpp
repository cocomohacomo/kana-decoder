#include "language_pack_internal.hpp"
#include "kana_decoder/japanese_decoder.hpp"
#include "kana_decoder/japanese_language.hpp"

#include "kana_decoder/pack_manifest.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace kana_decoder::language_sets {
namespace {

class JapaneseLanguagePack final : public LanguagePack {
 public:
  explicit JapaneseLanguagePack(LanguagePackIdentity value)
      : identity_data(std::move(value)) {}

  LanguagePackIdentity identity_data;
  LexiconTrie lexicon_data;
  MaskedNgramScorer scorer_data;
  ConnectionModel connections_data;
  CorrectionOptions corrections_data;
  surge::JapaneseLanguageAdapter adapter_data;

  std::vector<DecodedPath> decode(
      const std::string& reading,
      const LanguageDecodeOptions& options) const override {
    DecoderOptions japanese_options;
    japanese_options.max_paths = std::min<std::size_t>(options.max_paths, 8);
    japanese_options.beam_width = std::min<std::size_t>(options.beam_width, 8);
    japanese_options.max_input_codepoints = options.max_input_codepoints;
    japanese_options.max_lattice_edges = options.max_lattice_edges;
    japanese_options.max_lattice_candidates = options.max_lattice_candidates;
    japanese_options.max_expanded_states = options.max_expanded_states;
    return decode_reading(reading, lexicon_data, scorer_data,
                          japanese_options, &connections_data);
  }

  bool append(StreamingSession& session, const std::string& chunk,
              StreamingSnapshot& snapshot,
              const LanguageDecodeOptions& options) const override {
    if (chunk.empty() || !is_valid_utf8(chunk)) return false;
    const std::string combined = session.reading() + chunk;
    auto paths = decode(combined, options);
    return session.append_decoded(chunk, std::move(paths), snapshot,
                                  options.max_input_codepoints);
  }

  const LanguagePackIdentity& identity() const override { return identity_data; }
  const surge::LanguageAdapter& adapter() const override { return adapter_data; }
};

}  // namespace

std::shared_ptr<const LanguagePack> load_japanese_language_pack(
    const std::string& directory, LanguagePackIdentity identity,
    std::string* error) {
  auto pack = std::make_shared<JapaneseLanguagePack>(std::move(identity));
  if (!load_language_pack(directory, pack->lexicon_data, pack->scorer_data,
                          nullptr, &pack->connections_data,
                          &pack->corrections_data)) {
    if (error) *error = "LANGUAGE_PACK_RESOURCE_LOAD_FAILED";
    return {};
  }
  if (pack->identity_data.language_tag != pack->adapter_data.language_tag()) {
    if (error) *error = "LANGUAGE_PACK_ADAPTER_MISMATCH";
    return {};
  }
  if (error) error->clear();
  return pack;
}

}  // namespace kana_decoder::language_sets
