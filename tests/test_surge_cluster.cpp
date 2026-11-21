#include "kana_decoder/engine.hpp"
#include "kana_decoder/japanese_decoder.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main() {
 const std::string path="surge-cluster-test.tsv";
 {
  std::ofstream out(path);
  out << "KANA_SURGE_CLUSTER\t1\n"
      << "META\ttrain_sequences\t10\n"
      << "META\tvocabulary_considered\t3\n"
      << "META\tcluster_count\t1\n"
      << "META\tclustered_tokens\t2\n"
      << "META\twindow\t2\n"
      << "META\tmax_history\t2\n"
      << "META\tgap_threshold\t0.01\n"
      << "META\tsmoothing_alpha\t0.5\n"
      << "TOKEN\t0\tprev-a\n"
      << "TOKEN\t0\tprev-b\n"
      << "EVIDENCE\t0\t1\tcandidate-good\t10\n"
      << "EVIDENCE\t0\t1\tcandidate-bad\t1\n"
      << "TOTAL\t0\t1\t11\n";
 }
 kana_decoder::SurgeClusterModel model;
 if(!model.load_tsv(path)) {
  std::remove(path.c_str());
  std::cerr << "valid sidecar rejected\n";
  return 1;
 }
 bool good_evidence=false,bad_evidence=false;
 const std::vector<std::string> history{"prev-a"};
 const double good=model.evidence_score("candidate-good",history,&good_evidence);
 const double bad=model.evidence_score("candidate-bad",history,&bad_evidence);
 if(!good_evidence||!bad_evidence||!(good>bad)||
    model.membership_count()!=2||model.evidence_count()!=2||
    model.gap_threshold()!=0.01) {
  std::remove(path.c_str());
  std::cerr << "cluster scoring mismatch\n";
  return 1;
 }
 {
  std::ofstream out(path);
  out << "KANA_SURGE_CLUSTER\t99\n";
 }
 kana_decoder::LatticeEdge first;
 first.start=0; first.end=1;
 first.candidates.push_back(kana_decoder::LatticeCandidate{"prev-a","前",0,0,0,0,"","",""});
 kana_decoder::LatticeEdge second;
 second.start=1; second.end=2;
 second.candidates.push_back(kana_decoder::LatticeCandidate{"candidate-bad","悪",0,0,0,0,"","",""});
 second.candidates.push_back(kana_decoder::LatticeCandidate{"candidate-good","良",10000,0,0,0,"","",""});
 for(int i=0;i<2048;++i) {
  second.candidates.push_back(kana_decoder::LatticeCandidate{
   "other-"+std::to_string(i),"他"+std::to_string(i),100+i,0,0,0,"","",""});
 }
 kana_decoder::DecoderOptions options;
 options.surge_cluster_model=&model;
 options.context_weight=0.0;
 kana_decoder::MorphologyDecoder decoder;
 bool limited=false;
 std::vector<kana_decoder::CandidateScoreTrace> trace;
 const auto paths=decoder.decode(2,{first,second},kana_decoder::MaskedNgramScorer{},
                                 {},options,nullptr,&trace,&limited);
 const auto rescued_trace=std::find_if(trace.begin(),trace.end(),[](const auto& item){
  return item.surface=="良";
 });
 if(limited||paths.empty()||paths.front().tokens.size()!=2||
    paths.front().tokens.back().candidate.surface!="良"||
    rescued_trace==trace.end()||!rescued_trace->has_cluster_evidence||
    !(rescued_trace->cluster_raw_evidence>0.0)) {
  std::remove(path.c_str());
  std::cerr << "cluster evidence failed to rescue candidate outside morphology top four\n";
  return 1;
 }
 if(model.load_tsv(path)) {
  std::remove(path.c_str());
  std::cerr << "unsupported schema accepted\n";
  return 1;
 }
 bool retained=false;
 const double after=model.evidence_score("candidate-good",history,&retained);
 std::remove(path.c_str());
 if(!retained||after!=good) {
  std::cerr << "failed load mutated active model\n";
  return 1;
 }
 std::cout << "SURGE cluster loader/scorer tests passed\n";
 return 0;
}
