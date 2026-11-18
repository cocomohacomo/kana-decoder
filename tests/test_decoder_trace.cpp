#include "kana_decoder/engine.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
 using namespace kana_decoder;
 LexiconTrie lexicon;
 assert(lexicon.add("あめ", {"rain", "雨", -1200, 25, 1, 2}));
 assert(lexicon.add("あめ", {"candy", "飴", -900, 15, 3, 4}));
 MaskedNgramScorer scorer;
 scorer.add_unigram("rain", 10);
 scorer.add_unigram("candy", 1);
 ConnectionModel connections;
 connections.set_cost(2, 1, 7);
 std::vector<CandidateScoreTrace> trace;
 DecoderOptions options;
 auto paths=decode_reading("あめ",lexicon,scorer,options,&connections,&trace);
 assert(!paths.empty());
 assert(trace.size()>=2);
 bool saw_rain=false,saw_candy=false;
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
 }
 assert(saw_rain&&saw_candy);
 auto fallback=decode_reading("ぴ",lexicon,scorer,options,nullptr,&trace);
 assert(!fallback.empty());
 bool saw_fallback=false;
 for(const auto& item:trace) if(item.unknown_fallback) saw_fallback=true;
 assert(saw_fallback);
 std::cout << "decoder trace tests passed\n";
}
