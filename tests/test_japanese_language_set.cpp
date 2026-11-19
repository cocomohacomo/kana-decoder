#include "kana_decoder/engine.hpp"

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
  return 0;
}
