#include "kana_decoder/engine.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
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
    std::size_t total=0,exact=0,edits=0,reference_chars=0,failed=0;
    std::string line;
    while(std::getline(in,line)){
        if(line.empty()||line[0]=='#')continue;
        auto tab=line.find('\t');
        if(tab==std::string::npos||line.find('\t',tab+1)!=std::string::npos){std::cerr<<"invalid corpus row\n";return 2;}
        std::string reading=line.substr(0,tab),reference=line.substr(tab+1);
        if(!kana_decoder::is_valid_utf8(reading)||!kana_decoder::is_valid_utf8(reference)){std::cerr<<"invalid UTF-8 in corpus\n";return 2;}
        auto paths=kana_decoder::decode_reading(reading,lexicon,scorer,{},&connections);
        std::string predicted;
        if(!paths.empty())for(const auto& token:paths.front().tokens)predicted+=token.candidate.surface;
        else ++failed;
        auto ref_cp=codepoints(reference),pred_cp=codepoints(predicted);
        const auto distance=edit_distance(pred_cp,ref_cp);
        ++total;edits+=distance;reference_chars+=ref_cp.size();if(predicted==reference)++exact;
    }
    if(total==0){std::cerr<<"evaluation corpus is empty\n";return 2;}
    const double accuracy=static_cast<double>(exact)/total;
    const double cer=reference_chars?static_cast<double>(edits)/reference_chars:(edits?1.0:0.0);
    std::cout<<std::fixed<<std::setprecision(6)
      <<"{\"cases\":"<<total<<",\"exact_matches\":"<<exact
      <<",\"exact_accuracy\":"<<accuracy<<",\"edit_distance\":"<<edits
      <<",\"reference_codepoints\":"<<reference_chars<<",\"cer\":"<<cer
      <<",\"decode_failures\":"<<failed<<"}\n";
    return 0;
}
