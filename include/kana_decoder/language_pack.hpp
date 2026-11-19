#pragma once

#include "kana_decoder/engine.hpp"
#include "kana_decoder/surge_language.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace kana_decoder {

struct LanguagePackIdentity {
  std::string pack_id;
  std::string language_tag;
  std::string version;
  std::string digest;
};

// Resource policy shared by all language providers. It deliberately contains
// no dictionary, scorer, morphology, POS, or language-specific policy objects.
struct LanguageDecodeOptions {
  std::size_t max_paths{8};
  std::size_t beam_width{8};
  std::size_t max_input_codepoints{256};
  std::size_t max_lattice_edges{4096};
  std::size_t max_lattice_candidates{32768};
  std::size_t max_expanded_states{262144};
};

// Immutable resources pinned by a decode/streaming session. The host depends on
// this language-neutral contract rather than owning language tables itself.
class LanguagePack {
 public:
  virtual ~LanguagePack() = default;
  virtual const LanguagePackIdentity& identity() const = 0;
  virtual std::vector<DecodedPath> decode(
      const std::string& reading,
      const LanguageDecodeOptions& options = {}) const = 0;
  virtual bool append(StreamingSession& session, const std::string& chunk,
                      StreamingSnapshot& snapshot,
                      const LanguageDecodeOptions& options = {}) const = 0;
  virtual const surge::LanguageAdapter& adapter() const = 0;

};

std::shared_ptr<const LanguagePack> load_language_pack_snapshot(
    const std::string& directory, std::string* error = nullptr);

}  // namespace kana_decoder
