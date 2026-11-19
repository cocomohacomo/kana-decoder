#include "kana_decoder/engine.hpp"
#include <cmath>
#include <iostream>
#include <vector>
int main() {
 kana_decoder::MaskedNgramScorer scorer;
 scorer.add_unigram("good",2); scorer.add_unigram("bad",100);
 scorer.add_left_bigram("p1","good",2); scorer.add_left_bigram("p1","bad",80);
 scorer.add_left_ngram({"p2","p1"},"good",40); scorer.add_left_ngram({"p2","p1"},"bad",1);
 scorer.add_left_ngram({"p0","p2","p1"},"good",50); scorer.add_left_ngram({"p0","p2","p1"},"bad",1);
 scorer.add_left_skipgram("p2","good",30); scorer.add_left_skipgram("p2","bad",1);
 const std::vector<std::string> history{"p0","p2","p1"};
 if(!(scorer.contextual_score("good",history,{})>scorer.contextual_score("bad",history,{})) || !scorer.has_left_context_evidence("good",history)) { std::cerr<<"multi-order evidence ranking failed\n"; return 1; }
 kana_decoder::MaskedNgramScorer skip_only; skip_only.add_unigram("good",1); skip_only.add_left_skipgram("p2","good",10);
 if(!skip_only.has_left_context_evidence("good",history)||!std::isfinite(skip_only.skip_contextual_score("good",history))||skip_only.has_left_context_evidence("bad",history)){std::cerr<<"skip evidence handling failed\n";return 1;}
 std::cout<<"SURGE multi-order n-gram tests passed\n"; return 0;
}
