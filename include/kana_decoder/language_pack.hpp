#pragma once

#include "kana_decoder/engine.hpp"
#include "kana_decoder/surge_language.hpp"

#include <algorithm>
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

  // Transitional source-compatibility bridge for callers using the former
  // Japanese DecoderOptions. New host code must use LanguageDecodeOptions.
  std::vector<DecodedPath> decode(const std::string& reading,
                                  const DecoderOptions& legacy) const {
    LanguageDecodeOptions options;
    options.max_paths = std::min<std::size_t>(legacy.max_paths, 8);
    options.beam_width = std::min<std::size_t>(legacy.beam_width, 8);
    options.max_input_codepoints = legacy.max_input_codepoints;
    options.max_lattice_edges = legacy.max_lattice_edges;
    options.max_lattice_candidates = legacy.max_lattice_candidates;
    options.max_expanded_states = legacy.max_expanded_states;
    return decode(reading, options);
  }
};

std::shared_ptr<const LanguagePack> load_language_pack_snapshot(
    const std::string& directory, std::string* error = nullptr);

}  // namespace kana_decoder
