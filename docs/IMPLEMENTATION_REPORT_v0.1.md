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
- 旧n-gram microbenchmark: 200,000回の4候補resolve、約2,298 ns/resolve（約2.30 µs）。現行CI microbenchmark（Ubuntu 26.04 / GCC 15.2 / Release）: n-gram resolve 約2,697 ns、2-token/5-edge小規模ラティスdecode 約1,116 ns。単一CIランナー（Ubuntu 26.04 / GCC 15.2 / Release）の小規模測定であり、全文変換・p50/p95/p99・実機CPU・RSSを含まず、1文節1ms未満を証明しない。再現時はActions run 44343988998のログを参照。

## 5. 既知の制約
1. 実用日本語形態素辞書・大規模接続行列は未実装。N-best参照デコーダと小規模接続モデルfixtureは実装済み。
2. n-gram countの簡易学習パイプラインは実装済み。実コーパスの前処理・ドメイン別評価・確率校正は未実施。
3. Correction Engineは候補生成器。辞書・形態素解析器との統合が必要。
4. DependencyGraphは単体実装。Streaming Context CoreのClaim / Evidence / Question / Focusとの統合が必要。
5. Pack Manifest/SHA-256検証はPythonツール。C++ runtime側のManifest/SHA-256検証、mmap、複数Sessionでの辞書共有ポリシーは未実装。
6. 30 MiB実用Packにはライセンス確認済み語彙の選定・インポート・圧縮・カバレッジ測定が必要。
7. UTF-8不正シーケンス拒否とコードポイント範囲処理は実装済み。Unicode NFC等の正規化、全APIにわたる原入力byte span対応は未実装。
8. 補正閾値はfixture上の値。実データで誤修正率・見逃し率を評価して決める。
9. 部分Commitは保持中のN-best上位集合の共通接頭辞に限定。Beamで破棄された候補まで含む保証ではない。
10. C++ Pack loaderはManifestのhashを検証しない。必ずPython verifierを事前実行する。

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
- `load_language_pack` はlexicon.tsvとngram.tsvを一時オブジェクトへ読み込み、両方成功した場合のみEngine側へ反映する。Manifest、SHA-256、ライセンス、サイズの検証はC++ローダーではなく既存のオフラインPython検証器で事前実行する必要がある。
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
- C++ PackローダーはTSV構造を検査するが、ManifestのSHA-256検証は行わない。利用前に `tools/verify_pack.py` を実行する運用契約とし、C ABIのPackロード関数も同じ前提に従う。
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
- Commit: `f002f3b292b53b67b3803aeec1cd76bb0a94a129`（この段階の実装。後続の報告更新コミットは除く）
- GitHub Actions run `44343988998`: Configure / Build / CTest / benchmark / Pack verificationすべてPASS。
- CTest: 5/5。C API integration、C++ engine、Pack validation、Japanese smoke quality、Pack builder。
- Python Pack verifier tests: 7件。Pack builder tests: 2件。
- Smoke gold corpus: 4件中4件完全一致。これはfixtureの配線確認であり、製品精度指標ではない。
- CI microbenchmark: n-gram resolve 2,697.2 ns/resolve、2-token/5-edge decoder 1,116.08 ns/decode。1回の小規模測定であり、実機・実用辞書・全文処理・tail latency・RSSの保証ではない。
- 追加したstrict numeric parserはint32/uint32の範囲と文字列全体の一致を検査し、`100junk` や `10x` のような末尾不正文字を拒否する。
