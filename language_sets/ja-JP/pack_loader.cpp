#include "kana_decoder/japanese_decoder.hpp"
#include "kana_decoder/pack_manifest.hpp"
#include <charconv>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
namespace kana_decoder {
static bool load_correction_rules_file(const std::string& path,std::vector<CorrectionRule>& rules){
 std::ifstream in(path);if(!in)return false;
 std::vector<CorrectionRule> parsed;std::set<std::string> ids;std::string line;
 while(std::getline(in,line)){
  if(line.empty()||line[0]=='#')continue;
  std::vector<std::string> cols;std::stringstream ss(line);std::string field;
  while(std::getline(ss,field,'\t'))cols.push_back(field);
  if(cols.size()!=5||cols[2].empty()||cols[0].size()>256||cols[1].size()>256||cols[2].size()>128||parsed.size()>=256||!ids.insert(cols[2]).second)return false;
  if(!is_valid_utf8(cols[0])||!is_valid_utf8(cols[1])||!is_valid_utf8(cols[2]))return false;
  CorrectionKind kind;
  if(cols[3]=="insert")kind=CorrectionKind::Insert;
  else if(cols[3]=="delete")kind=CorrectionKind::Delete;
  else if(cols[3]=="replace")kind=CorrectionKind::Replace;
  else if(cols[3]=="duplicate")kind=CorrectionKind::Duplicate;
  else if(cols[3]=="phonetic")kind=CorrectionKind::Phonetic;
  else return false;
  if((kind==CorrectionKind::Insert&&(cols[0].size()!=0||cols[1].empty()))||
     (kind==CorrectionKind::Delete&&(cols[0].empty()||!cols[1].empty()))||
     ((kind==CorrectionKind::Replace||kind==CorrectionKind::Phonetic)&&(cols[0].empty()||cols[1].empty())))return false;
  std::size_t used=0;double cost=0.0;
  try{cost=std::stod(cols[4],&used);}catch(...){return false;}
  if(used!=cols[4].size()||!std::isfinite(cost)||cost<0.0)return false;
  parsed.push_back({cols[0],cols[1],cols[2],kind,cost});
 }
 if (!in.eof()) return false;
 rules = std::move(parsed);
 return true;
}

bool load_language_pack(const std::string& directory,LexiconTrie& lexicon,MaskedNgramScorer& scorer,std::size_t* loaded,ConnectionModel* connections,CorrectionOptions* corrections,std::string* error){
 auto fail=[&](const char* reason){if(error)*error=reason;return false;};
 std::string base=directory.empty()?std::string("."):directory;
 if(!verify_pack_manifest(base,error))return false;
 if(base.back()!='/')base+='/';
 LexiconTrie parsed_lexicon;MaskedNgramScorer parsed_scorer;std::size_t count=0;
 std::ifstream compressed_lexicon(base+"lexicon.tsv.gz",std::ios::binary);
 const std::string lexicon_path=compressed_lexicon?base+"lexicon.tsv.gz":base+"lexicon.tsv";
 if(!parsed_lexicon.load_tsv(lexicon_path,&count))return fail("LANGUAGE_PACK_LEXICON_PARSE_FAILED");
 if(!parsed_scorer.load_tsv(base+"ngram.tsv"))return fail("LANGUAGE_PACK_NGRAM_PARSE_FAILED");
 ConnectionModel parsed_connections;std::ifstream conn_file(base+"connections.tsv");if(conn_file&&!parsed_connections.load_tsv(base+"connections.tsv"))return fail("LANGUAGE_PACK_CONNECTIONS_PARSE_FAILED");
 CorrectionOptions parsed_corrections;std::ifstream correction_file(base+"correction_rules.tsv");
 if(correction_file&&!load_correction_rules_file(base+"correction_rules.tsv",parsed_corrections.rules))return fail("LANGUAGE_PACK_CORRECTION_RULES_PARSE_FAILED");
 lexicon=std::move(parsed_lexicon);scorer=std::move(parsed_scorer);if(connections)*connections=std::move(parsed_connections);
 if (corrections) *corrections = std::move(parsed_corrections);
 if (loaded) *loaded = count;
 if(error)error->clear();
 return true;
}
}
