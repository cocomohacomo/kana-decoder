#include "kana_decoder/engine.hpp"
#include <chrono>
#include <iostream>
using namespace kana_decoder;
int main(){MaskedNgramScorer s;s.add_unigram("rain",100);s.add_unigram("candy",100);s.add_unigram("bridge",100);s.add_unigram("edge",100);s.add_right_ngram("rain",{"が","ふる"},80);s.add_right_ngram("candy",{"が","ふる"},1);
 std::vector<Candidate>c{{"rain","雨"},{"candy","飴"},{"bridge","橋"},{"edge","端"}};auto a=std::chrono::steady_clock::now();constexpr int n=200000;
 for(int i=0;i<n;++i)(void)s.resolve(c,{"きょう","あめ"},{"が","ふる"},OutputMode::Ambiguous,-20,.5,true,"あめ");
 auto b=std::chrono::steady_clock::now();auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count();std::cout<<"iterations="<<n<<" total_ns="<<ns<<" ns_per_resolve="<<(double)ns/n<<"\n";}
