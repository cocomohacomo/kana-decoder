#include "kana_decoder/engine.hpp"
#include "kana_decoder/japanese_decoder.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
int main() {
 using namespace kana_decoder;
 LexiconTrie lexicon;
 assert(lexicon.add("あめ", {"rain", "雨", -1200, 25, 1, 2}));
 assert(lexicon.add("あめ", {"candy", "飴", -900, 15, 3, 4}));
 assert(lexicon.add("あ", {"first", "あ", -100, 0, 0, 2}));
 assert(lexicon.add("め", {"second", "め", -100, 0, 1, 0}));
 std::vector<LatticePositionTrace> generation;
 auto generated_lattice = lexicon.build_lattice("あめ", 16, true, &generation);
 assert(generated_lattice.size() >= 2);
 assert(generation.size() == 2);
 assert(generation[0].dictionary_match_count == 2);
 assert(generation[0].fallback_suppressed_by_dictionary);
 assert(!generation[0].fallback_generated);
 assert(generation[0].dictionary_matches.size() == 2);
 assert(generation[1].dictionary_match_count == 1);
 assert(generation[1].fallback_suppressed_by_dictionary);
 std::vector<LatticePositionTrace> unknown_generation;
 auto unknown_lattice = lexicon.build_lattice("ぴ", 16, true, &unknown_generation);
 assert(unknown_lattice.size() == 1);
 assert(unknown_generation.size() == 1);
 assert(unknown_generation[0].dictionary_match_count == 0);
 assert(unknown_generation[0].fallback_generated);
 assert(!unknown_generation[0].fallback_suppressed_by_dictionary);
 MaskedNgramScorer scorer;
 scorer.add_unigram("rain", 10);
 scorer.add_unigram("candy", 1);
 MaskedNgramScorer context_model;
 context_model.add_left_bigram("previous", "rain", 10);
 context_model.add_left_bigram("previous", "candy", 1);
 const double rain_context=context_model.contextual_score("rain", {"previous"}, {});
 const double candy_context=context_model.contextual_score("candy", {"previous"}, {});
 assert(rain_context>candy_context);
 assert(context_model.has_left_context_evidence("rain", {"previous"}));
 assert(context_model.has_left_context_evidence("candy", {"previous"}));
 assert(!context_model.has_left_context_evidence("rain", {"unseen"}));
 MaskedNgramScorer empty_context;
 assert(empty_context.contextual_score("rain", {"previous"}, {})==0.0);
 assert(!empty_context.has_left_context_evidence("rain", {"previous"}));

 // SURGE evidence gating requires two distinct supported surfaces and a
 // sufficient context-only margin. Without evidence it falls back to morphology.
 LexiconTrie gated_lexicon;
 assert(gated_lexicon.add("あ", {"rain", "雨", -2000, 0, 1, 1}));
 assert(gated_lexicon.add("あ", {"candy", "飴", -1000, 0, 1, 1}));
 assert(gated_lexicon.add("め", {"bridge", "橋", -100, 0, 1, 1}));
 assert(gated_lexicon.add("め", {"chopsticks", "箸", -100, 0, 1, 1}));
 MaskedNgramScorer gated_scorer;
 gated_scorer.add_left_bigram("rain", "bridge", 10);
 gated_scorer.add_left_bigram("rain", "chopsticks", 1);
 DecoderOptions gated_options;
 gated_options.surge_left_context_gate = true;
 gated_options.surge_context_margin = 0.5;
 std::vector<CandidateScoreTrace> gated_trace;
 auto gated_paths=decode_reading("あめ",gated_lexicon,gated_scorer,gated_options,
                                  nullptr,&gated_trace);
 assert(!gated_paths.empty());
 bool observed_gated_context=false;
 for(const auto& item:gated_trace){
  if(item.start==1&&item.candidate_id=="bridge"){
   observed_gated_context=true;
   assert(std::abs(item.context_evidence_score)>1e-12);
   assert(item.weighted_context_score!=0.0);
  }
 }
 assert(observed_gated_context);
 MaskedNgramScorer unsupported_scorer;
 std::vector<CandidateScoreTrace> unsupported_trace;
 auto unsupported_paths=decode_reading("あめ",gated_lexicon,unsupported_scorer,
  gated_options,nullptr,&unsupported_trace);
 assert(!unsupported_paths.empty());
 for(const auto& item:unsupported_trace)if(item.start==1)
  assert(item.weighted_context_score==0.0);
 ConnectionModel connections;
 connections.set_cost(2, 1, 7);
 std::vector<CandidateScoreTrace> trace;
 DecoderOptions options;
 auto paths=decode_reading("あめ",lexicon,scorer,options,&connections,&trace);
 assert(!paths.empty());
 assert(trace.size()>=2);
 bool saw_rain=false,saw_candy=false,saw_transition=false;
 for(const auto& item:trace) {
   assert(item.end>item.start);
   assert(item.local_score==item.morphology_score+item.weighted_context_score);
   assert(item.weighted_context_score==item.raw_context_score*item.context_weight);
   assert(item.candidate_id.rfind("kana:",0)!=0);
   if(item.candidate_id=="rain") {
     saw_rain=true;
     assert(item.word_cost==-1200);
     assert(item.candidate_connection_cost==25);
     assert(item.transition_cost==0);
   }
   if(item.candidate_id=="candy") saw_candy=true;
   if(item.candidate_id=="second" && item.transition_cost==7) saw_transition=true;
 }
 assert(saw_rain&&saw_candy&&saw_transition);
 auto fallback=decode_reading("ぴ",lexicon,scorer,options,nullptr,&trace);
 assert(!fallback.empty());
 bool saw_fallback=false;
 for(const auto& item:trace) if(item.unknown_fallback) saw_fallback=true;
 assert(saw_fallback);

 // Invalid floating-point scoring controls fail closed instead of poisoning beam order.
 bool exceeded=false;
 DecoderOptions invalid_scoring;
 invalid_scoring.context_weight=std::numeric_limits<double>::quiet_NaN();
 exceeded=false;
 auto nan_weight=decode_reading("あ",lexicon,scorer,invalid_scoring,nullptr,nullptr,nullptr,&exceeded);
 assert(nan_weight.empty()&&exceeded);
 invalid_scoring=DecoderOptions{};
 invalid_scoring.surge_context_margin=-0.1;
 exceeded=false;
 auto negative_margin=decode_reading("あ",lexicon,scorer,invalid_scoring,nullptr,nullptr,nullptr,&exceeded);
 assert(negative_margin.empty()&&exceeded);

 // Input, lattice, and search expansion limits fail closed instead of
 // returning a silently truncated hypothesis.
 DecoderOptions limited;
 exceeded=false;
 limited.max_input_codepoints=1;
 auto too_long=decode_reading("あめ",lexicon,scorer,limited,nullptr,nullptr,nullptr,&exceeded);
 assert(too_long.empty()&&exceeded);
 limited=DecoderOptions{};
 limited.max_input_codepoints=100000;
 exceeded=false;
 std::string oversized;
 for(std::size_t i=0;i<257;++i)oversized+="あ";
 auto hard_cap=decode_reading(oversized,lexicon,scorer,limited,nullptr,nullptr,nullptr,&exceeded);
 assert(hard_cap.empty()&&exceeded);
 limited=DecoderOptions{};
 limited.max_lattice_edges=1;
 exceeded=false;
 auto too_many_edges=decode_reading("あめ",lexicon,scorer,limited,nullptr,nullptr,nullptr,&exceeded);
 assert(too_many_edges.empty()&&exceeded);
 limited=DecoderOptions{};
 limited.max_expanded_states=1;
 exceeded=false;
 auto too_many_states=decode_reading("あめ",lexicon,scorer,limited,nullptr,nullptr,nullptr,&exceeded);
 assert(too_many_states.empty()&&exceeded);
 MorphologyDecoder direct_decoder;
 exceeded=false;
 limited.max_expanded_states=0;
 auto direct_zero_budget=direct_decoder.decode(2,generated_lattice,scorer,{},limited,nullptr,nullptr,&exceeded);
 assert(direct_zero_budget.empty()&&exceeded);
 limited=DecoderOptions{};
 exceeded=false;
 auto direct_oversized=direct_decoder.decode(257,generated_lattice,scorer,{},limited,nullptr,nullptr,&exceeded);
 assert(direct_oversized.empty()&&exceeded);
 exceeded=false;
 std::vector<LatticeEdge> oversized_edges(4097);
 auto direct_edge_overflow=direct_decoder.decode(2,oversized_edges,scorer,{},limited,nullptr,nullptr,&exceeded);
 assert(direct_edge_overflow.empty()&&exceeded);
 exceeded=false;
 LatticeEdge oversized_candidate_edge;
 oversized_candidate_edge.start=0;oversized_candidate_edge.end=1;
 oversized_candidate_edge.candidates.resize(32769);
 std::vector<LatticeEdge> oversized_candidate_lattice{oversized_candidate_edge};
 auto direct_candidate_overflow=direct_decoder.decode(2,oversized_candidate_lattice,scorer,{},limited,nullptr,nullptr,&exceeded);
 assert(direct_candidate_overflow.empty()&&exceeded);
 limited=DecoderOptions{};
 limited.max_trace_records=1;
 std::vector<CandidateScoreTrace> bounded_trace;
 auto bounded=decode_reading("あめ",lexicon,scorer,limited,nullptr,&bounded_trace);
 assert(!bounded.empty()&&bounded_trace.size()<=1);
 std::cout << "decoder trace and resource budget tests passed\n";
}
