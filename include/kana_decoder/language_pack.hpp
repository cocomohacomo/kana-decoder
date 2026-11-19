#pragma once

#include "kana_decoder/engine.hpp"
#include "kana_decoder/surge_language.hpp"

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

// Immutable resources pinned by a decode/streaming session. The host depends on
// this language-neutral contract rather than owning language tables itself.
class LanguagePack {
 public:
  virtual ~LanguagePack() = default;
  virtual const LanguagePackIdentity& identity() const = 0;
  virtual const LexiconTrie& lexicon() const = 0;
  virtual const MaskedNgramScorer& ngram_scorer() const = 0;
  virtual const ConnectionModel& connections() const = 0;
  virtual const CorrectionOptions& correction_options() const = 0;
  virtual const surge::LanguageAdapter& adapter() const = 0;
};

std::shared_ptr<const LanguagePack> load_language_pack_snapshot(
    const std::string& directory, std::string* error = nullptr);

}  // namespace kana_decoder
