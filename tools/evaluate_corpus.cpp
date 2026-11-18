#include "kana_decoder/engine.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

static std::vector<std::string> codepoints(const std::string& s) {
    std::vector<std::string> out;
    if (!kana_decoder::is_valid_utf8(s)) return out;
    for (std::size_t i=0;i<s.size();) {
        unsigned char c=static_cast<unsigned char>(s[i]);
        std::size_t n=c<0x80?1:(c&0xe0)==0xc0?2:(c&0xf0)==0xe0?3:4;
        out.push_back(s.substr(i,n)); i+=n;
    }
    return out;
}
static std::size_t edit_distance(const std::vector<std::string>& a,const std::vector<std::string>& b) {
    std::vector<std::size_t> prev(b.size()+1),cur(b.size()+1);
    for(std::size_t j=0;j<=b.size();++j)prev[j]=j;
    for(std::size_t i=1;i<=a.size();++i){
        cur[0]=i;
        for(std::size_t j=1;j<=b.size();++j)
            cur[j]=std::min({prev[j]+1,cur[j-1]+1,prev[j-1]+(a[i-1]==b[j-1]?0:1)});
        prev.swap(cur);
    }
    return prev[b.size()];
}
static std::string json_escape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c=='"' || c=='\\') { out.push_back('\\'); out.push_back(static_cast<char>(c)); }
        else if (c=='\n') out += "\\n";
        else if (c=='\r') out += "\\r";
        else if (c=='\t') out += "\\t";
        else if (c<0x20) out += "?";
        else out.push_back(static_cast<char>(c));
    }
    return out;
}
struct Stats { std::size_t cases=0, exact=0, edits=0, refs=0, failures=0; };
static void print_stats(const Stats& s) {
    const double accuracy=s.cases?static_cast<double>(s.exact)/s.cases:0.0;
    const double cer=s.refs?static_cast<double>(s.edits)/s.refs:(s.edits?1.0:0.0);
    std::cout << "{\"cases\":"<<s.cases<<",\"exact_matches\":"<<s.exact
      <<",\"exact_accuracy\":"<<accuracy<<",\"edit_distance\":"<<s.edits
      <<",\"reference_codepoints\":"<<s.refs<<",\"cer\":"<<cer
      <<",\"decode_failures\":"<<s.failures<<"}";
}
int main(int argc,char** argv) {
    if(argc!=3){std::cerr<<"usage: kana_decoder_eval PACK_DIR CORPUS_TSV\n";return 2;}
    kana_decoder::LexiconTrie lexicon;
    kana_decoder::MaskedNgramScorer scorer;
    kana_decoder::ConnectionModel connections;
    if(!kana_decoder::load_language_pack(argv[1],lexicon,scorer,nullptr,&connections)){
        std::cerr<<"failed to load Language Pack\n";return 2;
    }
    std::ifstream in(argv[2]);
    if(!in){std::cerr<<"cannot open evaluation corpus\n";return 2;}
    Stats total;
    std::map<std::string,Stats> domains;
    std::string line;
    while(std::getline(in,line)){
        if(line.empty()||line[0]=='#')continue;
        auto first=line.find('\t');
        auto second=first==std::string::npos?std::string::npos:line.find('\t',first+1);
        if(first==std::string::npos){std::cerr<<"invalid corpus row\n";return 2;}
        std::string reading=line.substr(0,first);
        std::string reference=second==std::string::npos?line.substr(first+1):line.substr(first+1,second-first-1);
        std::string domain=second==std::string::npos?"unspecified":line.substr(second+1);
        if(domain.empty()||domain.find('\t')!=std::string::npos){std::cerr<<"invalid domain label\n";return 2;}
        if(!kana_decoder::is_valid_utf8(reading)||!kana_decoder::is_valid_utf8(reference)||!kana_decoder::is_valid_utf8(domain)){std::cerr<<"invalid UTF-8 in corpus\n";return 2;}
        auto paths=kana_decoder::decode_reading(reading,lexicon,scorer,{},&connections);
        std::string predicted;
        if(!paths.empty())for(const auto& token:paths.front().tokens)predicted+=token.candidate.surface;
        auto ref_cp=codepoints(reference),pred_cp=codepoints(predicted);
        const auto distance=edit_distance(pred_cp,ref_cp);
        auto add=[&](Stats& s){++s.cases;s.edits+=distance;s.refs+=ref_cp.size();if(predicted==reference)++s.exact;if(paths.empty())++s.failures;};
        add(total);add(domains[domain]);
    }
    if(total.cases==0){std::cerr<<"evaluation corpus is empty\n";return 2;}
    std::cout<<std::fixed<<std::setprecision(6)<<"{\"overall\":";
    print_stats(total);
    std::cout<<",\"domains\":{";
    bool first=true;
    for(const auto& item:domains){if(!first)std::cout<<",";first=false;std::cout<<"\""<<json_escape(item.first)<<"\":";print_stats(item.second);}
    std::cout<<"}}\n";
    return 0;
}
