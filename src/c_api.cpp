#include "kana_decoder/c_api.h"
#include "kana_decoder/engine.hpp"
#include <cstring>
#include <new>
#include <string>
#include <vector>
struct kd_ngram_scorer{kana_decoder::MaskedNgramScorer impl;};
extern "C" {
kd_ngram_scorer*kd_ngram_scorer_create(void){return new(std::nothrow)kd_ngram_scorer();}
void kd_ngram_scorer_destroy(kd_ngram_scorer*p){delete p;}
int kd_ngram_add_unigram(kd_ngram_scorer*p,const char*i,uint32_t n){if(!p||!i)return-1;p->impl.add_unigram(i,n);return 0;}
int kd_ngram_add_left_bigram(kd_ngram_scorer*p,const char*l,const char*i,uint32_t n){if(!p||!l||!i)return-1;p->impl.add_left_bigram(l,i,n);return 0;}
int kd_ngram_add_right_bigram(kd_ngram_scorer*p,const char*i,const char*r,uint32_t n){if(!p||!r||!i)return-1;p->impl.add_right_bigram(i,r,n);return 0;}
int kd_ngram_resolve(kd_ngram_scorer*p,const kd_candidate_view*c,size_t count,const char*const*l,size_t nl,const char*const*r,size_t nr,int mode,double ms,double mm,int other,const char*original,char*out,size_t cap){
 if(!p||!c||!count||count>8||!out||!cap||mode<0||mode>3)return-1;std::vector<kana_decoder::Candidate>cs;std::vector<std::string>ls,rs;
 for(size_t i=0;i<count;++i){if(!c[i].id||!c[i].surface)return-1;cs.push_back({c[i].id,c[i].surface});}
 for(size_t i=0;i<nl;++i){if(!l||!l[i])return-1;ls.emplace_back(l[i]);}for(size_t i=0;i<nr;++i){if(!r||!r[i])return-1;rs.emplace_back(r[i]);}
 auto v=p->impl.resolve(cs,ls,rs,static_cast<kana_decoder::OutputMode>(mode),ms,mm,other!=0,original?original:"");if(v.output.size()+1>cap)return-2;std::memcpy(out,v.output.c_str(),v.output.size()+1);return 0;
}}
