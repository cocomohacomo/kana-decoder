#include "kana_decoder/engine.hpp"
#include "kana_decoder/c_api.h"
#include <cassert>
#include <iostream>
using namespace kana_decoder;
int main(){
 MaskedNgramScorer s;s.add_unigram("rain",10);s.add_unigram("candy",10);s.add_left_bigram("あした","rain",1);s.add_right_bigram("rain","がふる",50);s.add_left_bigram("あした","candy",1);s.add_right_bigram("candy","がふる",0);s.add_left_bigram("","rain",1);s.add_right_bigram("rain","をかう",1);s.add_left_bigram("","candy",1);s.add_right_bigram("candy","をかう",1);
 std::vector<Candidate>c{{"rain","雨"},{"candy","飴"}};auto resolved=s.resolve(c,{"あした"},{"がふる"},OutputMode::Ambiguous,-20,.5,true);assert(!resolved.ambiguous&&resolved.output=="雨");
 MaskedNgramScorer ngram;ngram.add_unigram("rain",10);ngram.add_unigram("candy",10);ngram.add_right_ngram("rain",{"明日","が","降る"},40);ngram.add_right_ngram("candy",{"明日","が","降る"},0);auto tri=ngram.resolve(c,{},{"明日","が","降る"},OutputMode::Normal,-20,.1,true);assert(tri.output=="雨");
 auto amb=s.resolve(c,{},{"をかう"},OutputMode::Ambiguous,10,1,true);assert(amb.ambiguous&&amb.output=="[雨/飴/other]");
 auto safe=s.resolve(c,{},{"をかう"},OutputMode::Conservative,10,1,true,"あめをかう");assert(safe.output=="あめをかう");
 CorrectionEngine e;CorrectionOptions o;o.minimum_improvement=1;o.minimum_score=1;auto dup=e.generate("きっって",{{"きっって",0},{"きって",3}},o);assert(dup.size()==1&&dup[0].proposed=="きって"&&dup[0].auto_accepted);
 o.rules.push_back({"し","じ","ja-voicing-01",CorrectionKind::Phonetic,1.5});o.rules.push_back({"っ","","ja-small-tsu-delete",CorrectionKind::Delete,1});o.rules.push_back({"","っ","ja-small-tsu-insert",CorrectionKind::Insert,1.5});
 auto ph=e.generate("かんしん",{{"かんしん",0},{"かんじん",3.5}},o);assert(!ph.empty()&&ph[0].proposed=="かんじん");auto de=e.generate("かっこう",{{"かっこう",0},{"かこう",3}},o);assert(!de.empty()&&de[0].proposed=="かこう");auto in=e.generate("かこう",{{"かこう",0},{"かっこう",3.5}},o);assert(!in.empty()&&in[0].proposed=="かっこう");
 auto weak=e.generate("かんしん",{{"かんしん",0},{"かんじん",.5}},o);assert(!weak.empty()&&!weak[0].auto_accepted&&weak[0].original=="かんしん");assert(e.generate("12333",{{"1233",9}},o).empty());o.enabled=false;assert(e.generate("きっって",{{"きって",3}},o).empty());
 DependencyGraph g;assert(g.add({"m1",Layer::Morphological,{},0,true}));assert(g.add({"s1",Layer::Semantic,{"m1"},0,true}));assert(g.add({"o1",Layer::Orthographic,{"s1"},0,true}));assert(g.add({"s2",Layer::Semantic,{},0,true}));auto invalid=g.invalidate_dependents("m1");assert(invalid.size()==2);assert(!g.find("s1")->valid&&!g.find("o1")->valid&&g.find("s2")->valid);assert(!g.add({"bad",Layer::Morphological,{"s2"},0,true}));
 kd_ngram_scorer*cs=kd_ngram_scorer_create();assert(cs);assert(kd_ngram_add_unigram(cs,"rain",10)==0);assert(kd_ngram_add_unigram(cs,"candy",10)==0);assert(kd_ngram_add_right_bigram(cs,"rain","がふる",50)==0);assert(kd_ngram_add_right_bigram(cs,"candy","がふる",0)==0);kd_candidate_view cv[]={{"rain","雨"},{"candy","飴"}};const char*rc[]={"がふる"};char out[128]={};assert(kd_ngram_resolve(cs,cv,2,nullptr,0,rc,1,KD_OUTPUT_NORMAL,-20,.5,1,"あめ",out,sizeof(out))==0);assert(std::string(out)=="雨");kd_ngram_scorer_destroy(cs);
 std::cout<<"all C++ tests passed\n";
}
