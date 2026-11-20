#include "kana_decoder/engine.hpp"
#include "kana_decoder/japanese_decoder.hpp"
#include "kana_decoder/japanese_language.hpp"

#include <cassert>
#include <string>
#include <vector>

using namespace kana_decoder;

namespace {

LatticeCandidate candidate(const std::string& id, const std::string& surface,
                           std::int32_t cost) {
  LatticeCandidate value;
  value.id = id;
  value.surface = surface;
  value.word_cost = cost;
  return value;
}

}  // namespace

int main() {
  LexiconTrie lexicon;
  assert(lexicon.add("あ", candidate("c-high", "高", 30)));
  assert(lexicon.add("あ", candidate("c-low", "低", 1)));
  assert(lexicon.add("あ", candidate("c-mid", "中", 10)));

  std::vector<LatticePositionTrace> trace;
  bool exceeded = false;
  const auto lattice = lexicon.build_lattice(
      "あい", 16, true, &trace, 16, 16, &exceeded);
  assert(!exceeded);
  assert(lattice.size() == 2);
  assert(lattice[0].start == 0 && lattice[0].end == 1);
  assert(lattice[0].candidates.size() == 3);
  assert(lattice[0].candidates[0].id == "c-low");
  assert(lattice[0].candidates[1].id == "c-mid");
  assert(lattice[0].candidates[2].id == "c-high");
  assert(lattice[1].start == 1 && lattice[1].end == 2);
  assert(lattice[1].candidates.size() == 1);
  assert(lattice[1].candidates[0].id == "kana:い");
  assert(lattice[1].candidates[0].surface == "い");
  assert(trace.size() == 2);
  assert(trace[0].dictionary_match_count == 1);
  assert(trace[0].fallback_suppressed_by_dictionary);
  assert(trace[1].fallback_generated);

  // Dictionary matches suppress per-character fallback at the same start.
  const auto no_fallback = lexicon.build_lattice("あ", 16, true);
  assert(no_fallback.size() == 1);
  assert(no_fallback.front().candidates.front().id != "kana:あ");

  // An entirely unknown reading receives a deterministic kana fallback.
  LexiconTrie empty_lexicon;
  const auto unknown = empty_lexicon.build_lattice("ぬ", 16, true);
  assert(unknown.size() == 1);
  assert(unknown.front().candidates.front().id == "kana:ぬ");
  assert(unknown.front().candidates.front().surface == "ぬ");

  // Repeated POS and inflection labels are interned once in the compact
  // string pool, while candidate IDs and surfaces remain independently owned.
  LexiconTrie interned;
  auto metadata_a = candidate("intern-a", "甲", 0);
  metadata_a.pos = "名詞/普通名詞";
  metadata_a.inflection_type = "五段-ラ行";
  metadata_a.inflection_form = "連用形-一般";
  auto metadata_b = candidate("intern-b", "乙", 1);
  metadata_b.pos = "名詞/普通名詞";
  metadata_b.inflection_type = "五段-ラ行";
  metadata_b.inflection_form = "連用形-一般";
  assert(interned.add("こう", metadata_a));
  assert(interned.add("おつ", metadata_b));
  assert(interned.interned_metadata_string_count() == 3);
  const std::size_t expected_pool_bytes =
      metadata_a.id.size() + metadata_a.surface.size() +
      metadata_b.id.size() + metadata_b.surface.size() +
      metadata_a.pos.size() + metadata_a.inflection_type.size() +
      metadata_a.inflection_form.size();
  assert(interned.candidate_string_pool_bytes() == expected_pool_bytes);
  const auto interned_lattice = interned.build_lattice("こうおつ", 16, false);
  assert(interned_lattice.size() == 2);
  assert(interned_lattice[0].candidates.front().pos == "名詞/普通名詞");
  assert(interned_lattice[1].candidates.front().inflection_form == "連用形-一般");

  // Candidate and edge ceilings are signalled, never silently treated as
  // complete lattice results.
  exceeded = false;
  const auto candidate_bounded = lexicon.build_lattice(
      "あ", 16, false, nullptr, 16, 2, &exceeded);
  assert(exceeded);
  assert(candidate_bounded.size() == 1);
  assert(candidate_bounded.front().candidates.size() == 2);

  exceeded = false;
  const auto edge_bounded = lexicon.build_lattice(
      "あい", 16, true, nullptr, 1, 16, &exceeded);
  assert(exceeded);
  assert(edge_bounded.size() == 1);

  // Expansion exhaustion rejects the decode instead of returning a
  // misleading partial best path.
  MaskedNgramScorer scorer;
  DecoderOptions options;
  options.max_expanded_states = 1;
  exceeded = false;
  const auto paths = decode_reading(
      "あ", lexicon, scorer, options, nullptr, nullptr, nullptr, &exceeded);
  assert(exceeded);
  assert(paths.empty());
  // Dictionary-supplied inflected surfaces carry their own okurigana.
  // The renderer must preserve the selected surface rather than attempting
  // to regenerate a lemma form or guessing an inflection.
  LatticeCandidate inflected = candidate("ja-maw-連用形促音便", "回っ", -100);
  inflected.pos = "動詞/一般";
  inflected.inflection_type = "五段-ラ行";
  inflected.inflection_form = "連用形-促音便";
  assert(lexicon.add("まわっ", inflected));
  const auto inflected_paths = decode_reading("まわっ", lexicon, scorer);
  assert(!inflected_paths.empty());
  assert(inflected_paths.front().tokens.size() == 1);
  const auto& inflected_token = inflected_paths.front().tokens.front();
  assert(inflected_token.candidate.surface == "回っ");
  assert(inflected_token.candidate.morphology.language_tag == "ja-JP");
  assert(inflected_token.candidate.morphology.inflection_class == "五段-ラ行");
  assert(inflected_token.candidate.morphology.inflection_form == "連用形-促音便");
  surge::JapaneseLanguageAdapter adapter;
  surge::SurfaceRenderOptions render_options;
  std::string rendered;
  assert(adapter.render_path(inflected_paths.front(), "まわっ", render_options, rendered));
  assert(rendered == "回っ");

  LatticeCandidate te_form = candidate("ja-tab-連用形", "食べて", -90);
  te_form.pos = "動詞/一般";
  te_form.inflection_type = "下一段-バ行";
  te_form.inflection_form = "連用形-一般";
  assert(lexicon.add("たべて", te_form));
  const auto te_paths = decode_reading("たべて", lexicon, scorer);
  assert(!te_paths.empty());
  assert(te_paths.front().tokens.front().candidate.surface == "食べて");
  assert(adapter.render_path(te_paths.front(), "たべて", render_options, rendered));
  assert(rendered == "食べて");

  LatticeCandidate te_particle = candidate("ja-particle-te", "て", -20);
  te_particle.pos = "助詞/接続助詞";
  assert(lexicon.add("て", te_particle));
  const auto te_chain = decode_reading("まわって", lexicon, scorer);
  assert(!te_chain.empty());
  assert(te_chain.front().tokens.size() == 2);
  assert(te_chain.front().tokens[0].candidate.surface == "回っ");
  assert(te_chain.front().tokens[1].candidate.surface == "て");
  assert(adapter.render_path(te_chain.front(), "まわって", render_options, rendered));
  assert(rendered == "回って");

  LatticeCandidate auxiliary = candidate("ja-aux-iru", "いる", -30);
  auxiliary.pos = "助動詞";
  assert(lexicon.add("いる", auxiliary));
  const auto auxiliary_chain = decode_reading("たべている", lexicon, scorer);
  assert(!auxiliary_chain.empty());
  assert(auxiliary_chain.front().tokens.size() == 2);
  assert(auxiliary_chain.front().tokens[0].candidate.surface == "食べて");
  assert(auxiliary_chain.front().tokens[1].candidate.surface == "いる");
  assert(adapter.render_path(auxiliary_chain.front(), "たべている",
                             render_options, rendered));
  assert(rendered == "食べている");

  return 0;
}
