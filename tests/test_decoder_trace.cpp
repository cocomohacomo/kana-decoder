#include "kana_decoder/engine.hpp"
#include "kana_decoder/japanese_decoder.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <fstream>
#include <limits>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <zlib.h>
int main() {
 using namespace kana_decoder;
 LexiconTrie lexicon;
 // UTF-8 trie traversal uses byte offsets without materializing one string per code point.
 LexiconTrie utf8_lexicon;
 assert(utf8_lexicon.add("😀",{ "emoji","😀",-1,0,0,0 }));
 auto emoji_lattice=utf8_lexicon.build_lattice("😀",16,false);
 assert(emoji_lattice.size()==1&&emoji_lattice.front().start==0&&emoji_lattice.front().end==1);
 // Mixed-width UTF-8 input preserves code-point spans without a byte-offset table.
 auto mixed_utf8_lattice=utf8_lexicon.build_lattice("😀あ",16,true);
 assert(mixed_utf8_lattice.size()==2);
 assert(mixed_utf8_lattice[0].start==0&&mixed_utf8_lattice[0].end==1);
 assert(mixed_utf8_lattice[1].start==1&&mixed_utf8_lattice[1].end==2);
 assert(mixed_utf8_lattice[1].candidates.front().surface=="あ");
 const std::string invalid_utf8("\xff",1);
 assert(!utf8_lexicon.add(invalid_utf8,{"invalid","x",0,0,0,0}));
 assert(utf8_lexicon.build_lattice(invalid_utf8,16,false).empty());
 // Gzip dictionary input is decompressed incrementally and preserves entries.
 const auto gzip_path=(std::filesystem::temp_directory_path()/"kana_decoder_trace_lexicon.tsv.gz").string();
 const std::string gzip_row="あめ\t雨\t雨\t名詞\t雨\t雨\t-10\t0\t0\t0\n";
 gzFile gzip_output=gzopen(gzip_path.c_str(),"wb");assert(gzip_output);
 assert(gzwrite(gzip_output,gzip_row.data(),static_cast<unsigned int>(gzip_row.size()))==static_cast<int>(gzip_row.size()));
 assert(gzclose(gzip_output)==Z_OK);
 LexiconTrie compressed_lexicon;std::size_t compressed_count=0;
 assert(compressed_lexicon.load_tsv(gzip_path,&compressed_count));
 assert(compressed_count==1);
 auto compressed_lattice=compressed_lexicon.build_lattice("あめ",16,false);
 assert(!compressed_lattice.empty()&&compressed_lattice.front().candidates.front().surface=="雨");
 // Mutating a loaded/compacted trie expands its edge arena without changing lookup semantics.
 assert(compressed_lexicon.add("あま",{"rain2","天",-20,0,0,0}));
 auto post_load_lattice=compressed_lexicon.build_lattice("あま",16,false);
 assert(!post_load_lattice.empty()&&post_load_lattice.front().candidates.front().surface=="天");
 auto preserved_lattice=compressed_lexicon.build_lattice("あめ",16,false);
 assert(!preserved_lattice.empty()&&preserved_lattice.front().candidates.front().surface=="雨");
 // Candidate arena is compacted after load, expanded on mutation, then compacted again.
 const auto arena_path=(std::filesystem::temp_directory_path()/"kana_decoder_trace_arena.tsv").string();
 { std::ofstream arena_file(arena_path);assert(arena_file);arena_file<<"あめ\t雨\t雨\t名詞\t雨\t雨\t-10\t0\t0\t0\n"; }
 LexiconTrie arena_lexicon;assert(arena_lexicon.load_tsv(arena_path));
 arena_lexicon.build_candidate_pos_index();
 assert(arena_lexicon.candidate_pos("雨")=="名詞");
 auto arena_before=arena_lexicon.build_lattice("あめ",16,false);
 assert(!arena_before.empty()&&arena_before.front().candidates.front().surface=="雨");
 assert(arena_lexicon.add("あま",{"arena-new","天",-20,0,0,0,"名詞"}));
 auto arena_after=arena_lexicon.build_lattice("あま",16,false);
 assert(!arena_after.empty()&&arena_after.front().candidates.front().surface=="天");
 auto arena_preserved=arena_lexicon.build_lattice("あめ",16,false);
 assert(!arena_preserved.empty()&&arena_preserved.front().candidates.front().surface=="雨");
 arena_lexicon.build_candidate_pos_index();
 assert(arena_lexicon.candidate_pos("arena-new")=="名詞");
 std::remove(arena_path.c_str());
 // Compact candidate string pool must materialize all public fields without loss.
 LatticeCandidate rich;rich.id="candidate-rich";rich.surface="天気";rich.word_cost=-123;rich.connection_cost=7;rich.left_id=11;rich.right_id=12;rich.pos="名詞,一般";rich.inflection_type="五段";rich.inflection_form="基本形";
 assert(compressed_lexicon.add("てんき",rich));
 auto rich_lattice=compressed_lexicon.build_lattice("てんき",16,false);
 assert(!rich_lattice.empty()&&!rich_lattice.front().candidates.empty());
 const auto& restored=rich_lattice.front().candidates.front();
 assert(restored.id==rich.id&&restored.surface==rich.surface&&restored.word_cost==rich.word_cost&&restored.connection_cost==rich.connection_cost);
 assert(restored.left_id==rich.left_id&&restored.right_id==rich.right_id&&restored.pos==rich.pos&&restored.inflection_type==rich.inflection_type&&restored.inflection_form==rich.inflection_form);
 compressed_lexicon.build_candidate_pos_index();
 assert(compressed_lexicon.candidate_pos("candidate-rich")==rich.pos);
 std::remove(gzip_path.c_str());
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
 // Diverse surfaces survive candidate caps even when duplicates are cheaper.
 LexiconTrie diverse_lexicon;
 assert(diverse_lexicon.add("あ",{"dup-cheap","同一",-1000,0,1,1}));
 assert(diverse_lexicon.add("あ",{"dup-next","同一",-900,0,1,1}));
 for(int i=0;i<8;++i)assert(diverse_lexicon.add("あ",{"surface-"+std::to_string(i),"別"+std::to_string(i),-800+i,0,1,1}));
 auto diverse_lattice=diverse_lexicon.build_lattice("あ",16,false);
 assert(diverse_lattice.size()==1&&diverse_lattice.front().candidates.size()==8);
 std::vector<std::string> retained_surfaces;
 for(const auto& c:diverse_lattice.front().candidates)if(std::find(retained_surfaces.begin(),retained_surfaces.end(),c.surface)==retained_surfaces.end())retained_surfaces.push_back(c.surface);
 assert(retained_surfaces.size()==8);
 // Transition vectors remain searchable after out-of-order insertions.
 LexiconTrie branching_lexicon;
 assert(branching_lexicon.add("あん",{"branch-n","案",-10,0,1,1}));
 assert(branching_lexicon.add("あか",{"branch-k","赤",-10,0,1,1}));
 assert(branching_lexicon.add("あさ",{"branch-s","朝",-10,0,1,1}));
 assert(branching_lexicon.add("あき",{"branch-i","秋",-10,0,1,1}));
 for(const auto& reading:std::vector<std::string>{"あか","あき","あさ","あん"}){
  auto matches=branching_lexicon.build_lattice(reading,16,false);
  assert(!matches.empty());
  assert(matches.front().start==0&&matches.front().end==2);
  assert(!matches.front().candidates.empty());
 }
 // Prefix-edge pruning samples the full matched span range.
 LexiconTrie span_lexicon;std::string nested_reading;
 for(int i=1;i<=20;++i){nested_reading+="あ";assert(span_lexicon.add(nested_reading,{"prefix-"+std::to_string(i),"候補"+std::to_string(i),-100*i,0,1,1}));}
 std::vector<LatticePositionTrace> span_trace;
 auto span_lattice=span_lexicon.build_lattice(nested_reading,16,false,&span_trace);
 assert(!span_lattice.empty()&&!span_trace.empty());
 assert(span_trace.front().available_dictionary_match_count==20);
 assert(span_trace.front().dictionary_match_count==16&&span_trace.front().pruned_dictionary_match_count==4);
 assert(span_trace.front().pruned_candidate_count==4);
 bool longest=false;for(const auto& m:span_trace.front().dictionary_matches)longest=longest||m.end==20;assert(longest);
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
   observed_gated_context = observed_gated_context ||
       (std::abs(item.context_evidence_score)>1e-12 &&
        std::abs(item.weighted_context_score)>1e-12);
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
 // Direct caller-supplied lattices receive the same span diversity policy.
 std::vector<LatticeEdge> direct_span_lattice;
 for(std::size_t end=1;end<=20;++end){
  LatticeEdge edge;edge.start=0;edge.end=end;
  edge.candidates.push_back({"direct-span-"+std::to_string(end),"候補"+std::to_string(end),-100,0,1,1});
  direct_span_lattice.push_back(std::move(edge));
 }
 std::vector<CandidateScoreTrace> direct_span_trace;
 auto direct_span_paths=direct_decoder.decode(20,direct_span_lattice,scorer,{},DecoderOptions{},nullptr,&direct_span_trace,&exceeded);
 assert(!exceeded&&!direct_span_paths.empty());
 bool direct_longest_retained=false;
 for(const auto& item:direct_span_trace)if(item.start==0&&item.end==20&&item.candidate_id=="direct-span-20")direct_longest_retained=true;
 assert(direct_longest_retained);

 // Caller-supplied lattices also preserve distinct surfaces under the
 // per-edge candidate cap, rather than letting duplicate surfaces crowd them out.
 LatticeEdge direct_diverse_edge;direct_diverse_edge.start=0;direct_diverse_edge.end=1;
 direct_diverse_edge.candidates.push_back({"direct-dup-0","重複",-1000,0,1,1});
 direct_diverse_edge.candidates.push_back({"direct-dup-1","重複",-900,0,1,1});
 for(int i=0;i<8;++i)direct_diverse_edge.candidates.push_back(
  {"direct-surface-"+std::to_string(i),"異表層"+std::to_string(i),-800+i,0,1,1});
 std::vector<LatticeEdge> direct_diverse_lattice{direct_diverse_edge};
 auto direct_diverse_paths=direct_decoder.decode(1,direct_diverse_lattice,scorer);
 assert(direct_diverse_paths.size()==8);
 std::vector<std::string> direct_surfaces;
 for(const auto& path:direct_diverse_paths)for(const auto& token:path.tokens)
  if(std::find(direct_surfaces.begin(),direct_surfaces.end(),token.candidate.surface)==direct_surfaces.end())
   direct_surfaces.push_back(token.candidate.surface);
 assert(direct_surfaces.size()==8);
 // Deterministic adversarial cap stress: repeated surface duplicates must not
 // crowd out unique alternatives across varied candidate costs and insertion order.
 for(int trial=0;trial<100;++trial){
  LexiconTrie stress;
  const std::string reading="あ";
  for(int i=0;i<24;++i){
   const int surface_group=(i*7+trial*3)%12;
   const int cost=-1200+((i*31+trial*17)%400);
   assert(stress.add(reading,{"stress-"+std::to_string(trial)+"-"+std::to_string(i),
    "候補群"+std::to_string(surface_group),cost,0,1,1}));
  }
  auto stress_lattice=stress.build_lattice(reading,16,false);
  assert(stress_lattice.size()==1);
  assert(stress_lattice.front().candidates.size()==8);
  std::vector<std::string> stress_surfaces;
  for(const auto& candidate:stress_lattice.front().candidates){
   if(std::find(stress_surfaces.begin(),stress_surfaces.end(),candidate.surface)==stress_surfaces.end())
    stress_surfaces.push_back(candidate.surface);
  }
  assert(stress_surfaces.size()==8);
 }
 std::cout << "decoder trace and resource budget tests passed\n";
}
