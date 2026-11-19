#pragma once

#include "kana_decoder/japanese_decoder.hpp"
#include "kana_decoder/surge_language.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace kana_decoder::surge {

class JapaneseLanguageAdapter final : public LanguageAdapter {
 public:
  std::string_view language_tag() const override { return "ja-JP"; }
  bool make_hypothesis(const ::kana_decoder::Candidate& candidate, double candidate_score,
                       Span span, std::string_view hypothesis_id,
                       Hypothesis& output) const override;
  bool make_entity(std::string_view reading, std::string_view entity_id,
                   EntityTarget target, std::string_view unit,
                   Entity& output) const override;
  bool render_entity(const Entity& entity, NumericStyle style,
                     std::string& output) const override;
  bool extract_entities(std::string_view reading, std::size_t max_entities,
                        std::vector<EntitySpan>& output) const override;
  bool render_path(const ::kana_decoder::DecodedPath& path,
                   std::string_view reading,
                   const SurfaceRenderOptions& options,
                   std::string& output) const override;
};

struct ParsedNumber {
  std::int64_t mantissa = 0;
  std::uint8_t decimal_places = 0;
};
bool parse_japanese_number(std::string_view reading, ParsedNumber& result);
bool render_entity(const Entity& entity, NumericStyle style, std::string& output);
std::string terminal_punctuation(DiscourseAct act, PunctuationStyle style);

}  // namespace kana_decoder::surge
