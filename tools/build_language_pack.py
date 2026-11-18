#!/usr/bin/env python3
"""Build a deterministic offline Language Pack from normalized TSV inputs.

Dictionary TSV columns:
kana, surface, lemma, pos, reading_group[, candidate_id, word_cost,
connection_cost, left_id, right_id, inflection_type, inflection_form]
Training sequence TSV: one utterance per line, candidate IDs separated by tabs.
"""
import argparse,hashlib,json,re,shutil,subprocess,sys
from collections import Counter
from pathlib import Path
MAX_I32=2**31-1
MAX_U32=2**32-1
def clean_field(value,label):
 value=value.strip()
 if not value or any(ch in value for ch in "\t\r\n"):raise ValueError(f"invalid {label}")
 return value
def parse_i32(value,label,minimum=-(2**31)):
 try:number=int(value)
 except ValueError as exc:raise ValueError(f"invalid integer {label}: {value}") from exc
 if number<minimum or number>MAX_I32:raise ValueError(f"integer out of range {label}: {value}")
 return number
def import_lexicon(source):
 rows=[];seen=set()
 with source.open(encoding="utf-8",newline="") as stream:
  for line_no,line in enumerate(stream,1):
   if not line.strip() or line.startswith("#"):continue
   cols=line.rstrip("\r\n").split("\t")
   if len(cols)<5 or len(cols)>12:raise ValueError(f"{source}:{line_no}: expected 5..12 columns")
   cols+=[""]*(12-len(cols));kana,surface,lemma,pos,group,candidate_id=cols[:6]
   kana=clean_field(kana,"reading");surface=clean_field(surface,"surface");lemma=clean_field(lemma or surface,"lemma");pos=clean_field(pos,"pos");group=clean_field(group,"reading_group");candidate_id=clean_field(candidate_id or group,"candidate_id")
   costs=[parse_i32(cols[6] or "0","word_cost"),parse_i32(cols[7] or "0","connection_cost"),parse_i32(cols[8] or "0","left_id",0),parse_i32(cols[9] or "0","right_id",0)]
   row=[kana,surface,lemma,pos,group,candidate_id]+[str(x) for x in costs]+[cols[10].strip(),cols[11].strip()];key=tuple(row)
   if key not in seen:seen.add(key);rows.append(row)
 if not rows:raise ValueError("dictionary contains no entries")
 rows.sort(key=lambda row:(row[0],row[6],row[5],row[1]));return rows
def train_ngrams(sequence_file,known_candidates):
 counts=Counter()
 if sequence_file is None:return counts
 with sequence_file.open(encoding="utf-8") as stream:
  for line_no,line in enumerate(stream,1):
   if not line.strip() or line.startswith("#"):continue
   tokens=[clean_field(x,"candidate token") for x in line.rstrip("\r\n").split("\t")]
   unknown=[token for token in tokens if token not in known_candidates]
   if unknown:raise ValueError(f"{sequence_file}:{line_no}: unknown candidate IDs: {unknown}")
   for i,token in enumerate(tokens):
    counts[("U","",token)]+=1
    if i:counts[("L",tokens[i-1],token)]+=1
    if i+1<len(tokens):counts[("R",tokens[i+1],token)]+=1
 if any(value>MAX_U32 for value in counts.values()):raise ValueError("trained n-gram count exceeds uint32 pack format")
 return counts
def copy_license_profile(profile_path,out,payload_names):
 profile_path=profile_path.resolve();profile=json.loads(profile_path.read_text(encoding="utf-8"))
 if not isinstance(profile,dict) or profile.get("schema_version")!=1:raise ValueError("license profile schema_version must be 1")
 components=profile.get("components")
 if not isinstance(components,list) or not components:raise ValueError("license profile must contain components")
 normalized=[];seen=set();notice_dir=out/"THIRD_PARTY_LICENSES";notice_dir.mkdir(parents=True,exist_ok=True)
 for item in components:
  if not isinstance(item,dict):raise ValueError("license component must be an object")
  ident=item.get("id")
  if not isinstance(ident,str) or not re.fullmatch("[a-z0-9][a-z0-9._-]{1,63}",ident) or ident in seen:raise ValueError("invalid or duplicate license component id")
  seen.add(ident);source_value=item.get("license_file")
  if not isinstance(source_value,str) or not source_value:raise ValueError(f"{ident}: license_file is required")
  source_input=profile_path.parent/source_value
  if source_input.is_symlink():raise ValueError(f"{ident}: symlink license_file is not permitted")
  source=source_input.resolve()
  if profile_path.parent.resolve() not in source.parents or not source.is_file():raise ValueError(f"{ident}: license_file missing or outside profile directory")
  notice_name=f"{ident}.txt";notice_rel=f"THIRD_PARTY_LICENSES/{notice_name}";shutil.copyfile(source,notice_dir/notice_name);payload_names.append(notice_rel)
  normalized.append({"id":ident,"version":item.get("version"),"source_sha256":item.get("source_sha256"),"spdx":item.get("spdx"),"commercial_use":item.get("commercial_use"),"redistribution":item.get("redistribution"),"modified_redistribution":item.get("modified_redistribution"),"upstream_url":item.get("upstream_url"),"notice_file":notice_rel})
 output_profile={"schema_version":1,"components":normalized};(out/"license-profile.json").write_text(json.dumps(output_profile,ensure_ascii=False,indent=2)+"\n",encoding="utf-8");payload_names.append("license-profile.json")
 validator=Path(__file__).with_name("validate_license_profile.py");subprocess.run([sys.executable,str(validator),str(out)],check=True,capture_output=True,text=True)
 return normalized
def write_pack(args):
 out=args.output.resolve()
 if out.exists() and any(out.iterdir()):raise ValueError("output directory must be empty")
 out.mkdir(parents=True,exist_ok=True);rows=import_lexicon(args.lexicon.resolve());known={row[5] for row in rows};counts=train_ngrams(args.sequences.resolve() if args.sequences else None,known)
 with (out/"lexicon.tsv").open("w",encoding="utf-8",newline="\n") as f:
  f.write("# kana\tsurface\tlemma\tpos\treading_group\tcandidate_id\tword_cost\tconnection_cost\tleft_id\tright_id\tinflection_type\tinflection_form\n")
  for row in rows:f.write("\t".join(row)+"\n")
 with (out/"ngram.tsv").open("w",encoding="utf-8",newline="\n") as f:
  f.write("# direction\tcontext_token\tcandidate_id\tcount\n")
  for (direction,context,candidate),count in sorted(counts.items()):f.write(f"{direction}\t{context}\t{candidate}\t{count}\n")
 payload=["lexicon.tsv","ngram.tsv"]
 for arg,name in ((args.connections,"connections.tsv"),(args.correction_rules,"correction_rules.tsv")):
  if arg:
   source=arg.resolve()
   if not source.is_file():raise ValueError(f"{name} source does not exist")
   shutil.copyfile(source,out/name);payload.append(name)
 license_source=args.license_file.resolve()
 if not license_source.is_file():raise ValueError("license file does not exist")
 shutil.copyfile(license_source,out/"LICENSE");payload.append("LICENSE");components=[]
 if args.license_profile:
  components=copy_license_profile(args.license_profile,out,payload);declared=[x.strip() for x in args.license_spdx.split(" AND ")];expected=sorted({x["spdx"] for x in components})
  if len(declared)!=len(set(declared)) or sorted(declared)!=expected:raise ValueError("combined --license-spdx must exactly match the component SPDX conjunction")
 files=[]
 for name in payload:
  data=(out/name).read_bytes();files.append({"path":name,"size":len(data),"sha256":hashlib.sha256(data).hexdigest()})
 manifest={"pack_id":args.pack_id,"language_id":args.language_id,"pack_version":args.pack_version,"core_abi_version":"0.1","dictionary_version":args.dictionary_version,"model_version":"candidate-ngram-counts-1","format_version":1,"reading_encoding_id":args.reading_encoding,"normalization_profile":args.normalization_profile,"orthography_profile":args.orthography_profile,"license":{"spdx":args.license_spdx,"file":"LICENSE","upstream":args.upstream,"components":components,"profile_file":"license-profile.json" if components else None},"resource_limits":{"max_expanded_bytes":args.max_expanded_bytes,"max_candidates":8,"max_edit_distance":1},"files":files,"status":"imported-pack-requires-accuracy-validation"}
 (out/"manifest.json").write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
 return {"pack_id":args.pack_id,"entries":len(rows),"trained_ngram_records":len(counts),"files":payload,"max_expanded_bytes":args.max_expanded_bytes}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument("--lexicon",type=Path,required=True);p.add_argument("--output",type=Path,required=True);p.add_argument("--license-file",type=Path,required=True);p.add_argument("--license-profile",type=Path);p.add_argument("--license-spdx",required=True);p.add_argument("--upstream",required=True);p.add_argument("--pack-id",required=True);p.add_argument("--language-id",required=True);p.add_argument("--pack-version",default="0.1.0");p.add_argument("--dictionary-version",default="imported");p.add_argument("--sequences",type=Path);p.add_argument("--connections",type=Path);p.add_argument("--correction-rules",type=Path);p.add_argument("--reading-encoding",default="utf8");p.add_argument("--normalization-profile",default="none-declared");p.add_argument("--orthography-profile",default="language-default");p.add_argument("--max-expanded-bytes",type=int,default=8*1024*1024*1024,help="Pack-declared expanded byte budget; default 8 GiB, independent of the provisional 30 MiB optimization target")
 a=p.parse_args()
 if a.max_expanded_bytes<=0:p.error("--max-expanded-bytes must be positive")
 if not a.license_spdx.strip() or not a.upstream.strip():p.error("license SPDX and upstream metadata are required")
 print(json.dumps(write_pack(a),ensure_ascii=False,indent=2))
if __name__=="__main__":main()
