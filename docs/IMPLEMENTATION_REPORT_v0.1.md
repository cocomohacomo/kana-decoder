# kana-decoder 実装・検証報告 v0.1

- 対象仕様: SPECIFICATION_v0.7.md
- 実装: C++17 Core / C API / Pack validation tools
- 初期ローカル検証環境: Linux x86_64 container, GCC 14.2.0, Python 3.13.5, Release build。最新CI環境はUbuntu 26.04 / GCC 15.2 / Python 3.14.4。
- 判定: コア機能の参照実装とテストは成功。製品相当の全文かな漢字変換器・実用日本語辞書は未完成。

## 1. 実装範囲
| 要求 | 実装 | 現時点の境界 |
|---|---|---|
| 1. 曖昧性候補/API | AmbiguityResult、Candidate、4出力モード、N-best Decoder、統合C ABI decode | C ABIはbest-path単発変換まで。Session/差分/ambiguityの完全公開は未実装 |
| 2. Context-masked n-gram | 左右文脈のmasked n-gram順位付け、最大2トークン窓、加算平滑化 | 手作業fixture count。実コーパス学習・確率校正なし |
| 3. 軽度誤り補正 | UTF-8コードポイント重複削除、Pack規則による挿入・削除・置換、編集コスト、候補上限 | ASR音響信頼度連携・音韻モデル学習なし |
| 4. 依存再評価 | 三層依存グラフ、下流限定無効化、revision | Streaming Session / Claim / Question統合は未実装 |
| 5. テスト | C++ assert、Python Pack検証、CTest | 大規模ゴールドコーパス評価は未実施 |
| 6. 日本語Lite Pack | 語彙・n-gram・接続行列・補正規則・MIT manifest、SHA-256/ライセンス/容量検証 | 小規模fixture。実用語彙カバレッジ/接続精度の30 MiB Packではない |

## 2. API
MaskedNgramScorerは対象spanをマスクした左右文脈から候補順位を計算する。AmbiguityResultは候補順位・top score・margin・曖昧性判定を保持する。Ambiguousモードは低確信時に [雨/飴/other] を生成し、Conservativeモードは元かなを保持する。CorrectionEngineは原文を変更せず補正候補を別に生成し、補正コスト控除後の改善幅と最低スコアで自動採用可否を判定する。DependencyGraphは下流依存だけを無効化する。C APIはn-gram scorerに加え、Language Packロードとbest-path単発デコードを公開する。

候補数は最大8件。左右文脈はそれぞれ最大2トークン。n-gramスコアは未校正の順位スコアであり確率として公開しない。

## 3. テスト結果
実行コマンド:
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j2
    ctest --test-dir build --output-on-failure
    gcc -x c -Iinclude -fsyntax-only <C API smoke source>
    python3 tools/verify_pack.py packs/ja-lite

- C++ Core / C API build: PASS
- C++機能テスト: PASS（曖昧性解消・保持・かなフォールバック、masked trigram、重複削除、音韻置換、促音挿入削除、低確信時非採用、数字保護、無効化、依存再評価、C API）
- Python Pack validation: PASS（7 tests）
- Pack builder tests: PASS（2 tests）
- Corpus quality smoke: PASS（4/4 exact match）
- CTest: 5/5 passed（C API integration / C++ engine / Pack validation / Japanese quality smoke / Pack builder）
- C言語ソースからC ABIをリンクし、Packロード・かな変換・出力バッファ不足・未ロード/不正Packを実行確認: PASS

## 4. サイズ・速度
- 初期参照実装時のRelease static library libkana_decoder_core.a: 131,502 bytes。今回の統合後バイナリサイズは未計測であり、この過去値を現行サイズとして扱わない。
- 現行日本語Lite fixture: payload 1,751 bytes + manifest 1,164 bytes = 2,915 bytes（約0.00278 MiB）。SHA-256全5ファイル検証、30 MiB上限PASS。
- fixtureはテスト用であり、実用辞書のサイズ・語彙カバレッジ評価には使えない。外部辞書はライセンス確認前のため含めていない。
- 旧n-gram microbenchmark: 200,000回の4候補resolve、約2,298 ns/resolve（約2.30 µs）。現行CI microbenchmark（Ubuntu 26.04 / GCC 15.2 / Release）: n-gram resolve 約4,429 ns、2-token/5-edge小規模ラティスdecode 約1,672 ns。単一CIランナー（Ubuntu 26.04 / GCC 15.2 / Release）の小規模測定であり、全文変換・p50/p95/p99・実機CPU・RSSを含まず、1文節1ms未満を証明しない。再現時はActions run 44344624019のログを参照。

## 5. 既知の制約
1. 実用日本語形態素辞書・大規模接続行列は未実装。N-best参照デコーダと小規模接続モデルfixtureは実装済み。
2. n-gram countの簡易学習パイプラインは実装済み。実コーパスの前処理・ドメイン別評価・確率校正は未実施。
3. Correction Engineは候補生成器。辞書・形態素解析器との統合が必要。
4. DependencyGraphは単体実装。Streaming Context CoreのClaim / Evidence / Question / Focusとの統合が必要。
5. Pack Manifest/SHA-256検証はPythonとC++ runtimeの両方に実装済み。mmap、複数Sessionでの辞書共有ポリシーは未実装。
6. 30 MiB実用Packにはライセンス確認済み語彙の選定・インポート・圧縮・カバレッジ測定が必要。
7. UTF-8不正シーケンス拒否とコードポイント範囲処理は実装済み。Unicode NFC等の正規化、全APIにわたる原入力byte span対応は未実装。
8. 補正閾値はfixture上の値。実データで誤修正率・見逃し率を評価して決める。
9. 部分Commitは保持中のN-best上位集合の共通接頭辞に限定。Beamで破棄された候補まで含む保証ではない。
10. C++ Pack loaderはbuilderが生成する限定Manifest形式のhashを検証する。Python verifierも配布前の追加preflightとして実行する。

## 6. 総合判定
**実装基盤と単体テストは成功。製品機能としては部分実装。**

要求1〜4は独立したC++参照実装として動作し、要求5の自動テストと要求6のPack検証prototypeも動作する。一方、全文変換パイプライン・実用辞書・実コーパス評価・runtime Pack loaderは未完了であり、現段階で完成版や性能保証とは扱わない。


## 7. N-best形態素ラティス統合（追加実装）
- `MorphologyDecoder` を追加し、読み位置ごとの `LatticeEdge` と複数の `LatticeCandidate` から完全経路を探索する。
- 各候補の形態素スコアは `-word_cost - connection_cost`。過去最大2トークンの文脈を `MaskedNgramScorer` に渡して加算し、後続形態素による先行候補の再順位付けを行う。
- Beam幅、位置ごとのEdge数、Edgeあたり候補数、返却経路数に上限を設ける。無効な範囲・到達不能な読みは経路を生成しない。
- デコード結果は上位N経路とトークン範囲、候補ID・表層、形態素スコア・文脈スコアを返す。
- テスト追加: 後続トークンの文脈で先行同音異義語が選択されること、読み末尾まで到達しないラティスの拒否、Beam/出力件数上限。
- 注意: この段階ではラティス入力APIのみだったが、後続のsection 8で辞書Trieによる自動生成とStreamingSessionに接続し、section 9で接続IDと部分Commitを追加した。実用日本語辞書・大規模接続行列は未完了。
- 今回、GitHub Actionsワークフロー `.github/workflows/ci.yml` を追加。push / PRでCMakeビルド、CTest、Pack検証を実行する。2026-11-18のGitHub Actions run #4（commit `86583728e1d3de3750641aa7b044f7c0e213a6f0`）でConfigure / Build / CTest / Pack検証がすべて成功。初回CIで検出したfixture Manifestのサイズ・SHA-256不整合を修正し、再実行でPASSを確認した。


## 8. Language Pack辞書Trie・ストリーミング接続（追加実装）
- `LexiconTrie` を追加。UTF-8読みをUnicode code point単位で走査し、辞書一致区間を `LatticeEdge` として自動生成する。
- TSV列は `kana, surface, lemma, pos, reading_group` を必須とし、6列目のcandidate_id、7列目word_cost、8列目connection_costを任意で受け取る。既存の5列形式も互換。
- 一致辞書がない位置には1 code point単位のかなfallback edgeを作り、未知語を失わずに末尾まで経路を構成する。未知語は高コストのため既知語を優先する。
- `MaskedNgramScorer::load_tsv` を追加。PackのL/R/Uレコードを読み込み、Lは左文脈、Rは右文脈、Uはunigramとして取り込む。
- `load_language_pack` はlexicon.tsvとngram.tsvを一時オブジェクトへ読み込み、両方成功した場合のみEngine側へ反映する。Manifest、SHA-256、ライセンス、サイズの検証はC++ローダーとオフラインPython検証器の両方で検証する。
- `decode_reading` により読み文字列→Trieラティス→N-bestデコードを接続。
- `StreamingSession` は入力チャンクごとに現在の読み全体を再デコードし、単調増加revision付きのprovisional snapshotを返す。最大読み長をcode point数で制限。
- `commit_best` は呼び出し側が明示した時点で上位経路を文字列化してsegment commitする。自動commit・右文脈待ち時間による確定は未実装。部分commitはsection 9の `commit_common_prefix` で追加。commit後は読みバッファを空にして次segmentを受け付ける。
- テスト追加: Pack一括ロード、辞書Trie一致、Unicode code point span、未知かなfallback、文脈データ取込み、stream append/revision、上限拒否、明示commit。
- 日本語fixtureは7語エントリのみ。実用辞書ではなく、辞書カバレッジ・実運用精度の評価には使用できない。


## 9. 形態素接続・安全な入力・C ABI統合（追加実装）
- `LatticeCandidate` にleft_id / right_idを追加。Language Packのlexicon.tsvは従来の5列/8列形式と互換を保ちつつ、9列目・10列目で接続クラスIDを受け取る。
- `ConnectionModel` を追加。connections.tsvの `previous_right_id, next_left_id, connection_cost` を読み込み、デコーダ状態が保持する直前形態素のright_idと次候補のleft_idから接続コストを加算する。辞書語彙コスト・候補固有接続コスト・遷移接続コストを形態素スコアに反映する。
- `ja-lite` に4遷移の小さな接続行列fixtureを追加。これは接続機構の動作確認用であり、Sudachi/UniDic等の実用接続行列ではない。
- UTF-8バリデータを追加。過長符号化、孤立継続バイト、不正な先行バイト、サロゲート符号化、U+10FFFF超過、切れたシーケンスを拒否する。辞書登録、読みデコード、ストリーミング追加入力で不正UTF-8を受け付けない。
- `StreamingSession::commit_common_prefix` を追加。現在保持するN-best経路すべてに共通する同一トークン接頭辞のみを確定し、確定済み読み範囲を除去して曖昧な後続読み・候補経路を保持する。共通接頭辞がない場合は確定しない。
- C ABIに `kd_engine_create/destroy`、`kd_engine_load_pack`、`kd_engine_decode` を追加。C言語ソースから実際にリンク・実行するCTestターゲットを追加。Packロード失敗・未ロード・不正入力・出力バッファ不足をエラーコードで返す。
- Pack検証器を強化。Manifest file entryの型・必須キー、重複パス、root外参照、必須辞書/n-gramファイル、宣言済みmax_expanded_bytesを検証。connections.tsvをManifest対象に追加。
- CMakeをC/C++両言語対応にし、C API統合テストをCTestへ追加。GitHub ActionsでC/C++ビルド、CTest、辞書Pack検証に加えてN-bestデコーダmicrobenchmarkを実行する。
- テスト追加: UTF-8妥当/不正/過長シーケンス、接続IDによる経路選択、共通接頭辞部分commitと未確定suffix保持、C ABIによるPackロードと「あめ」変換、出力バッファ不足、Manifest重複パス/容量制限。
- C++ Packローダーは辞書読込前にManifestとSHA-256を検証する。さらに配布前に `tools/verify_pack.py` を実行する運用とする。
- `commit_common_prefix` の確定安全性は保持中のN-best集合に対する共通性に限定される。Beamで破棄された候補まで含む数学的な全候補保証ではない。
- 実用辞書・大規模接続行列、文脈モデルの実データ校正、補正器の形態素統合、mmap、無動的確保のhot path、p50/p95/p99・peak RSSの計測は引き続き未完了。


## 10. Language Packインポート・コーパス評価基盤（今回追加）
- `tools/build_language_pack.py` を追加。5〜10列の正規化辞書TSVを標準10列へ正規化し、重複行除去・決定的ソート・int32範囲検証を行う。
- 入力列: reading / surface / lemma / POS / reading_group / candidate_id / word_cost / connection_cost / left_id / right_id。元辞書の独自形式は言語別Adapterでこの形式へ変換する契約。
- 学習用token sequence TSVからunigram(U)、前候補→現候補(L)、後候補context→現候補(R)のcountを生成。候補IDが辞書に存在すること、countがC++ Pack形式のuint32上限内であることを検証。
- Pack builderはライセンスファイル、SPDX識別子、upstream/version metadataを必須にし、ファイルSHA-256・サイズを含むManifestを生成。ネットワークからの自動取得は行わない。出力先は空ディレクトリのみ許可。
- `tools/verify_pack.py` を強化。絶対パス・親ディレクトリ参照・Windows形式区切り・symlink・重複パス・Manifest未登録payload・未hash化license・不正型・サイズ/hash不一致・上限超過を拒否。
- `tools/evaluate_corpus.cpp` と `kana_decoder_eval` を追加。reading<TAB>expected surface形式のゴールドコーパスを読み、完全一致率、Unicode code point単位の編集距離/CER、decode failureをJSON出力する。
- `tests/data/ja-smoke-gold.tsv` とtoken sequence fixtureを追加。CTestに日本語品質スモーク評価とPack builder testsを追加。
- `MaskedNgramScorer` のスコア正規化を修正。語彙数ではなくunigram総数、各文脈の出現総数を分母とするadditive smoothingに変更。n-gram count加算はuint64飽和加算とし、uint32入力値の累積wraparoundを防止。
- 学習・評価仕様を `docs/PACK_IMPORT_AND_EVALUATION.md` に記載。
- 重要な制約: 実用辞書の取り込みはまだ行っていない。外部辞書はライセンス・再配布条件を個別確認する必要がある。任意のSudachi/UniDic/MeCab形式を直接解析するものではなく、標準化TSVへの変換Adapterが必要。n-gram学習は簡易count modelであり、確率校正や高度な平滑化は未実装。
- 評価CLIのsmoke corpusは4件の配線テストであり、CER/完全一致率を製品品質の代表値として扱わない。実データ評価には、話者/文書単位で分離したtrain/dev/testコーパスと領域別集計が必要。


## 11. 今回のCI実行結果
- Commit: `375de3049dedb696db72a4221d2f077f1e5d1097`（Pack builderのconnection matrix取り込みテストを含む）
- GitHub Actions run `44344624019`: Configure / Build / CTest / benchmark / Pack verificationすべてPASS。
- CTest: 5/5。C API integration、C++ engine、Pack validation、Japanese smoke quality、Pack builder。
- Python Pack verifier tests: 7件。Pack builder tests: 2件。
- Smoke gold corpus: 4件中4件完全一致。これはfixtureの配線確認であり、製品精度指標ではない。
- CI microbenchmark: n-gram resolve 4,429.48 ns/resolve、2-token/5-edge decoder 1,672.04 ns/decode。1回の小規模測定であり、実機・実用辞書・全文処理・tail latency・RSSの保証ではない。
- 追加したstrict numeric parserはint32/uint32の範囲と文字列全体の一致を検査し、`100junk` や `10x` のような末尾不正文字を拒否する。


## 12. 日本語辞書Adapter・Correction統合・native Pack検証
- `tools/import_japanese_dictionary.py` を追加。Sudachi 17列CSV、IPADIC互換13列CSV、明示ヘッダーCSVを正規化辞書TSVへ変換する。
- 読みはカタカナからひらがなへ変換。読みが `*` / 空の項目はスキップ。surface、lemma、POS、word cost、left/right connection IDを保持。candidate IDはソースIDがなければ決定的SHA-256由来IDを生成する。
- UniDicはリリース・出力形式が複数あるため、IPADIC形式と決めつけない。監査済みの列対応を明示ヘッダーCSVで渡す。実際の第三者辞書は同梱していない。
- `tools/evaluate_corpus.cpp` は3列目のdomainラベルに対応。全体とdomain別のExact Match、CER、編集距離、decode failureをJSON出力する。旧2列形式は `unspecified` に集計。
- `decode_with_corrections` を追加。重複文字・Language Pack規則による最大8候補を生成し、候補ごとに形態素デコードしてスコア比較。CorrectionEngineの閾値を満たす候補のみ採用。数字、URL形式、protected spanは自動補正しない。
- `src/pack_manifest.cpp` を追加。外部依存なしのSHA-256検証、Pack ABI/format、license metadata、必須ファイル、payloadサイズ/hash、相対パス、symlink、未登録ファイル、30MiB runtime上限を確認する。C++ `load_language_pack` は辞書読み込み前にnative検証を実行。
- 重要な制約: native verifierは現在、builderが生成するManifestの限定スキーマを正規表現で検査する実装であり、汎用JSON parserではない。Manifest形式を拡張する場合はparserも同時に更新する必要がある。Python verifierも併用する。
- AdapterテストはSudachi/IPADIC/header CSV、読めない項目のskip、壊れた行の拒否を検証。Correction統合とPack payload改ざん検知をC++テストに追加。


## 13. Native verifier hardeningとCI最終結果
- Runtime Manifest verifierにManifest 1 MiB上限、Pack payload 64ファイル上限、Pack root symlink拒否、license upstream metadata必須を追加。
- C++ Pack loaderは、Manifest検証・payload全件SHA-256照合・サイズ検証・path containment・symlink/未登録ファイル検査を完了してからLexicon/N-gramを読み込む。
- GitHub Actions run `44346915656`（commit `bfe7c07976460de17314a66707129f9a527f8de4`）: completed / success。
- CTest: 6/6 PASS。C API integration、C++ engine（native manifest tamper検知・Correction統合含む）、Python Pack validation、Japanese smoke quality、Pack builder、Japanese dictionary adapters。
- Python Pack verifier tests 7件、Pack builder tests 2件、Japanese adapter tests 4ケースを含む。
- Smoke corpus: 4件。domainラベル付きJSON集計を確認。小規模fixtureなので精度性能を示すものではない。
- CI microbenchmark: n-gram resolve 5,422.39 ns/resolve、2-token/5-edge decoder 2,183.57 ns/decode。native verifier追加前の以前の測定値とは実行条件・ビルド内容が異なるため、直接の速度比較に使わない。
- この時点で実用Sudachi/UniDic辞書の投入は未実施。辞書Adapterは形式変換機能であり、辞書ライセンス確認、connection matrixの取得/変換、領域別の実データ精度評価は引き続き必要。


## 14. 日本語形態素属性の保持とPack correction rules
- 正規化辞書を12列へ拡張。既存10列は後方互換を維持し、11列目にinflection/conjugation type、12列目にformを追加。
- Sudachi CSVのconjugation type/form、IPADIC CSVのctype/cformを保持し、POS・活用情報をLatticeCandidateからDecodedTokenのCandidateまで伝搬する。
- IPADIC adapterのbase form列を修正。IPADICの13列配置に従い、base formはindex 10、readingはindex 11、pronunciationはindex 12。
- Pack builderに `--correction-rules` を追加。生成Manifestにcorrection_rules.tsvを含め、native loaderでUTF-8、種別、コスト、重複IDを検証。最大256ルール、from/to各256 bytes以下。
- `load_language_pack` はoptional CorrectionOptions出力を受け、Lexicon・N-gram・Connection Matrix・Correction Rulesを一時オブジェクトにロードしてから一括反映する。
- C++テストは12列normalized TSVから読み込んだPOS/活用属性がDecodedTokenまで保持されること、Packから3件のCorrection Ruleが読み込まれることを確認する。
- 最新のテスト実行はGitHub Actions run `44347934698` で6/6 PASS（形態素属性追加前）。12列辞書の最終変更後のActions run `44348011281` とREADME更新後の `44348037203` は確認中。


## 16. 最終main更新確認
- READMEとv0.7仕様書の説明整合後、GitHub Actions run `44348679722`（commit `0187a5da9d206a9ea52b2179c98596234f7c9599`）もcompleted / success。
- CTest 6/6 PASS、Pack検証PASS。
- このCI runの小規模ベンチマーク: n-gram resolve 3,776.49 ns/resolve、2-token/5-edge decoder 1,851.51 ns/decode。単一CI測定であり、性能保証や実用辞書評価ではない。


## 17. License inheritance policy and component-level Pack profiles
- Added root `LICENSE` for original kana-decoder code and original fixtures under MIT.
- Added `docs/LICENSING_POLICY.md` and `docs/SPECIFICATION_v0.8.md`. Third-party dictionaries, corpora, matrices and models are not relicensed under the repository's MIT license; each Language Pack records the conjunction of its upstream component licenses.
- Added the third-party component register. SudachiDict Small is the preferred candidate, with an expected combined expression of Apache-2.0 AND BSD-3-Clause based on the upstream LEGAL notice. SudachiDict Core/Full are excluded from the first import because their additional NEologd-derived entries reference multiple external data sources and require component-level clearance.
- Preserved the complete upstream SudachiDict LEGAL notice at `licenses/UPSTREAM_SUDACHIDICT_LEGAL.txt`.
- Added `tools/validate_license_profile.py`. It requires a pinned release identifier, exact source-artifact SHA-256, reviewed permissive SPDX ID, HTTPS upstream URL, explicit commercial-use / redistribution / modified-redistribution permissions, and bundled notice files. This validates declarations and package structure, not the legal correctness of upstream rights.
- Extended `tools/build_language_pack.py` with `--license-profile`. It copies each component license into `THIRD_PARTY_LICENSES/`, writes `license-profile.json`, records component metadata in Manifest, hashes the profile and notices, and rejects a combined SPDX expression that does not match the component licenses.
- Added tests for multi-component Apache-2.0 + BSD-3-Clause Packs, missing source checksums, floating versions, non-commercial permissions, missing redistribution permission, and Pack checksum verification.
- CI run `44351118276` for commit `717b55e44492d9348a7c0af0a17b284ec2283760`: completed / success. CTest 6/6 PASS; Python unittest 19/19 PASS; Pack manifest verification PASS.
- The 20261015 SudachiDict-small PyPI wheel is listed at 42.5 MB. This is not the eventual kana-decoder Pack size, but it exceeds the current 30 MiB lightweight target and must be measured after import/compaction.
- No third-party dictionary payload was added. The exact raw CSV source archive and its SHA-256 still need to be independently pinned and verified before the first production import. The 20261015 release candidate's source archive was not available for verified retrieval during this implementation run.
- The repository therefore now has a license-safe import/release path, but does not yet claim production dictionary coverage or production accuracy.


## 18. Sudachi V1 source import pipeline
- Added tools/import_sudachi_v1.py with a strict 18-column official V1 header check. It maps reading form, writing, lemma, POS1-6, conjugation fields, word cost, and left/right connection IDs into the normalized Pack TSV.
- Kept V0 parsing as a separate legacy format; V0 and V1 are not auto-detected or conflated.
- Added tools/import_sudachi_v1.py matrix to parse MeCab matrix.def dimensions and (right_id, left_id, cost) cells, validate bounds and duplicates, and emit sparse TSV by omitting zero-cost cells. ConnectionModel defines missing cells as zero.
- Added tools/verify_sudachi_source.py to verify source ZIP SHA-256 before extracting only the required member. It rejects path traversal, Windows drive/path forms, symlinks and oversized members.
- Added licenses/sudachidict-small-20260723-source-lock.json with published hashes for SudachiDict Small V1 small_lex.zip and the UniDic 2.1.2 matrix.def.zip. Hashes are sourced from the Lindera raw-source mirror; archives have not been downloaded or independently verified in this repository session.
- Added V1 lexicon, matrix conversion, source hash mismatch and safe extraction fixture tests.
- Added docs/SPECIFICATION_v0.9.md and updated the Pack import guide and third-party component register.
- GitHub Actions run 44351954164 for commit ec0a16de065ddc02704b929e9505ec84c838915b: completed / success. CTest 6/6 passed; Python unittest 22/22 passed; Pack verification and bounded decode benchmark passed.
- Production dictionary import remains pending: exact archive bytes must be acquired and checked against the lock, the exact release notices reviewed, then the generated Pack's installed size, load time, runtime RSS, unknown-word rate and held-out accuracy measured. The sparse unordered-map connection matrix is not yet approved for production memory budgets.


## 19. v0.10: completion-first Pack policy and data integrity gates
- User decision: the 30 MiB Pack size target is provisional during development. Completing library functionality, correctness and practical dictionary integration takes priority over size optimization.
- Removed the fixed 30 MiB ceiling from the native C++ Pack Manifest verifier. The verifier now honors the Pack-declared max_expanded_bytes budget; the default for older manifests without this field is 8 GiB. Manifest size remains capped at 1 MiB and payload file count at 64.
- Changed the Pack builder default max_expanded_bytes to 8 GiB. Explicit lower or higher budgets remain configurable. Python verify_pack now uses the Pack-declared budget by default; --max-mib is an optional additional verification ceiling.
- Added a native regression test proving that a Pack declaring 64 MiB is accepted even when its actual fixture payload is small. Added a Python regression test for budgets above 30 MiB and retained rejection of a Pack that exceeds its own declared budget.
- Updated Sudachi V1 importer header matching: case-insensitive, underscore-insensitive, required canonical columns, optional extra columns counted and ignored, duplicate normalized headers and malformed row widths rejected.
- Changed Sudachi source verification to read expected SHA-256 values and archive member names from the versioned source-lock JSON. The verifier validates lock schema, exact source IDs, HTTPS URLs, expected members and checksum format, then verifies local archive bytes before extraction and writes a machine-readable receipt. No network download is performed.
- Added tools/validate_corpus_splits.py. Train/dev/test TSV inputs require reading, expected surface, domain and group_id. Group IDs cannot cross split boundaries; empty splits, malformed rows and empty fields are rejected. JSON summary includes case counts, unique groups, domain counts and unique readings/references.
- Added tests for V1 header compatibility, extra columns, malformed row widths, source-lock-based local archive verification, matrix conversion, corpus leakage and Pack size budgets.
- Added docs/SPECIFICATION_v0.10.md and updated README / Pack import and evaluation guide.
- GitHub Actions run 44354197584 for commit 5ffd4a304345be9e38f8c54992662a86a8b784b2: completed / success. CTest 6/6 PASS; Python unittest 25/25 PASS; bounded decode microbenchmark PASS; Japanese reference Pack verification PASS.
- No real Sudachi source archive has been acquired or verified as part of this milestone. The lock-driven verifier is tested with synthetic archives only. Production dictionary creation, upstream notice audit, connection matrix memory behavior, representative held-out quality, target hardware latency and peak RSS remain pending.
- Size optimization is deferred until the full Japanese Pack and real workload are available. This policy does not waive runtime safety, checksum verification, offline operation or upstream license obligations.
