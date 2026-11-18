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


## 20. C ABI streaming sessions (v0.11)
- Added opaque kd_streaming_session API: create/destroy, append, common-prefix commit, best-path commit and reset.
- Each session owns a copy of the loaded engine's LexiconTrie, MaskedNgramScorer and ConnectionModel. Destroying or reloading the source engine does not invalidate an existing session.
- Streaming append returns the best retained path as provisional UTF-8 text and exposes the monotonic session revision.
- Append and commit operations stage state changes in a temporary session/snapshot copy. Output buffer insufficiency returns KD_ERR_BUFFER_TOO_SMALL without consuming input, committing tokens or writing the revision output.
- Common-prefix commit delegates to the C++ retained-N-best equality check; if no common token prefix exists, it returns KD_ERR_INVALID_ARGUMENT without mutation. Best-path commit is an explicit caller choice.
- C API integration tests cover chunked append, provisional output, best-path commit, empty-session commit rejection, buffer-too-small transaction behavior, common-prefix rejection under ambiguity, and session validity after destroying the source engine.
- Added docs/SPECIFICATION_v0.11.md and docs/C_API.md.
- GitHub Actions run 44354758942 for commit e84f4420abfa5e7e8a65ee62d15efe1f9491696a: completed / success. CTest 6/6 PASS; Python unittest 25/25 PASS; bounded decode microbenchmark PASS; Japanese reference Pack verification PASS.
- Remaining C ABI work: structured N-best snapshots/token spans, a positive common-prefix C API fixture, Python ctypes integration, ABI compatibility tests, and target-hardware streaming latency/RSS measurement.


## 21. v0.12: structured N-best ABI and Sudachi acquisition pipeline
- Added opaque owned kd_decode_result API: kd_engine_decode_nbest, result destruction, path count/score, token count and token view.
- Token views expose Unicode-code-point start/end spans, candidate ID, surface, POS, inflection type/form, morphology score and context score. Borrowed strings are valid only while the result object remains alive.
- Enforced the Core's hard N-best ceiling of 8; zero selects the default 8 and requests above 8 are rejected.
- Added KD_C_API_VERSION and kd_c_api_version() for FFI ABI version checks.
- Added shared kana_decoder library target while retaining the existing kana_decoder_core link target.
- Added C integration tests and Python ctypes integration tests for Pack loading, N-best paths, token metadata, code-point spans and ABI version.
- Added tools/fetch_sudachi_source.py: source-lock-driven HTTPS download, exact host/redirect allowlist, 2 GiB archive cap, SHA-256 verification and atomic publication.
- Added tools/validate_sudachi_pack_inputs.py to reject lexicon context IDs outside the pinned matrix dimensions before conversion/build.
- Added pipeline regression tests and registered them in CTest. Added docs/SPECIFICATION_v0.12.md; updated C API, Pack import, README and implementation docs.
- GitHub Actions run 44356502933 for commit 44d6cf1b55e7e95f94231a91f8c86487964979a: completed / success. CTest 9/9 PASS; Python unittest 28 tests PASS; bounded decode microbenchmark PASS; Japanese reference Pack verification PASS.
- The 30 MiB value remains a provisional optimization target, not a development or runtime hard cap. Pack manifest resource budgets remain explicit and configurable.
- No production Sudachi source archive was downloaded or imported during this milestone. The new downloader and validation pipeline are implemented and tested with synthetic fixtures, but real archive checksum confirmation, release-specific legal/NOTICE audit, full Pack generation, held-out quality evaluation, target CPU latency and peak RSS remain open release gates.
- Upstream source research confirms Small lexicon and matrix are separate inputs and that Small/Core/Full have different data composition. Core/Full are not implicitly approved for redistribution; retain the current Small-only licensing review gate.


## 22. v0.13: sparse connection matrix and reproducible Sudachi Pack build
- Replaced the string-keyed connection matrix hash map with a sorted sparse sequence using packed 64-bit (right_id, left_id) keys and int32 costs. Lookup uses binary search; zero-cost cells are omitted from the normalized representation.
- Added explicit matrix dimensions to normalized connections.tsv. The C++ loader validates dimensions, rejects malformed/out-of-range/duplicate coordinates (including duplicate zero-cost rows), and publishes the model only after complete validation.
- Updated the V1 matrix importer to emit sorted sparse rows with matrix dimensions and detect duplicate coordinates before dropping zero values.
- Expanded validate_sudachi_pack_inputs.py to scan every matrix row, validate row width, IDs, int32 cost range, duplicate cells, CSV row widths and every lexicon LEFT_ID/RIGHT_ID.
- Added tools/build_sudachi_pack.py to orchestrate archive verification, safe extraction, matrix/lexicon compatibility checks, V1 import, provenance generation, Pack build, license-profile validation and final Pack manifest verification.
- Added Pack provenance.json with source-lock contents, source archive hashes, compatibility report, import reports and importer/tool hashes. Generated Packs also include the complete upstream LEGAL notice.
- Corrected the source-license register: the release v20260723 LEGAL notice identifies the distribution as Apache-2.0 and identifies both small_lex.csv and matrix.def.zip as UniDic-derived with BSD-3-Clause conditions. Added exact Apache-2.0 and BSD-3-Clause texts and four artifact/license components. Combined data expression is Apache-2.0 AND BSD-3-Clause; original repository Core remains MIT.
- Added synthetic end-to-end Pack build coverage, matrix boundary/duplicate tests and source-profile checks. Added docs/SPECIFICATION_v0.13.md and updated Pack, licensing and README documentation.
- 30 MiB remains a provisional optimization target only. The Pack builder defaults to a declared expanded budget of 8 GiB, configurable by the caller.
- The actual upstream archives have not yet been downloaded and verified in this environment. The complete build pipeline is tested using synthetic source fixtures; no production Sudachi Pack is claimed.

## 23. v0.14: supported Python package and streaming wrapper
- Added pip-installable package python/kana_decoder with project metadata in pyproject.toml.
- KanaDecoder checks KD_C_API_VERSION, loads the shared library via explicit path, KANA_DECODER_LIBRARY or system discovery, loads a local Pack and exposes decode() and decode_nbest().
- Added immutable DecodedPath and DecodedToken values, including Unicode code-point spans and raw ranking scores.
- Added Python StreamingSession with append, common-prefix commit, best-path commit and reset. Native sessions retain independent snapshots after the originating engine is closed.
- Added bounded output-buffer growth and explicit native error translation. The Python layer delegates all decoding to the native C ABI.
- Added docs/PYTHON_API.md and docs/SPECIFICATION_v0.14.md. CTest now covers Python ctypes, the public Python wrapper, snapshot lifetime after engine close and pip installation.
- GitHub Actions run 44359119067 for commit 243386680abc2684ac806a34842de37c2ece0cc3: completed / success. CTest 12/12 PASS; Python unittest 34 tests PASS; bounded decode microbenchmark PASS; Japanese reference Pack verification PASS; Pack/license policy validation PASS.
- Remaining production gates: obtain and verify the pinned official Sudachi archives; build the real Pack; evaluate on disjoint train/dev/test corpora; measure target CPU latency and peak RSS; improve unknown-word handling and test long-running streaming stability.


## 24. Manual production Pack acquisition workflow
- Added .github/workflows/sudachi-pack.yml with workflow_dispatch. It downloads the two source archives from the pinned lock, verifies their SHA-256 values, builds the real Small Pack, runs final Pack/license verification and uploads the result as a 30-day Actions artifact.
- Added README and Pack import guide instructions for selecting Actions → Build Sudachi Small Pack → Run workflow. The expanded Pack budget defaults to 8 GiB and is bounded to 0.25–64 GiB.
- This manual workflow has been added but has not been dispatched in this session. No real dictionary archive or production Pack is claimed until the workflow completes successfully and the artifact is inspected.


## 25. v0.15 native Pack verifier resource consistency
- Fixed a schema mismatch: C++ runtime verification now reads the nested \`resource_limits.max_expanded_bytes\` field that the Pack builder and Python verifier emit. Previously, the native verifier could silently use its 8 GiB fallback instead of enforcing a smaller declared Pack budget.
- If \`resource_limits\` exists but its byte budget is missing or invalid, native verification fails closed. Legacy manifests without the optional object retain the 8 GiB default.
- Replaced whole-file SHA-256 buffering with bounded streaming hashing (64 KiB input buffer, 64-byte SHA-256 blocks), preventing a large lexicon file from being copied wholesale into temporary RAM during integrity verification.
- Increased the native payload-file ceiling from 64 to 256 while retaining the 1 MiB manifest cap.
- Added C++ conformance tests: declared 64 MiB budget is accepted, and a deliberately undersized 1-byte budget is rejected with the expected resource-limit error.
- CI validation is pending for commits \`770ba78\` and \`f730f66\`. No production Sudachi archive or Pack was downloaded or built as part of this change.

- Follow-up hardening: native resource accounting now includes both Manifest and payload bytes, matching the Python verifier's installed-size accounting.
- Added a deterministic 70,000-byte payload fixture with a pinned SHA-256 digest to exercise hashing across multiple 64 KiB reads and SHA-256 compression blocks.
- GitHub Actions run `44369302922` for the final v0.15 documentation commit `0934305727b26de5d9a5b3dde15e1b729d110a78`: completed / success. CTest 12/12 PASS, Python unittest: 34 tests discovered, 30 passed and 4 skipped because the standalone discovery invocation does not receive CTest-only native library arguments; those paths are exercised by CTest integration targets, Pack verification PASS, and bounded decode benchmark PASS.


## 26. 実Sudachiアーカイブ取得試行と診断改善
- 2026-11-18の手動Workflow実行（run IDs: \`44370290450\`, \`44370361751\`, \`44370432314\`, \`44370467031\`）で、両アーカイブのダウンロードとSHA-256照合は成功。
- 実測アーカイブサイズ: \`small_lex.zip\` 13,641,526 bytes、\`matrix.def.zip\` 128,639,959 bytes、合計142,281,485 bytes（約142.28 MB、十進）。これは圧縮された入力アーカイブの合計であり、完成Packサイズではない。
- Pack生成は全件で \`validate_sudachi_pack_inputs.py\` の段階で失敗し、Artifactはアップロードされていない。従来の \`run_json\` は subprocess の \`CalledProcessError\` をそのまま伝播し、検証器のstderrをログに表示できていなかったため、根本原因はまだ確定していない。
- \`tools/build_sudachi_pack.py\` の \`run_json\` を修正し、サブコマンド失敗時に終了コード・stdout・stderrを表示するようにした。失敗診断を隠さないための回帰テストを追加。
- 次回の手動Workflow実行で、検証失敗の具体的な行・列・寸法エラーを取得して修正する。成功Artifactがないため、辞書Packのサイズ、ロード時間、ピークRSS、変換精度は未測定。

- Diagnostic regression-test CI: run \`44370723405\` completed successfully for commit \`477c1f17ff80608fdd0d2510ba2c8246c0d91e5e\`; CTest 12/12 PASS, Python unittest 35 discovered / 31 passed / 4 skipped, including the new captured-stderr test. Later documentation commits are still undergoing CI at report time.


## 27. v0.16: 大規模辞書インポートのメモリ上限対策
- 実データのCIログを再調査し、2026-11-18のSudachi Pack生成が辞書CSVヘッダー検査で停止していたことを特定。診断改善後のrun 44371482922で duplicate lexicon header after normalization が確認された。重複している実ヘッダー名そのものは、このrunのログには含まれていないため未確定。次回実行ではraw headerと正規化後の重複名を例外に含めて採取する。
- validate_sudachi_pack_inputs.py のmatrix cellキー全件リスト・ソートを廃止し、一時SQLite DBのPRIMARY KEYで重複を検出する方式へ変更。ゼロコストセルも重複判定対象。
- import_sudachi_v1.py の語彙全件list/set保持を廃止。SQLiteの一意制約で重複排除し、SQL ORDER BYで決定的な順序でTSVへストリーム出力する。
- matrix importerも全セルキー・非ゼロ行のlist保持を廃止し、一時SQLiteへ逐次格納。重複検出後、packed cell key順で非ゼロ行のみをストリーム出力する。
- SQLiteは一時ディレクトリ内に作成し、終了時に削除する。アプリケーション側の行配列保持をなくし、SQLite cacheを8MiBに制限。ディスク空き容量は入力規模に応じて必要。
- 追加した境界テスト: 5,000行のvalidator入力、ゼロコスト重複、matrixの順序正規化・重複拒否、語彙の重複排除と決定的出力。
- 初回CIではテストfixtureのmatrix座標を重複生成していたことと、テストが古い戻り値キーを参照していたことを検出。fixtureと診断を修正し、最新CIで再検証する。
- 実Sudachiアーカイブは引き続きダウンロード・SHA-256照合に成功。Pack Artifactはまだ生成されていない。raw header診断を含む最新コードで手動Workflowを再実行し、実ヘッダー互換性を確定させる必要がある。

- GitHub Actions run 44371939507 for v0.16 commit 84755b08b01b9df56df0c62edfd6a9096450d6f5: completed / success. CTest 12/12 PASS; Python unittest 39 tests discovered, 35 passed and 4 skipped because standalone discovery lacks CTest-only native arguments; the skipped native paths are exercised by CTest integration. Bounded decode benchmark, reference Pack verification and license policy validation also PASS.


## 28. 実Sudachi Pack失敗原因の確定と修正
- 2026-11-18の実辞書Workflow run 44372231418（commit 657947d94a2ec6e19605d0a9d49b130f19e2f6fe）を調査。アーカイブ取得・SHA-256検証は成功したが、辞書検証で停止しPack Artifact uploadはskip。
- エラーのraw headerは \`[' ', '5967', '5967', '10', ' ', '空白', '*', '*', '*', '*', '*', 'キゴウ', ' ', '*', 'A', '*', '*', '*', '*']\`。これはヘッダーではなく辞書の第1データ行である。
- 根本原因: source lockがSudachiDict V1と宣言していた一方、取得URLは \`sudachidict-raw/20260723/small_lex.zip\` で、V0 raw CSVを指していた。SudachiDictはV1 raw sourceを \`sudachidict-raw/v1/20260723/\` に分けて配布する。V0/V1は同じファイル名でも内容が異なる。
- V1 small archiveの公開SHA-256として \`e49936daef64043657752eb9f4ada912cf0316e26dd158a6200c770e2f93e706\` を採用し、source lockとlicense profileのURL/hashをV1 endpointへ変更。fetcherのURL規約も \`/v1/{release}/\` に固定。
- ImporterをV1ヘッダー形式に対応。Index_Form/Headwordをsurface/writingへ正規化し、V1の追加メタデータ列（Synonym_Groups、Split_C、User_Data、POS_Id、Reference_Id）を許容。V0 headerless CSVはV1 importerで誤読せず、明示的に拒否する。
- 23列V1ヘッダーfixtureとheaderless V0拒否fixtureを追加。実アーカイブの新SHA-256照合と実データPack生成は、修正後Workflowで再確認するまで未成功扱い。

- Added v0.17 regression gates for the official 23-column V1 named header, V0 headerless rejection, and exact pinned V1 source URL/hash. The Sudachi Pack workflow now also runs on pushes that change the pinned source lock, source importer/fetcher/validator, or this workflow definition, so the corrected source pipeline is retried from the new main commit without a manual dispatch.


## 29. Sudachi V1 composition-only context IDs
- Workflow run 44373076130 successfully downloaded the corrected V1 lexicon archive (14,641,540 bytes) and verified SHA-256 \`e49936daef64043657752eb9f4ada912cf0316e26dd158a6200c770e2f93e706\`. The matrix archive also passed its pinned checksum.
- The build advanced beyond the previous header error and stopped at lexicon line 12 because a context ID was \`-1\`.
- Sudachi V1 documentation states that entries used only as components of split words may have Left_Id or Right_Id set to -1; such entries do not appear independently as analysis results. The importer now skips these rows as standalone candidates, while validation counts and reports them. Values below -1 and out-of-dimension positive IDs remain errors.
- Added regression tests for validator acceptance, importer filtering and skip counts. The first test revision contained literal escaped newlines in fixture data; corrected in commit 62399cc20a0d2415c61824b0ef8bddb4e0d15b6f before final CI verification.
- The V1 Pack Workflow has not yet succeeded. A new push-triggered run will verify the sentinel handling against the complete source.


## 30. 初回Sudachi V1 Small Pack生成成功
- GitHub Actions run 44373492197（commit f03c4048a60afb59f9d47c83cb9cdfbe2be10362）: completed / success。Sudachi V1 source取得、SHA-256照合、全件入力検証、正規化import、Pack生成、Manifest/hash/容量/ライセンス検証、Artifact uploadまで成功。
- Artifact: \`kana-decoder-ja-jp-sudachi-small-20260723\`, ID \`14459966853\`, ZIP 149,644,217 bytes, SHA-256 \`de7512aa0c387e3ca44921794d9e64dfbcf218574e1326941462d88b7ecc7341\`. Retention expires 2026-12-18.
- Source archives: V1 small lexicon 14,641,540 bytes / SHA-256 \`e49936daef64043657752eb9f4ada912cf0316e26dd158a6200c770e2f93e706\`; matrix 128,639,959 bytes / SHA-256 \`3ccaed582481b31be170c7200cf1cb2856f2d14c2fa601b3692d97dbad5a3bd1\`.
- Source validation: lexicon rows 765,649; composition-only rows with -1 context IDs 10,213; matrix 5,981 x 5,981, 35,772,361 source cells, 35,759,450 nonzero.
- Lexicon importer: 754,603 entries before normalized Pack builder duplicate elimination; 10,215 skipped, of which 10,213 were composition-only. Final Pack builder reports 754,602 entries.
- Pack: payload 693,537,484 bytes; manifest 4,518 bytes; installed total 693,542,002 bytes = 661.413 MiB. connections.tsv 514,338,787 bytes; lexicon.tsv 179,160,409 bytes; ngram.tsv 45 bytes (no training corpus supplied, so no learned n-gram records).
- Independent artifact ZIP download was materialized and inspected. Its SHA-256 matches the GitHub artifact digest. Manifest identifies pack \`ja-jp-sudachi-small\`, version \`20260723.1\`, license expression \`Apache-2.0 AND BSD-3-Clause\`.
- CI run 44373791625 for commit b84de582766d1c184466404cc6394760001e4037: completed / success; CTest 12/12 PASS; Python unittest 45 discovered, 41 passed, 4 skipped (CTest-only native argument cases).
- This proves Pack generation and static Pack verification, not yet runtime usability or quality. Native C++ runtime load, peak RSS, load time, decode smoke metrics and held-out corpus accuracy remain to be measured. The Sudachi Pack workflow now includes a native runtime build/evaluation step before artifact upload.


## 31. Native runtime loading of the production Pack
- Workflow run 44374465962 (commit 0f72987fa8b1565d1e369223dcc203cf4767d416): completed / success. After rebuilding and statically verifying the Pack, the workflow built the native C++ evaluator and loaded the complete 661.4 MiB Pack including the 35,759,450-cell sparse connection matrix.
- Combined native Pack load + four-case smoke evaluation elapsed time: 8.00 seconds on the GitHub Actions runner.
- Peak resident set size: 1,425,240 KiB (approximately 1.36 GiB). This is a single CI runner measurement, not a target-device guarantee.
- Smoke evaluation: 4 cases, exact matches 1/4 (25%), edit distance 5 over 5 reference codepoints (CER 1.0), decode failures 0. Domain general: 1/3 exact; unknown: 0/1 exact.
- This shows that the native loader accepts the production Pack and can produce a path for all four cases. It also shows that the current four-case corpus is not passed at an acceptable quality level. The corpus is too small to estimate general accuracy, and no n-gram training corpus was supplied (ngram.tsv has zero learned records).
- Added \`--details\` support to \`kana_decoder_eval\` to emit per-case expected/predicted strings and Pack load milliseconds. The next Pack workflow will use this mode so each smoke error can be diagnosed directly. The new detailed run is pending.
- Latest successful production Pack artifact from run 44374465962: ID 14459349705, ZIP 149,644,215 bytes, SHA-256 \`440169a5dc4854f41a831a24f4fbc7d2093b60fa728a55f7fce87e4f7c1d0583\`.


## 32. 評価CLIの責務分離と回帰テスト追加
- `tools/evaluate_corpus.cpp` をリファクタ。TSV行解析、UTF-8検証、Unicode code point分割、Levenshtein距離、1ケース評価、統計集計、JSON出力を個別関数・構造体へ分離。
- 既存のTSV互換性（reading / expected / optional domain）を維持。domain省略時は `unspecified`。CRLFの末尾CRはdomainから除去し、不正行は行番号を付けて終了コード2で拒否。
- JSON制御文字を `\\b` / `\\f` 等の標準escapeまたは `\\u00XX` で保持するよう修正。従来の制御文字を `?` に置換する情報損失を解消。
- 通常モードと `--details` モードで同じ集計関数を使用。詳細モードは個別 reading / expected / predicted / domain / edit_distance / decode_failure を追加出力。
- `tests/test_evaluator_cli.py` を追加。両モードのJSON parse、集計値の一致、詳細件数、CRLF処理、不正行・空コーパスのfail-closed動作を検証。CTestに `evaluator_cli_contract_tests` を登録。
- 仕様書 `docs/SPECIFICATION_v0.21.md` を追加。CI runは実行中のため、PASS確認後に検証結果を追記する。


## 33. 実Sudachi Packのケース別誤変換診断
- Detailed workflow run 44375182255: completed / success。Pack build、native runtime evaluation、Artifact uploadまで成功。Artifact ID 14460393351、ZIP 149,644,215 bytes、SHA-256 `3f4bb36d7f7ef12a9742fd99996eb3cfaf7b1ec5876c952307a7ce08fed8ed9e`。
- Pack load: 13,525 ms。4件すべてdecode pathを生成（decode failure 0）。Peak RSS: 1,425,388 KiB。評価CLIのload_msと `/usr/bin/time` の全プロセスelapsedは計測区間が異なるため、同一指標として比較しない。
- ケース別結果:
  - `あめ` → 期待 `雨` / 実際 `アメ`（編集距離2）。辞書候補は存在して経路も生成されるが、現在のスコアでは表記候補の選択を誤る。
  - `はし` → `橋` / `橋`（完全一致）。
  - `きしゃ` → `汽車` / `記者`（編集距離2）。同音候補を文脈で解消する学習済みn-gramが存在しないため、候補選択が不適切。
  - `ぴ` → `ぴ` / `日`（編集距離1）。未知語fallbackが発火せず、辞書側の候補が選ばれている可能性がある。候補生成と未知語優先順位の追加トレースが必要。
- 集計は従来どおり4件中1件完全一致、CER 1.0。これは4件の診断用smoke corpusに限る結果であり、一般精度の推定値ではない。
- 重要な判断: 現時点でconnection matrix全体の向きやimporter mappingの不具合と断定できる証拠はない。まず候補一覧・各候補のword cost / connection cost / transition cost / context scoreを可視化し、未知語fallbackの生成条件も追跡する。その後、学習データを用いた同音語選択の評価へ進む。

- Refactor CI verification: run 44376327575 for commit 25ec03e2870d527d41f38c62a60d3cc65e38fd07 completed / success. CTest 13/13 PASS, including the new evaluator CLI contract test; build, bounded decode microbenchmark, reference Pack verification and Python Pack/license tests also passed. Subsequent documentation-only commits have their own CI runs in progress.



## 34. デコーダ責務分割・候補スコア診断（v0.22）
- `MorphologyDecoder::decode` を `src/engine.cpp` から `src/morphology_decoder.cpp` へ抽出。ビーム幅・edge/candidate/path上限、候補評価式、経路順位は変更せず、翻訳単位の責務分割として実施。
- `CandidateScoreTrace` を追加し、読み範囲、候補ID/表層、word cost、候補固有connection cost、遷移connection cost、raw context score、context weight、morphology score、weighted context score、local score、unknown fallback識別を記録。
- `decode_reading` / `MorphologyDecoder::decode` に省略可能なtrace出力引数を追加。既存呼び出しはデフォルト引数により互換性を維持。1 decodeあたり最大4,096候補展開まで記録し、診断メモリを制限。
- `kana_decoder_eval --trace` を追加。ケース別JSONへcandidate_traceを追加し、通常の集計値は変更しない。CLI回帰テストでJSON解析、集計同一性、fallback flag、score decompositionを検証。
- `tests/test_decoder_trace.cpp` を追加。word cost、candidate connection cost、transition connection cost、context scoreの各項目と合計式を検証。
- 既知の観察点: 現行Trieは各読み位置に辞書一致がある場合、その位置のかなfallback edgeを生成しない。従って「ぴ→日」のケースではfallback候補が候補集合に存在しない可能性を、実データtraceで直接確認できる。今回の変更では候補選択規則自体は変更していない。
- 仕様書 `docs/SPECIFICATION_v0.22.md` を追加。
- GitHub Actions run `44377822634`（code commit `713eb22ec4712a43cccac47909b2ab1106d6c042`）: completed / success。Configure / Build / CTest / bounded decode benchmark / Pack verification / Python Pack・license testsすべてPASS。CTest 14/14 PASS。新規 `decoder_trace_tests` と拡張 `evaluator_cli_contract_tests` を含む。


## 35. エンジン責務の一括分割（v0.23）
- `src/engine.cpp` から以下の責務を独立翻訳単位へ抽出: `src/lexicon_trie.cpp`（Trie / TSV / Lattice）、`src/scoring.cpp`（ConnectionModel / MaskedNgramScorer / ambiguity）、`src/correction.cpp`（補正ルール読込 / 候補生成 / 適用）、`src/streaming_session.cpp`（append / commit / reset）、`src/pack_loader.cpp`（Manifest検証後のPackロード）。
- 既存の公開宣言は `include/kana_decoder/engine.hpp` に維持し、C++利用側のAPIとC ABIを変更しない構成。
- Static coreとshared runtimeの双方へ同じモジュール群を登録。Python ctypesが使用する共有ライブラリも新しい実装単位で構成。
- Pack loaderは引き続き一時オブジェクトに読み込み、Manifest・辞書・ngram・任意のconnection/correctionファイル検証後に一括反映するtransactional semanticsを維持。
- CI初回で分割ファイルに移した標準ライブラリ依存の不足を検出し、`<charconv>` / `<fstream>` を追加。StreamingのUTF-8検証は共通 `is_valid_utf8` APIへ統一。
- GitHub Actions run `44379310687`（最終コードcommit `f8ccb004b5f4fc9e8554dfc2eaa3a206f2867030`）: completed / success。Static coreとshared runtimeの双方をBuild成功。CTest 14/14 PASS、bounded decode benchmark PASS、Japanese reference Pack verification PASS、Python Pack/license tests 45件発見・41 PASS・4 SKIP。分割後に不要となったUTF-8補助・数値パーサ・重複したCorrection loaderも除去。


## 36. 警告ゼロ化と診断ゲート強化（v0.24）
- 前回の成功CI（run 44379477637）のBuildログを調査し、static/sharedの二重ビルドで繰り返し出ていた警告を分類。ソース上の警告箇所は `-Wmisleading-indentation` と `-Wmissing-field-initializers` の2種類。
- `engine.cpp`、`scoring.cpp`、`lexicon_trie.cpp`、`correction.cpp`、`streaming_session.cpp`、`pack_loader.cpp`、`pack_manifest.cpp`、`c_api.cpp` を修正。条件分岐・ループ後の処理を明示的に分離し、fallback/C API convenience candidateの未指定メタデータを空文字で明示初期化。
- これらは警告の原因となる記述を整理したもので、スコア式・候補順位・fallback発火条件・補正閾値・探索上限・Pack形式・公開APIは変更していない。
- CMakeのstatic/shared両ターゲットに `-Werror` を追加し、今後の警告再発をCIビルド段階で失敗として検出する。
- 既存の `decoder_trace_tests` と `evaluator_cli_contract_tests` を回帰ゲートとして維持。`--trace` 有無で全体/domain集計が一致すること、traceのscore decompositionが一致することを引き続き検証する。
- GitHub Actions run `44380200959`（commit `4f9aa8c2076626dad075555e161fcae76d26093d`）: completed / success。Configure / Build（`-Werror`有効）/ CTest / bounded decode benchmark / Japanese reference Pack verification / Python Pack・license testsすべて成功。Buildログのwarning/errorは0件。
- CTest: 14/14 PASS。Python unittest: 45件実行、41 PASS・4 SKIP。static coreとshared runtimeの双方がwarning-as-error条件でビルド成功。


## 37. 候補生成・fallback抑止の可視化（v0.25）
- `LatticePositionTrace` / `LatticeMatchTrace` を追加し、読みの各Unicode code point開始位置について、辞書一致数、一致span、保持候補ID/表層、fallback有効状態、fallback生成有無、辞書一致によるfallback抑止有無を記録可能にした。
- `LexiconTrie::build_lattice` と `decode_reading` に省略可能な診断出力引数を追加。既存呼び出しはデフォルト引数により互換性を維持。traceを要求しない通常処理ではtraceレコードを生成しない。
- 評価CLIへ `--lattice-trace` を追加。`--trace --lattice-trace` で候補ごとのスコア分解と候補生成状況を同じcase JSONへ出力する。診断有無でdecode結果・overall/domain集計は変化しない。
- C++回帰テストで、辞書一致がある位置ではfallbackが抑止され、辞書一致がない位置ではfallbackが生成されることを検証。CLI回帰テストでJSON構造、読みcode pointごとの記録数、fallback状態の整合性、集計値不変を検証。
- Sudachi実Pack workflowを更新し、smoke corpusに対してscore traceとlattice traceを同時出力。関連ヘッダー・生成器・decoder変更でもPack workflowが起動するようpaths filterを拡張。
- この版ではfallback生成方針・候補順位・スコア式を変更していない。実Pack traceから「候補不在」「fallback抑止」「候補順位誤り」を区別した後、別途ゴールドコーパスで候補生成方針の変更を評価する。
- GitHub Actions run `44381437986`（code commit `a35dec7a165dd51fe1de1542827398235dd15073`）: completed / success。`-Werror`有効のConfigure / Build、CTest 14/14、bounded decode benchmark、Japanese reference Pack verification、Python Pack/license tests（45件、41 PASS・4 SKIP）がすべて成功。Buildログのwarning/errorは0件。
- 実Sudachi Packを再生成して `--trace --lattice-trace` を実行するworkflow run `44381438082` は、Pack生成・検証ステップまで進行中。実Packの候補内容とfallback状態の分析結果は、その評価ステップ完了後に追記する。


## 38. 実行時メモリガードレールと簡易SURGE評価（v0.26）
- `DecoderOptions` に入力code point 256、lattice edge 4,096、保持candidate 32,768、形態素展開状態262,144、trace record 4,096の上限を追加。呼出側が上限値を大きく指定してもCore側で絶対上限へclampし、ガードレールを無効化できない。既存のbeam 8 / position edge 16 / edge candidate 8 / output path 8の上限と合わせ、デコード時の探索量・一時構造を決定的に制限する。
- `LexiconTrie::build_lattice` は候補を全件コピーしてから切り詰める方式を廃止し、上位8候補のみをstable bounded insertionで保持。従来の同点順序を維持しつつedge/candidate総量を上限管理する。
- `decode_reading` は入力・lattice上限を超えると空結果でfail-closed。MorphologyDecoderも展開上限に達すると処理を中断し、空結果とresource-limit flagを返す。途中までの不完全なbest pathを正常結果として返さない。
- Sudachi実Pack評価プロセスにLinux RLIMIT_DATA 3,072 MiBを設定し、GNU timeのPeak RSSは別途1,792 MiB以下とするcheckerを追加。初回にRLIMIT_DATAも1,792 MiBとしたところPack load中にstd::bad_allocとなったため、allocatorの仮想領域オーバーヘッドを考慮してデータ領域上限を3,072 MiBへ調整。RSS判定上限は1,792 MiBのまま維持。Pack常駐分を含む総プロセスRSSと、Coreのデコード作業量上限を分離して管理する。
- `tools/evaluate_surge.py` を追加。読みspanごとの候補をClaim clusterとして集約し、異なる表層候補を同一spanの未解決Questionとして記録。候補スコア差が小さいQuestionへFocusを割り当てる。gold surfaceは候補coverageのオフライン評価にのみ使用し、予測選択や自動解決には使用しない。
- 実Sudachi Packの既存trace（run 44381438082）を再解析: 4件中gold候補は4件すべて候補集合に存在（candidate coverage 4/4）する一方、top-1完全一致は1/4。raw context scoreは全件0で、Packに学習済みn-gram記録がないことと整合。簡易SURGEは曖昧候補を保持し質問化する点では有効だが、外部文脈・学習済み証拠なしに正解候補を決める能力は確認できない。
- 注意: 1,792 MiBはGitHub Actionsでの実行上限であり、端末全般でのRSS保証ではない。ターゲットCPU/OS/Packごとの実測が必要。4件smoke corpusも一般精度を示すものではない。
- 新規テスト: decoder resource ceilings / fail-closed / trace cap、SURGE question/focus/coverage、RSS log parser。GitHub Actions run `44385030265`（main HEAD `80387892`）でwarning-as-error build成功、CTest 16/16 PASS、Python unittest 49件PASS。実Sudachi Packのメモリ上限・SURGE probe workflow run `44385008956` はcompleted/success。Peak RSS 1,425,500 KiBで1,792 MiB gateを通過。4件smokeは完全一致1/4、CER 1.0、decode failure 0。SURGE probeは13 span cluster / 84 claims / 12 contradiction questions / 4 focus cases / gold candidate coverage 4/4。

## 39. 文脈証拠の分離とSURGE Question解決ゲート（v0.27）
- `MaskedNgramScorer::contextual_score` を追加し、隣接形態素のleft/right masked n-gram寄与をunigram priorから分離。既存の`score`はunigram + contextual_scoreの和とし、スコア式と候補順位を維持。
- `CandidateScoreTrace` / evaluator JSONに`context_evidence_score`を追加。従来の`raw_context_score`は互換のため維持し、診断上は総スコアと隣接文脈証拠を混同しない。
- 簡易SURGE probeは候補surfaceをClaim cluster、競合候補をQuestionとして扱う。Morphology/word costやunigram priorの差だけではQuestionを解決しない。少なくとも2候補に差別化された非ゼロの隣接文脈証拠があり、context score marginが既定0.5以上の場合のみ、probe上で解決扱いにする。gold labelはcoverage計測だけに使う。
- 各候補表層のcontext evidenceは、同一候補のbest local-path observationに付随する値を保持し、異なるbeam履歴から別々に最大値を寄せ集めて比較しない。
- 文脈証拠がないQuestionは未解決のままFocusへ送る。Focusは未解決Questionのうちcontext marginが最小のものを優先。decoder出力は変更しない。
- MorphologyDecoderの直接APIにも入力長256、lattice edge 4,096、総candidate 32,768、expanded state 262,144の絶対上限を適用。0 budgetや上限超過はresource-limit flag付きでfail-closed。外部latticeのedge/candidateを無制限にコピーせず、上位8候補のpointerだけをbounded insertionで保持。decode_reading経由でなくても上限を迂回できない。
- C++テストでunigram/contextの分離、context score差、direct decoder zero-budget fail-closedを検証。Pythonテストで大きなmorphology marginだけでは解決しないこと、文脈証拠のmarginでQuestionが解決可能になることを検証。
- v0.26実Pack run `44385008956`: completed/success。Peak RSS 1,425,500 KiB（1,792 MiB gate内）。smoke 4件は1/4 exact、CER 1.0、decode failure 0。SURGE baselineは13 clusters / 84 claims / 12 questions / 4 focus cases / candidate coverage 4/4。Packのneighbor-context evidenceは空で、解決件数0。
- v0.27 code CI run `44388451549`: completed/success。warning-as-error build成功、CTest 16/16 PASS、Python unittest 50件PASS。実Pack再検証run `44388413830` はPack生成・実行評価中で、v0.27のPeak RSSとcontext evidence結果はまだ未確定。

## 40. 文脈コーパス準備と候補アブレーション（v0.28）
- tools/prepare_context_corpus.py を追加。文書ID・文ID・reading・surface・domainを含む形態素トークンTSVを、Pack builderが受け取るcandidate-ID sequenceへ変換する。辞書との対応は完全一致し、同一reading/surfaceに複数candidate IDがある場合はword cost最小、次いでID順で決定する。
- train/dev/testを文書単位で分割。明示splitがあれば文書間の混在を拒否し、split指定がなければSHA-256による決定的順序で概ね80/10/10へ分割する。trainは --sequences 用ID列、dev/testはreading/expected/domainの評価TSVを生成する。辞書未登録tokenは行番号付きでfail-closedとし、黙って除外しない。
- tools/evaluate_ablation.py を追加。candidate traceを用い、morphology-only、combined local score、SURGE context-evidence gated resolutionを比較する。SURGEは2候補以上の非ゼロかつ差別化された文脈証拠と閾値以上のmarginがある場合だけ解決し、それ以外は棄却する。
- sentence-level goldとの誤ったtoken alignmentを避けるため、アブレーション精度は入力reading全体を覆うsingle-span候補のみを対象とする。複数span/複数形態素ケースは評価対象外として明示する。各候補surfaceのスコア要素は同じbest-local observationから取得し、異なるbeam履歴を混ぜない。
- CTestにcorpus compiler / ablation testsを追加。回帰テストは文書split漏洩防止、明示split衝突拒否、辞書未登録token拒否、context evidenceが形態素順位と異なる候補を解決する合成例、証拠なし棄却、multi-span誤alignment防止を検証する。
- Sudachi実Pack v0.27 run 44388413830: completed / success。754,602辞書entries、trained n-gram records 0、installed size 661.413 MiB、pack_load_ms 10,302、Peak RSS 1,425,444 KiB（1,792 MiB gate内）。4件smokeは1/4 exact、CER 1.0、decode failure 0。SURGEは13 span clusters / 84 claims / 12 Questions / 0 context-evidence spans / 0 resolved / 12 unresolved / 4 Focus / candidate coverage 4/4。学習コーパスがないため、文脈による実データ改善は未検証。
- 重要な境界: v0.28は学習コーパスを安全に準備・分割・評価する道具を追加したものであり、トークナイザや外部コーパスを同梱しない。精度改善の確認には、ライセンス確認済みの文書単位で分離された形態素tokenized corpusが必要。


- v0.28 CI run 44391162031（main commit 392cadd270977b6c3b7f7a3b8f0959f7f1600bd3）: completed / success。Configure / Build（-Werror）/ CTest / bounded decode benchmark / Japanese reference Pack verification / Python Pack and license tests all passed。CTest 18/18 PASS。Python unittest 56 tests: 52 PASS, 4 SKIP。新規corpus preparation 3 testsとablation 3 testsを含む。
- 実Sudachi Packへのv0.28 ablation追加検証run 44391164599は、ソース取得・サイズ検査・Pack生成の進行中。実Packの追加ablation出力はrun完了後に確認する。v0.27実Packの基準値は上記のとおりで、v0.28で精度向上したとはまだ判定しない。


## 41. UD Japanese-GSD実コーパスと文脈アブレーション（v0.29）
- UniversalDependencies/UD_Japanese-GSDをcommit c662dc96ed5ee5ac0fc8b5e44261557e431f50e6へ固定し、CI時のみtrain/dev/test CoNLL-UとLICENSE.txtを取得する。データはCC BY-SA 4.0の注記を持ち、上流は注釈と元文章の権利を区別しているため、評価専用の一時データとして扱い、データ・学習済みngram・訓練Packをコミット/Artifact公開しない。
- tools/prepare_ud_gsd_corpus.pyを追加。大文字小文字を区別せずMISC.UnidicInfoの実データ第5フィールド（token-form reading）を優先して読みを取得し、kana判定付きfallback後にひらがな化し、Sudachi正規化辞書のreading/surface完全一致で語彙カバレッジを確認する。未知語・読み欠損で連続区間を分断し、未知語をまたぐ誤った隣接bigramを作らない。splitは上流train/dev/testを維持し、source commitとファイルSHA-256、coverage統計をmanifestに記録する。
- tools/evaluate_context_ablation.pyを追加。train_sequences.tsvからunigram / 左右bigramを再集計し、src/scoring.cppと同じalpha=0.5の平滑化式を用いる。形態素のみ、unigram+context、context evidence gated SURGEの3方式をtest token単位で比較する。候補surfaceは最大8候補に制限し、同一surfaceの複数形態素IDは方式別best scoreで集約する。
- morphology scoreはword costとcandidate-local connection costを反映。token-local ablationのためfull connection matrix transition costは除外し、full-path decoder testと評価境界を分離する。SURGEは少なくとも2つの候補surfaceに実観測されたcontext countがあり、supported候補間marginが0.5以上のときのみ解決し、それ以外はabstainする。
- 重要な評価境界: test tokenの正解ラベルは精度計測にのみ使用するが、左右neighbor IDはgoldをteacher-forceする。したがって文脈スコアの寄与を切り分けるoracle-context upper boundであり、全文デコードCERや本番時のSURGE効果を示すものではない。
- CIはcandidate-covered test token 1,000件未満を失敗とし、coverage / morphology accuracy / combined accuracy / SURGE resolution precision・recall / abstentionを出力する。コーパス本体・train sequences・derived modelはArtifactとしてアップロードしない。
- C++/Pack CI run 44393631002（commit e734e3893757212598b63bf40a362777a6115d4c）: completed / success。Warning-as-error build、CTest 20/20、Python unittest 61件（57 PASS / 4 SKIP）、bounded decode benchmark、Japanese reference Pack verificationがPASS。
- 実コーパス計測 workflow run 44393633264 は実行中。GSDの実データcoverageと3方式の測定値は完了後に追記し、現時点では精度向上を未主張。


## 42. 逐次予測履歴を用いた文脈評価リファクタ（v0.30）

- v0.29の実コーパスworkflow run `44394039136` がcompleted / success。UD Japanese-GSD test 10,317 token中、候補coverage 9,774（94.74%）。morphology-only Top-1 45.2936%、gold左右文脈を与えたcombinedは45.4164%（差+0.1228ポイント）。oracle SURGEは875件を解決、8,899件を棄却、解決適合率90.8571%、covered tokenに対する正解解決recall 8.1338%。この数値はgold左右neighborを使うoracle評価であり、逐次予測精度ではない。
- この結果から、gold future contextを使わない逐次評価を追加。tools/evaluate_context_ablation.pyを責務分割し、辞書読込・train count集計・oracle評価・causal sequential評価を独立関数化。出力JSONも `oracle_ablation` と `causal_sequential` に分離し、評価境界を明示する。
- Causal評価ではgold token境界・readingのみを入力アラインメントとして使用し、各policyが独立した予測candidate ID履歴を維持する。morphology-only、morphology+unigram+left context、SURGE context-evidence gateの3方式を比較。gold neighbor IDとfuture contextは参照しない。
- SURGEは2つ以上の異なる候補surfaceに非ゼロの左文脈証拠があり、best-vs-second marginが閾値以上の場合のみ解決。その他はmorphology top-1へfallbackし、選択結果を当該policy自身の履歴へ追加する。別policyの予測履歴を共有しない。
- 追加テスト: oracle/causal出力の分離、予測履歴での文脈改善、SURGE解決・fallback、証拠不足時の棄却、候補上限・candidate miss、非有限margin拒否。
- 評価上の制約: gold token境界は維持するため、全文かな入力からの自動形態素分割やend-to-end CERの検証ではない。C++ runtimeのスコアリング/API/Pack schema/メモリ上限は変更しない。
- 次の検証はcausal結果の実測と、C++ decoderの実際の逐次経路との一致確認。causal評価が悪化する場合、context weightを増やすのではなく、予測履歴誤りの伝播とSURGE fallback条件を先に分析する。


### v0.30 実コーパス逐次評価の実測・CI結果追記

- 実Sudachi Pack workflow run `44405759595`: completed / success。oracle/causal両評価を完了し、native Packのメモリ検査・artifact uploadもPASS。
- 同じtest 10,317 token、candidate coverage 9,774 / 10,317（94.7368%）で比較。causal morphology-only 45.2936%（4,427 correct）、causal morphology+unigram+predicted-left context 45.3857%（4,436 correct、+0.0921ポイント）。
- Causal SURGE-gated（棄却時はmorphology fallback）は46.2861%（4,524 correct、morphology-only比+0.9925ポイント）。151件をSURGE解決、10,166件を棄却。解決適合率85.4305%、covered tokenに対する正解解決recall 1.3198%。全体精度はfallbackを含むため、SURGE単独で46.29%を解決した意味ではない。
- Oracle trackはv0.29と同じ値: morphology 45.2936%、combined 45.4164%、SURGE 875 resolved / 8,899 abstained / precision 90.8571%。Oracleとcausalの差は未来情報・正解neighbor IDを使えるかどうかを示す。
- Native runtime smoke: 4件中1件完全一致、CER 1.0、decode failure 0。Peak RSS 1,425,528 KiB（1,392.1 MiB）、1,792 MiB gate内。Pack load 7,610 ms。これはGitHub runner上でのSudachi実Pack基準値であり、製品精度や対象PCの性能保証ではない。
- Pack artifact: 149,644,215 bytes、基礎Sudachi Packのみをアップロード。コーパス・train sequence・派生ngram modelは含めない。
- Code CI run `44405760611`（commit `b3b32aacc2c0b5d12610334d6d8af1ba52bf0201`）: completed / success。warning-as-error build、CTest 20/20、Python unittest 62件（58 PASS / 4 SKIP）、bounded benchmark、Japanese reference Pack verificationがPASS。
- 結論: causal sequential条件でもSURGE gate + morphology fallbackはこのGSD token-aligned benchmark上で約0.99ポイントの改善を示した。ただしtoken境界はgoldで与えているため、次段階ではC++ runtimeの実際のstreaming pathへ同じ予測履歴制約を適用し、full reading-to-textのCERと誤り伝播を検証する。


## 43. C++ Coreによるgold境界非依存の全文逐次評価（v0.30追加）

- `tools/evaluate_sequential_corpus.cpp` を追加。test文書のreading列を文単位で連結し、gold token境界をデコーダ入力へ渡さずにC++ Coreの `decode_reading` で全文変換する。
- morphology-onlyは空のscorer、context方式はtrain splitからC++ `MaskedNgramScorer` にunigramとleft bigramを直接集計して比較。decoderが自ら選んだcandidate IDを次候補のleft historyとして使い、right/future contextを与えない。
- gold token境界はdecode後のalignment計測にだけ利用し、sentence exact / CER / decode failure / over-limit / token alignment precision・recallを出力する。入力上限256 codepoints超の文は部分decodeせずover-limitとして明示。
- CMakeに `kana_decoder_sequential_eval` とCTest統合テストを追加。合成2-token文「あめはし」で、morphology-onlyの「雨橋」からtrain-derived left contextにより「雨箸」へ選択が変わること、gold境界・未来文脈を使わないことを検証。
- C++ CI run `44407712171`（commit `28befedc1f4c09d42ef9e0b09acb0af57c025491`）: completed / success。CTest 21/21 PASS、Python unittest 63件（58 PASS / 5 SKIP）、warning-as-error build、bounded benchmark、reference Pack verificationがPASS。
- 実Sudachi Pack workflow run `44407712247`: completed / success。2,414 test sentences / 10,317 gold tokensで、gold token境界をデコード入力へ渡さず、future contextも使わずにC++全文デコードを実行。over-limit 0、decode failure 0。\n- Morphology-only: sentence exact 24.6479%、CER 48.6578%、token alignment precision 58.0839%、recall 56.2373%。train-derived unigram + predicted-left context: exact 24.8136%（+0.1657ポイント）、CER 48.4772%（0.1806ポイント低下）、alignment precision 58.2791%、recall 56.3924%。\n- Peak RSS 1,425,568 KiB（1,392.2 MiB）、1,792 MiB gate内。全文逐次評価のelapsed time 19.18秒（GitHub-hosted runner）。


### v0.30 C++全文逐次評価の結論

- 実Sudachi Packの全文reading-to-text pathでも、train-derived unigram + predicted-left contextはmorphology-onlyよりsentence exactを+0.1657ポイント、CERを0.1806ポイント改善した。Python token-aligned causal評価で見えた約+0.99ポイントより改善幅は小さく、token segmentation・path searchで候補/誤りが伝播する影響が表れている。
- このC++ full-path評価にはSURGE evidence gateを組み込んでいない。SURGE gateの85.43% precision / 1.32% correct-resolution recallはPythonのgold-token-boundary causal diagnosticの値であり、全文デコード結果と混同しない。
- 次の推奨課題はdev splitのみでcontext weight / smoothing / evidence thresholdを校正し、固定した設定をtest splitで再評価すること。test結果を見て閾値を調整するデータリークは避ける。


## 44. v0.31 Native SURGE evidence-gated decoding

- Added an opt-in native C++ decoder policy through DecoderOptions: observed left-context evidence must exist for at least two distinct candidate surfaces, and the best-vs-second context-only score margin must be at least 0.5 by default.
- Morphological variants sharing the same surface are collapsed for the gate decision. Only candidates with observed evidence receive context scores after the gate opens. If evidence is insufficient or the margin is too small, context contribution is zero and morphology plus connection costs remain the fallback.
- Added a native three-way full-path corpus evaluation: morphology-only, train-derived unigram + predicted-left context, and SURGE evidence-gated left-context. All three decode concatenated sentence readings without gold token boundaries; gold tokenization is used only for post-decode alignment.
- Fixed the sequential evaluator JSON contract to emit named nested metric objects and extended the test to parse and assert all three modes.
- Added unit tests for observed/missing context evidence, distinct-surface gate activation, and abstention when evidence is unavailable.
- Historical v0.30 full-path baseline: 2,414 sentences / 10,317 tokens; morphology-only exact 24.6479%, CER 48.6578%; unigram + predicted-left exact 24.8136%, CER 48.4772%; Peak RSS 1,425,568 KiB and elapsed 19.18 seconds on GitHub-hosted runner.
- v0.31 C++ CI run `44414980579`（commit `3d4a80d31d7782b6bd0e672efbfa57c0ded4dc36`）: completed / success。CTest 21/21 PASS、Python 63件（58 PASS / 5 SKIP）、warning-as-error build、bounded benchmark、reference Pack verificationがPASS。\n- v0.31実Sudachi Pack run `44414667242`（commit `569adbc9c48bfae9691d5b3297a7cb9f024a9d0a`）: completed / success。2,414文 / 10,317 token。morphology-only exact 24.6479%、CER 48.6578%。train-derived unigram + predicted-left exact 24.8136%、CER 48.4772%。default SURGE gateはmorphology-onlyと同一のexact/CERとなり、full-path test上で改善を確認できなかった。Peak RSS 1,425,512 KiB（約1,392.1 MiB）、1,792 MiB gate内。\n- この実測はSURGE gateが常に無効という意味ではなく、候補shortlist・予測履歴・証拠marginの組合せで、当該GSD testでは出力変更に至らなかったことを示す。閾値をtestで調整せず、v0.32でdev split限定の校正へ進む。
- Specification: docs/SPECIFICATION_v0.31.md.


## 45. dev-only SURGE校正とtest固定評価（v0.32）

- v0.31の実Sudachi Pack評価では、native SURGE gateの既定margin 0.5が実コーパスで解決に至らず、SURGE-gated full-path結果はmorphology-onlyと同一だった。したがってgateの単体テストPASSだけでは実データ上の有効性を示さないことを確認した。
- C++逐次評価器にsplit明示モードを追加。\`dev-calibrate\`はdev行のみ読み、\`test\`はtest行のみ読む。いずれもdecoderにはgold token境界を渡さず、gold tokenizationはdecode後のalignment計測にだけ使う。
- dev校正はcontext weight 4水準（0.25/0.5/1.0/2.0）、evidence margin 6水準（0/0.01/0.05/0.1/0.25/0.5）、smoothing alpha 3水準（0.1/0.5/1.0）の計72構成を評価する。
- 選択基準はCER最小、同値ならsentence exact accuracy最大、さらに同値ならexact token alignment最大。devでmorphology-onlyを上回るSURGE構成がなければcontext weight=0.0を出力し、既定alpha/marginへ戻す。devで悪化する設定をtestへ持ち込まない。
- MaskedNgramScorerにset_smoothing_alpha / smoothing_alphaを追加。alphaは有限かつ0より大きく100以下のみ許可し、default 0.5は維持。unigramとcontextual scoreの加算平滑化へ同じalphaを適用する。既存Pack形式・C ABI・既定挙動は変更しない。
- CIテストではalpha変更によるscore変化、0/非有限値の拒否、dev/test split分離、72構成の実行、校正TSVを使ったtest評価を検証する。
- Sudachi Pack workflowはdev校正とtest評価を別プロセスで実行し、Peak RSSを個別に1,792 MiB以下で検査する。校正値・コーパス・派生ngramはCI workspace限定であり、ArtifactやPackへ含めない。
- v0.32の精度結果はCI完了後に追記する。test結果を見て校正値を再調整しない。
- 仕様: docs/SPECIFICATION_v0.32.md。

- v0.32 code CI run `44415996245`（commit `c6b21bca5379b12d64f0f029cb2567705e714e5d`）: completed / success。Warning-as-error build、CTest 21/21 PASS、Python 63件（58 PASS / 5 SKIP）、bounded benchmark、Japanese reference Pack verificationがPASS。実Sudachi Packでの72構成dev校正とtest評価workflow run `44415786196` は現在実行中で、実測値は未確定。


## 43. v0.32 SURGE校正入力の堅牢化

- 校正ファイルの形式を厳密化し、3数値以外の末尾フィールドを拒否。
- DecoderOptionsのcontext_weight / surge_context_marginについて有限値・非負条件を検証。不正値はfail-closed。
- C++単体テストとPython統合テストを追加。精度向上は主張せず、数値入力の異常によるBeam順位破壊を防止する安全性変更として扱う。
- CI検証結果: 実行待ち。
