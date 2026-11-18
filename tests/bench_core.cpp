#include "kana_decoder/engine.hpp"
#include <chrono>
#include <iostream>
using namespace kana_decoder;
int main(){
 MaskedNgramScorer s;s.add_unigram("rain",100);s.add_unigram("candy",100);s.add_unigram("bridge",100);s.add_unigram("edge",100);s.add_right_ngram("rain",{"が","ふる"},80);s.add_right_ngram("candy",{"が","ふる"},1);
 std::vector<Candidate> candidates{{"rain","雨"},{"candy","飴"},{"bridge","橋"},{"edge","端"}};
 auto a=std::chrono::steady_clock::now();constexpr int n=200000;
 for(int i=0;i<n;++i)(void)s.resolve(candidates,{"きょう","あめ"},{"が","ふる"},OutputMode::Ambiguous,-20,.5,true,"あめ");
 auto b=std::chrono::steady_clock::now();auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count();
 std::cout<<"ngram_iterations="<<n<<" total_ns="<<ns<<" ns_per_resolve="<<(double)ns/n<<"\n";
 std::vector<LatticeEdge> lattice{{0,1,{{"rain","雨",100,0},{"candy","飴",100,0},{"bridge","橋",110,0},{"edge","端",120,0}}},{1,2,{{"verb","降る",80,0}}}};
 MaskedNgramScorer decoder_scorer;decoder_scorer.add_unigram("rain",100);decoder_scorer.add_unigram("candy",100);decoder_scorer.add_unigram("bridge",100);decoder_scorer.add_unigram("edge",100);decoder_scorer.add_unigram("verb",100);decoder_scorer.add_left_bigram("rain","verb",80);
 MorphologyDecoder decoder;DecoderOptions options;options.beam_width=8;options.max_paths=8;
 auto da=std::chrono::steady_clock::now();constexpr int dn=20000;for(int i=0;i<dn;++i)(void)decoder.decode(2,lattice,decoder_scorer,{},options);auto db=std::chrono::steady_clock::now();
 auto dns=std::chrono::duration_cast<std::chrono::nanoseconds>(db-da).count();std::cout<<"decoder_iterations="<<dn<<" total_ns="<<dns<<" ns_per_decode="<<(double)dns/dn<<"\n";
}
