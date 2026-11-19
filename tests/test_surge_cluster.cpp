#include "kana_decoder/engine.hpp"
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
      << "TOTAL\t0\t1\t20\n";
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
