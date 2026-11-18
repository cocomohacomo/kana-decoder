#include "kana_decoder/pack_manifest.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr std::array<std::uint32_t,64> K = {
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
std::uint32_t rotr(std::uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
std::string sha256_file(const std::filesystem::path& path) {
 std::ifstream in(path,std::ios::binary);if(!in)return{};
 std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),{});
 std::uint64_t bits=static_cast<std::uint64_t>(data.size())*8;
 data.push_back(0x80);while(data.size()%64!=56)data.push_back(0);
 for(int i=7;i>=0;--i)data.push_back(static_cast<unsigned char>((bits>>(i*8))&0xff));
 std::array<std::uint32_t,8> h={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
 for(std::size_t off=0;off<data.size();off+=64){
  std::array<std::uint32_t,64>w{};
  for(int i=0;i<16;++i){std::size_t p=off+static_cast<std::size_t>(i)*4;w[i]=(std::uint32_t(data[p])<<24)|(std::uint32_t(data[p+1])<<16)|(std::uint32_t(data[p+2])<<8)|data[p+3];}
  for(int i=16;i<64;++i){auto s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);auto s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
  auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
  for(int i=0;i<64;++i){auto s1=rotr(e,6)^rotr(e,11)^rotr(e,25);auto ch=(e&f)^((~e)&g);auto t1=z+s1+ch+K[i]+w[i];auto s0=rotr(a,2)^rotr(a,13)^rotr(a,22);auto maj=(a&b)^(a&c)^(b&c);auto t2=s0+maj;z=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
  h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
 }
 std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto x:h)out<<std::setw(8)<<x;return out.str();
}
bool fail(std::string* error,const std::string& message){if(error)*error=message;return false;}
bool capture(const std::string& text,const std::regex& re,std::string& out){std::smatch m;if(!std::regex_search(text,m,re)||m.size()<2)return false;out=m[1].str();return true;}
bool safe_path(const std::string& value){
 if(value.empty()||value.front()=='/'||value.find('\\')!=std::string::npos||value.find(':')!=std::string::npos)return false;
 std::filesystem::path p(value);
 if(p.is_absolute()||p.has_root_path())return false;
 for(const auto& part:p)if(part==".."||part=="."||part.empty())return false;
 return true;
}
}
namespace kana_decoder {
bool verify_pack_manifest(const std::string& directory,std::string* error){
 namespace fs=std::filesystem;
 const fs::path root=fs::canonical(fs::path(directory));
 const fs::path manifest_path=root/"manifest.json";
 if(fs::is_symlink(fs::symlink_status(manifest_path))||!fs::is_regular_file(manifest_path))return fail(error,"manifest missing or unsafe");
 std::ifstream mf(manifest_path,std::ios::binary);std::string json((std::istreambuf_iterator<char>(mf)),{});
 std::string value;
 if(!capture(json,std::regex(R"("core_abi_version"\s*:\s*"([^"]+)")"),value)||value!="0.1")return fail(error,"unsupported core ABI");
 if(!capture(json,std::regex(R"("format_version"\s*:\s*([0-9]+))"),value)||value!="1")return fail(error,"unsupported Pack format");
 for(const char* key:{"pack_id","language_id","pack_version"}){
  if(!capture(json,std::regex(std::string("\"")+key+R"("\s*:\s*"([^"]+)")"),value)||value.empty())return fail(error,"missing Pack identity field");
 }
 std::uint64_t limit=30u*1024u*1024u;
 if(capture(json,std::regex(R"("max_expanded_bytes"\s*:\s*([0-9]+))"),value)){
  try{limit=std::stoull(value);}catch(...){return fail(error,"invalid resource limit");}
 }
 if(limit==0||limit>30u*1024u*1024u)return fail(error,"Pack resource limit exceeds runtime ceiling");
 std::string license_block;
 if(!capture(json,std::regex(R"("license"\s*:\s*\{([\s\S]*?)\})"),license_block))return fail(error,"license metadata missing");
 std::string license_file,spdx;
 if(!capture(license_block,std::regex(R"("file"\s*:\s*"([^"]+)")"),license_file)||
    !capture(license_block,std::regex(R"("spdx"\s*:\s*"([^"]+)")"),spdx)||spdx.empty()||!safe_path(license_file))
  return fail(error,"invalid license metadata");
 std::string files_block;
 if(!capture(json,std::regex(R"("files"\s*:\s*\[([\s\S]*?)\])"),files_block))return fail(error,"manifest files list missing");
 const std::regex entry_re(R"(\{\s*"path"\s*:\s*"([^"]+)"\s*,\s*"size"\s*:\s*([0-9]+)\s*,\s*"sha256"\s*:\s*"([0-9a-fA-F]{64})"\s*\})");
 std::set<std::string> listed;std::uint64_t total=0;bool has_lex=false,has_ngram=false,has_license=false;
 for(std::sregex_iterator it(files_block.begin(),files_block.end(),entry_re),end;it!=end;++it){
  const std::string relative=(*it)[1].str(),hash=(*it)[3].str();
  if(!safe_path(relative)||!listed.insert(relative).second)return fail(error,"unsafe or duplicate Pack path");
  std::uint64_t expected=0;try{expected=std::stoull((*it)[2].str());}catch(...){return fail(error,"invalid file size");}
  fs::path file=root/fs::path(relative);
  if(fs::is_symlink(fs::symlink_status(file))||!fs::is_regular_file(file))return fail(error,"Pack payload missing or symlink");
  fs::path resolved=fs::canonical(file);
  auto rel=resolved.lexically_relative(root);
  if(rel.empty()||rel.native().find("..")==0)return fail(error,"Pack payload escapes root");
  auto actual=fs::file_size(file);if(actual!=expected)return fail(error,"Pack payload size mismatch");
  if(total>limit-std::min<std::uint64_t>(actual,limit))return fail(error,"Pack expanded size exceeds limit");
  total+=actual;if(total>limit)return fail(error,"Pack expanded size exceeds limit");
  if(sha256_file(file)!=hash)return fail(error,"Pack payload SHA-256 mismatch");
  if(relative=="lexicon.tsv")has_lex=true;if(relative=="ngram.tsv")has_ngram=true;if(relative==license_file)has_license=true;
 }
 if(listed.empty()||!has_lex||!has_ngram||!has_license)return fail(error,"required Pack payload or hashed license missing");
 for(fs::recursive_directory_iterator it(root),end;it!=end;++it){
  if(it->is_symlink())return fail(error,"symlink found in Pack directory");
  if(it->is_regular_file()){
   auto rel=it->path().lexically_relative(root).generic_string();
   if(rel!="manifest.json"&&!listed.count(rel))return fail(error,"unlisted Pack payload");
  }
 }
 if(error)error->clear();return true;
}
}
