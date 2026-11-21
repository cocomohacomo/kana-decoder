#include "kana_decoder/engine.hpp"
#include "kana_decoder/japanese_decoder.hpp"
#include "kana_decoder/c_api.h"
#include "kana_decoder/pack_manifest.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <utility>
using namespace kana_decoder;
int main(){
 MaskedNgramScorer smoothing_test;
 assert(smoothing_test.smoothing_alpha()==0.5);
 smoothing_test.add_unigram("a",10);smoothing_test.add_unigram("b",10);
 smoothing_test.add_left_bigram("p","a",9);smoothing_test.add_left_bigram("p","b",1);
 const double default_smoothing_score=smoothing_test.contextual_score("a",{"p"},{});
 assert(smoothing_test.set_smoothing_alpha(0.1));
 assert(smoothing_test.contextual_score("a",{"p"},{})!=default_smoothing_score);
 assert(!smoothing_test.set_smoothing_alpha(0.0));
 assert(!smoothing_test.set_smoothing_alpha(std::numeric_limits<double>::infinity()));
 assert(smoothing_test.set_smoothing_alpha(0.5));
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

 // Integrated N-best lattice: a later token's context resolves an earlier homonym.
 MaskedNgramScorer lattice_scorer;
 lattice_scorer.add_unigram("A",10); lattice_scorer.add_unigram("B",10);
 lattice_scorer.add_unigram("C",10);
 lattice_scorer.add_left_bigram("A","C",80);
 lattice_scorer.add_left_bigram("B","C",0);
 std::vector<LatticeEdge> lattice{
  {0,1,{{"A","雨",100,0},{"B","飴",100,0}}},
  {1,2,{{"C","が降る",100,0}}}
 };
 DecoderOptions dop; dop.beam_width=4; dop.max_paths=4;
 MorphologyDecoder decoder;
 auto paths=decoder.decode(2,lattice,lattice_scorer,{},dop);
 assert(paths.size()==2);
 assert(paths.front().tokens.size()==2);
 assert(paths.front().tokens[0].candidate.id=="A");
 assert(paths.front().score>paths.back().score);
 // Invalid spans and unreachable input must not fabricate a complete path.
 auto invalid_paths=decoder.decode(3,lattice,lattice_scorer,{},dop);
 assert(invalid_paths.empty());
 DecoderOptions bounded; bounded.beam_width=1; bounded.max_paths=1;
 auto one_path=decoder.decode(2,lattice,lattice_scorer,{},bounded);
 assert(one_path.size()==1&&one_path[0].tokens[0].candidate.id=="A");
 // Pack numeric fields must be fully parsed; trailing junk is rejected.
 const auto temp_root=std::filesystem::temp_directory_path();
 const auto bad_lex_path=temp_root/"kana_decoder_bad_lexicon.tsv";
 {std::ofstream f(bad_lex_path);f<<"あめ\t雨\t雨\t名詞\tweather\train\t100junk\t0\t1\t1\n";}
 LexiconTrie bad_lexicon;assert(!bad_lexicon.load_tsv(bad_lex_path.string()));std::filesystem::remove(bad_lex_path);
 const auto bad_ngram_path=temp_root/"kana_decoder_bad_ngram.tsv";
 {std::ofstream f(bad_ngram_path);f<<"U\t\train\t10x\n";}
 MaskedNgramScorer bad_ngram;assert(!bad_ngram.load_tsv(bad_ngram_path.string()));std::filesystem::remove(bad_ngram_path);
 const auto bad_conn_path=temp_root/"kana_decoder_bad_connections.tsv";
 {std::ofstream f(bad_conn_path);f<<"1x\t1\t0\n";}
 ConnectionModel bad_connections;assert(!bad_connections.load_tsv(bad_conn_path.string()));std::filesystem::remove(bad_conn_path);

 const auto matrix_path=temp_root/"kana_decoder_matrix_dimensions.tsv";
 {std::ofstream f(matrix_path);f<<"# matrix 3 4\n# right_id\tleft_id\tcost\n2\t1\t-20\n3\t2\t15\n";}
 ConnectionModel dimensioned;assert(dimensioned.load_tsv(matrix_path.string()));
 assert(dimensioned.left_size()==3&&dimensioned.right_size()==4&&dimensioned.nonzero_cells()==2);
 assert(dimensioned.cost(2,1)==-20&&dimensioned.cost(3,2)==15&&dimensioned.cost(1,1)==0);
 {std::ofstream f(matrix_path);f<<"# matrix 3 4\n2\t3\t1\n";}
 assert(!dimensioned.load_tsv(matrix_path.string()));
 {std::ofstream f(matrix_path);f<<"# matrix 3 4\n2\t1\t-20\n2\t1\t-10\n";}
 assert(!dimensioned.load_tsv(matrix_path.string()));
 {std::ofstream f(matrix_path);f<<"# matrix 3 4\n2\t1\t0\n2\t1\t-10\n";}
 assert(!dimensioned.load_tsv(matrix_path.string()));
 dimensioned.set_cost(2,1,0);assert(dimensioned.cost(2,1)==0&&dimensioned.nonzero_cells()==1);
 std::filesystem::remove(matrix_path);

 ConnectionModel cm;cm.set_cost(5,1,50);cm.set_cost(5,2,0);
 std::vector<LatticeEdge> pos_lattice{
  {0,1,{{"X","共通",10,0,0,5}}},
  {1,2,{{"Y","候補甲",10,0,1,6},{"Z","候補乙",10,0,2,7}}}
 };
 auto pos_paths=decoder.decode(2,pos_lattice,lattice_scorer,{},dop,&cm);
 assert(!pos_paths.empty()&&pos_paths.front().tokens[1].candidate.id=="Z");

 // POS evidence must be contextual, not a second unigram prior. The global
 // unigram strongly favors PA, while the observed preceding POS favors PB.
 MaskedNgramScorer pos_evidence;
 pos_evidence.add_unigram("P0",100);
 pos_evidence.add_unigram("PA",10000);
 pos_evidence.add_unigram("PB",1);
 pos_evidence.add_left_bigram("P0","PA",1);
 pos_evidence.add_left_bigram("P0","PB",100);
 DecoderOptions pos_options=dop;
 pos_options.pos_ngram_scorer=&pos_evidence;
 pos_options.pos_context_weight=10.0;
 std::vector<LatticeEdge> pos_evidence_lattice{
  {0,1,{{"P0_TOKEN","先行",10,0,0,1,"P0"}}},
  {1,2,{{"PA_TOKEN","候補甲",10,0,0,2,"PA"},
        {"PB_TOKEN","候補乙",10,0,0,3,"PB"}}}
 };
 auto pos_evidence_paths=decoder.decode(2,pos_evidence_lattice,lattice_scorer,{},pos_options);
 assert(!pos_evidence_paths.empty());
 assert(pos_evidence_paths.front().tokens[1].candidate.id=="PB_TOKEN");



 LexiconTrie pack_lexicon;MaskedNgramScorer pack_scorer;std::size_t pack_entries=0;
 assert(load_language_pack(std::string(KANA_DECODER_SOURCE_DIR)+"/packs/ja-lite",pack_lexicon,pack_scorer,&pack_entries));
 assert(pack_entries==7&&pack_lexicon.size()==7);
 const std::string valid_pack_path=std::string(KANA_DECODER_SOURCE_DIR)+"/packs/ja-lite";
 assert(verify_pack_manifest(valid_pack_path));
 const auto manifest_tmp=std::filesystem::temp_directory_path()/"kana_decoder_tampered_pack";
 std::filesystem::remove_all(manifest_tmp);
 std::filesystem::copy(valid_pack_path,manifest_tmp,std::filesystem::copy_options::recursive);
 {std::ofstream tamper(manifest_tmp/"lexicon.tsv",std::ios::app);tamper<<"tampered\\n";}
 assert(!verify_pack_manifest(manifest_tmp.string()));
 LexiconTrie rejected_lexicon;MaskedNgramScorer rejected_scorer;
 assert(!load_language_pack(manifest_tmp.string(),rejected_lexicon,rejected_scorer));
 std::filesystem::remove_all(manifest_tmp);
 ConnectionModel pack_connections;CorrectionOptions pack_corrections;assert(load_language_pack(std::string(KANA_DECODER_SOURCE_DIR)+"/packs/ja-lite",pack_lexicon,pack_scorer,&pack_entries,&pack_connections,&pack_corrections));
 assert(pack_connections.cost(1,1)==0&&pack_connections.cost(1,2)==20);
 assert(pack_corrections.rules.size()==3&&pack_corrections.rules.front().rule_id=="ja-voicing-01");
 LexiconTrie correction_pack_trie;
 assert(correction_pack_trie.add("かんじん",LatticeCandidate{"kanjin","肝心",10,0}));
 CorrectionOptions correction_pack_options=pack_corrections;
 correction_pack_options.minimum_improvement=1.0;correction_pack_options.minimum_score=-1000000000.0;
 auto pack_rule_decode=decode_with_corrections("かんしん",correction_pack_trie,pack_scorer,correction_pack_options,dop);
 assert(pack_rule_decode.correction_applied&&pack_rule_decode.decoded_reading=="かんじん");
 std::string malformed;malformed.push_back(static_cast<char>(0xf0));malformed.push_back(static_cast<char>(0x28));malformed.push_back(static_cast<char>(0x8c));malformed.push_back(static_cast<char>(0x28));
 assert(is_valid_utf8("こんにちは🙂")&&!is_valid_utf8(malformed));
 std::string overlong;overlong.push_back(static_cast<char>(0xc0));overlong.push_back(static_cast<char>(0xaf));assert(!pack_lexicon.add(overlong,{"bad","bad",0,0}));
 assert(pack_scorer.score("rain",{"あした"},{})>pack_scorer.score("candy",{"あした"},{}));
 LexiconTrie contextual_trie;
 assert(contextual_trie.add("あした",{"あした","明日",100,0}));
 assert(contextual_trie.add("あめ",{"rain","雨",100,0}));
 assert(contextual_trie.add("あめ",{"candy","飴",100,0}));
 auto context_paths=decode_reading("あしたあめ",contextual_trie,pack_scorer,dop);
 assert(!context_paths.empty()&&context_paths[0].tokens.size()==2);
 assert(context_paths[0].tokens[1].candidate.id=="rain");
 const auto morphology_tsv=std::filesystem::temp_directory_path()/"kana_decoder_inflection.tsv";
 {std::ofstream f(morphology_tsv);f<<"たべる\t食べる\t食べる\t動詞/一般\tverb\tverb_taberu\t20\t0\t7\t8\t下一段-バ行\t終止形-一般\n";}
 LexiconTrie morphology_trie;assert(morphology_trie.load_tsv(morphology_tsv.string()));std::filesystem::remove(morphology_tsv);
 auto morphology_path=decode_reading("たべる",morphology_trie,pack_scorer,dop);
 assert(!morphology_path.empty());
 assert(morphology_path.front().tokens.front().candidate.morphology.language_tag=="ja-JP");
 assert(morphology_path.front().tokens.front().candidate.morphology.part_of_speech=="動詞/一般");
 assert(morphology_path.front().tokens.front().candidate.morphology.inflection_class=="下一段-バ行");
 assert(morphology_path.front().tokens.front().candidate.morphology.inflection_form=="終止形-一般");

 // SURGE evidence gate resolves only when at least two distinct candidate surfaces
 // have observed left-context evidence and the evidence margin clears threshold.
 MaskedNgramScorer surge_scorer;
 surge_scorer.add_unigram("P",10);surge_scorer.add_unigram("A",10);surge_scorer.add_unigram("B",10);
 surge_scorer.add_left_bigram("P","A",80);surge_scorer.add_left_bigram("P","B",1);
 std::vector<LatticeEdge> surge_lattice{
  {0,1,{{"P","前",100,0}}},
  {1,2,{{"A","雨",100,0},{"B","飴",100,0}}}
 };
 DecoderOptions surge_options=dop;surge_options.surge_left_context_gate=true;
 surge_options.surge_context_margin=0.5;
 auto surge_paths=decoder.decode(2,surge_lattice,surge_scorer,{},surge_options);
 assert(!surge_paths.empty()&&surge_paths.front().tokens.size()==2);
 assert(surge_paths.front().tokens[1].candidate.id=="A");
 // One supported surface is insufficient: the gate abstains and contributes
 // no context score, even if that surface has a strong observed count.
 MaskedNgramScorer one_sided_scorer;
 one_sided_scorer.add_unigram("P",10);one_sided_scorer.add_unigram("A",10);one_sided_scorer.add_unigram("B",10);
 one_sided_scorer.add_left_bigram("P","A",80);
 std::vector<CandidateScoreTrace> abstain_trace;
 auto abstain_paths=decoder.decode(2,surge_lattice,one_sided_scorer,{},surge_options,
                                   nullptr,&abstain_trace);
 assert(!abstain_paths.empty()&&abstain_paths.front().tokens.size()==2);
 for(const auto& item:abstain_trace){
  if(item.start==1)assert(item.context_evidence_score==0.0&&item.weighted_context_score==0.0);
 }
 // Trie-backed Pack loading, lattice generation and bounded streaming revisions.
 LexiconTrie trie;std::size_t loaded=0;
 const std::string pack_path=std::string(KANA_DECODER_SOURCE_DIR)+"/packs/ja-lite/lexicon.tsv";
 assert(trie.load_tsv(pack_path,&loaded));assert(loaded>=7&&trie.size()==loaded);
 auto generated=trie.build_lattice("あめ",16,true);
 assert(generated.size()==2&&generated[0].start==0&&generated[0].end==2);
 assert(generated[1].start==1&&generated[1].end==2&&generated[1].candidates[0].surface=="め");
 assert(generated[0].candidates.size()==2);
 // High-cardinality trie terminals retain the same stable, surface-diverse
 // top-eight shortlist while materializing only those eight candidates.
 const auto shortlist_tsv=std::filesystem::temp_directory_path()/"kana_decoder_shortlist.tsv";
 {
  std::ofstream out(shortlist_tsv);
  out<<"かな\t重複\t\t名詞\tfeature\tdup-first\t100\t0\t0\t0\t\t\n";
  for(int i=0;i<18;++i)
   out<<"かな\t候補"<<i<<"\t\t名詞\tfeature\tid"<<i<<"\t"<<i<<"\t0\t0\t0\t\t\n";
  out<<"かな\t重複\t\t名詞\tfeature\tdup-late\t-1\t0\t0\t0\t\t\n";
  for(int i=0;i<10;++i)
   out<<"かた\t同一表層\t\t名詞\tfeature\tsame"<<i<<"\t"<<i<<"\t0\t0\t0\t\t\n";
 }
 LexiconTrie shortlist_trie;
 assert(shortlist_trie.load_tsv(shortlist_tsv.string()));
 std::filesystem::remove(shortlist_tsv);
 const auto shortlist=shortlist_trie.build_lattice("かな",16,false);
 assert(shortlist.size()==1&&shortlist.front().candidates.size()==8);
 assert(shortlist.front().candidates.front().surface=="重複");
 assert(shortlist.front().candidates.front().id=="dup-late");
 for(std::size_t i=1;i<shortlist.front().candidates.size();++i)
  assert(shortlist.front().candidates[i-1].word_cost<=shortlist.front().candidates[i].word_cost);
 const auto repeated_surface=shortlist_trie.build_lattice("かた",16,false);
 assert(repeated_surface.size()==1&&repeated_surface.front().candidates.size()==8);
 assert(repeated_surface.front().candidates.front().id=="same0");
 auto generated_paths=decode_reading("あめ",trie,lattice_scorer,dop);
 assert(!generated_paths.empty()&&generated_paths[0].tokens.size()==1);
 CorrectionOptions integrated_correction;integrated_correction.minimum_improvement=1.0;integrated_correction.minimum_score=-1000000000.0;
 auto corrected_decode=decode_with_corrections("あめめ",trie,lattice_scorer,integrated_correction,dop);
 assert(corrected_decode.correction_applied&&corrected_decode.decoded_reading=="あめ");
 assert(!corrected_decode.paths.empty()&&corrected_decode.paths.front().tokens.size()==1);
 auto protected_decode=decode_with_corrections("12333",trie,lattice_scorer,integrated_correction,dop);
 assert(!protected_decode.correction_applied&&protected_decode.decoded_reading=="12333");
 auto unknown=trie.build_lattice("ぴ",16,true);
 assert(unknown.size()==1&&unknown[0].candidates[0].surface=="ぴ");
 // The host StreamingSession accepts provider-decoded paths only. Keep
 // this unit test explicit about using the Japanese decoder as its provider.
 auto append_stream = [](StreamingSession& target, const std::string& chunk,
                         const LexiconTrie& source_lexicon,
                         const MaskedNgramScorer& source_scorer,
                         StreamingSnapshot& target_snapshot,
                         const DecoderOptions& options) {
  const auto combined = target.reading() + chunk;
  auto decoded = decode_reading(combined, source_lexicon, source_scorer, options);
  return target.append_decoded(chunk, std::move(decoded), target_snapshot,
                               options.max_input_codepoints);
 };
 StreamingSession session(4);StreamingSnapshot snapshot;
 assert(append_stream(session,"あ",trie,lattice_scorer,snapshot,dop));
 assert(snapshot.revision==1&&snapshot.provisional&&snapshot.reading=="あ");
 StreamingSnapshot stale_after_append=snapshot;std::string stale_commit;
 assert(append_stream(session,"め",trie,lattice_scorer,snapshot,dop));
 assert(!session.commit_best(stale_after_append,stale_commit));
 assert(snapshot.revision==2&&snapshot.reading=="あめ"&&!snapshot.paths.empty());
 assert(!append_stream(session,"がふる",trie,lattice_scorer,snapshot,dop));
 assert(!append_stream(session,malformed,trie,lattice_scorer,snapshot,dop));
 assert(session.reading()=="あめ"&&snapshot.revision==2);
 StreamingSnapshot stale_snapshot=snapshot;session.reset();assert(session.reading().empty());
 std::string stale_output;assert(!session.commit_best(stale_snapshot,stale_output));
 assert(append_stream(session,"あめ",trie,lattice_scorer,snapshot,dop));
 std::string committed;assert(session.commit_best(snapshot,committed));
 assert(committed=="雨"&&!snapshot.provisional&&snapshot.revision==5);
 assert(session.reading().empty());


 // Native Pack verification must accept a declared budget above the provisional 30 MiB target.
 const auto large_budget_pack=std::filesystem::temp_directory_path()/"kana_decoder_large_budget_pack";
 std::filesystem::remove_all(large_budget_pack);
 std::filesystem::copy(std::filesystem::path(KANA_DECODER_SOURCE_DIR)/"packs"/"ja-lite",large_budget_pack,std::filesystem::copy_options::recursive);
 const auto large_budget_manifest=large_budget_pack/"manifest.json";
 {std::ifstream in(large_budget_manifest);std::string json((std::istreambuf_iterator<char>(in)),{});
  const auto marker=json.find("31457280");assert(marker!=std::string::npos);json.replace(marker,8,"67108864");
  std::ofstream out_file(large_budget_manifest);out_file<<json;}
 std::string large_budget_error;assert(verify_pack_manifest(large_budget_pack.string(),&large_budget_error));
 std::filesystem::remove_all(large_budget_pack);

 // Native verification must enforce the same nested resource limit as Python.
 const auto undersized_budget_pack=std::filesystem::temp_directory_path()/"kana_decoder_undersized_budget_pack";
 std::filesystem::remove_all(undersized_budget_pack);
 std::filesystem::copy(std::filesystem::path(KANA_DECODER_SOURCE_DIR)/"packs"/"ja-lite",undersized_budget_pack,std::filesystem::copy_options::recursive);
 const auto undersized_manifest=undersized_budget_pack/"manifest.json";
 {std::ifstream in(undersized_manifest);std::string json((std::istreambuf_iterator<char>(in)),{});
  const auto marker=json.find("31457280");assert(marker!=std::string::npos);json.replace(marker,8,"1");
  std::ofstream out_file(undersized_manifest);out_file<<json;}
 std::string undersized_error;assert(!verify_pack_manifest(undersized_budget_pack.string(),&undersized_error));
 assert(undersized_error=="Pack expanded size exceeds declared limit");
 std::filesystem::remove_all(undersized_budget_pack);

 // Exercise the streaming SHA-256 path across multiple 64 KiB input chunks.
 const auto streaming_hash_pack=std::filesystem::temp_directory_path()/"kana_decoder_streaming_hash_pack";
 std::filesystem::remove_all(streaming_hash_pack);
 std::filesystem::copy(std::filesystem::path(KANA_DECODER_SOURCE_DIR)/"packs"/"ja-lite",streaming_hash_pack,std::filesystem::copy_options::recursive);
 {std::ofstream large(streaming_hash_pack/"large.bin",std::ios::binary);for(int i=0;i<70000;++i)large.put('x');}
 const auto streaming_manifest=streaming_hash_pack/"manifest.json";
 {std::ifstream in(streaming_manifest);std::string json((std::istreambuf_iterator<char>(in)),{});
  const auto files_pos=json.find("\"files\"");assert(files_pos!=std::string::npos);
  const auto files_end=json.find("\n ]",files_pos);assert(files_end!=std::string::npos);
  json.insert(files_end,",\n  {\"path\":\"large.bin\",\"size\":70000,\"sha256\":\"bca09f4a757d5571c7d9f3341d4301f3c391c090826acc1a3013c6bcb7c01722\"}");
  std::ofstream out_file(streaming_manifest);out_file<<json;}
 std::string streaming_hash_error;assert(verify_pack_manifest(streaming_hash_pack.string(),&streaming_hash_error));
 std::filesystem::remove_all(streaming_hash_pack);

 // Integrated C ABI: load a local Language Pack and decode kana to a surface form.
 kd_engine* engine=kd_engine_create();assert(engine);
 assert(kd_engine_load_pack(engine,(std::string(KANA_DECODER_SOURCE_DIR)+"/packs/ja-lite").c_str())==0);
 char converted[64]={};assert(kd_engine_decode(engine,"あめ",converted,sizeof(converted))==0);
 assert(std::string(converted)=="雨");
 assert(kd_engine_decode(engine,"あめ",converted,1)==-2);
 assert(kd_engine_decode(engine,malformed.c_str(),converted,sizeof(converted))==-1);
 kd_engine_destroy(engine);

 // Commit only the shared token prefix, retaining ambiguous suffix for later context.
 LexiconTrie partial_lexicon;
 assert(partial_lexicon.add("きょう",{"today","今日",10,0}));
 assert(partial_lexicon.add("は",{"topic","は",10,0}));
 assert(partial_lexicon.add("し",{"shi1","詩",10,0}));
 assert(partial_lexicon.add("し",{"shi2","市",10,0}));
 StreamingSession partial(16);StreamingSnapshot partial_snapshot;
 assert(append_stream(partial,"きょうはし",partial_lexicon,lattice_scorer,partial_snapshot,dop));
 assert(partial_snapshot.paths.size()>=2);
 std::string prefix;assert(partial.commit_common_prefix(partial_snapshot,prefix));
 assert(prefix=="今日は"&&partial.reading()=="し"&&partial_snapshot.reading=="し");
 assert(partial_snapshot.provisional&&!partial_snapshot.paths.empty());
 std::cout<<"all C++ tests passed\n";
}
