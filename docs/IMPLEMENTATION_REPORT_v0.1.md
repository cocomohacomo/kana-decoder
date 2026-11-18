# kana-decoder 実装・検証報告 v0.1

- 対象仕様: SPECIFICATION_v0.7.md
- 実装: C++17 Core / C API / Pack validation tools
- 検証環境: Linux x86_64 container, GCC 14.2.0, Python 3.13.5, Release build
- 判定: コア機能の参照実装とテストは成功。製品相当の全文かな漢字変換器・実用日本語辞書は未完成。

## 1. 実装範囲
| 要求 | 実装 | 現時点の境界 |
|---|---|---|
| 1. 曖昧性候補/API | AmbiguityResult、Candidate、4出力モード、C API | Morphological Decoder全体との統合は未実装 |
| 2. Context-masked n-gram | 左右文脈のmasked n-gram順位付け、最大2トークン窓、加算平滑化 | 手作業fixture count。実コーパス学習・確率校正なし |
| 3. 軽度誤り補正 | UTF-8コードポイント重複削除、Pack規則による挿入・削除・置換、編集コスト、候補上限 | ASR音響信頼度連携・音韻モデル学習なし |
| 4. 依存再評価 | 三層依存グラフ、下流限定無効化、revision | Streaming Session / Claim / Question統合は未実装 |
| 5. テスト | C++ assert、Python Pack検証、CTest | 大規模ゴールドコーパス評価は未実施 |
| 6. 日本語Lite Pack | 語彙・n-gram・補正規則・MIT manifest、SHA-256/ライセンス/容量検証 | 小規模fixture。実用語彙カバレッジの30 MiB Packではない |

## 2. API
MaskedNgramScorerは対象spanをマスクした左右文脈から候補順位を計算する。AmbiguityResultは候補順位・top score・margin・曖昧性判定を保持する。Ambiguousモードは低確信時に [雨/飴/other] を生成し、Conservativeモードは元かなを保持する。CorrectionEngineは原文を変更せず補正候補を別に生成し、補正コスト控除後の改善幅と最低スコアで自動採用可否を判定する。DependencyGraphは下流依存だけを無効化する。C APIはn-gram scorerを公開する。

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
- Python Pack validation: PASS（3 tests）
- CTest: 2/2 passed
- C API header C compiler syntax check: PASS

## 4. サイズ・速度
- Release static library libkana_decoder_core.a: 131,502 bytes。この環境での静的ライブラリサイズであり、全OSのruntime保証ではない。
- 日本語Lite fixture: payload 1,574 bytes + manifest 1,048 bytes = 2,622 bytes（約0.00250 MiB）。SHA-256全4ファイル検証、30 MiB上限PASS。
- fixtureはテスト用であり、実用辞書のサイズ・語彙カバレッジ評価には使えない。外部辞書はライセンス確認前のため含めていない。
- n-gram microbenchmark: 200,000回の4候補resolve、約2,298 ns/resolve（約2.30 µs）。コンテナ内の単一測定で、全文変換・p95/p99・実機CPU・RSSを含まない。1文節1ms未満を証明しない。

## 5. 既知の制約
1. 実用日本語形態素辞書・接続行列・Viterbi/N-bestは未実装。
2. n-gram countはfixture。学習パイプライン、ドメイン別評価、確率校正が必要。
3. Correction Engineは候補生成器。辞書・形態素解析器との統合が必要。
4. DependencyGraphは単体実装。Streaming Context CoreのClaim / Evidence / Question / Focusとの統合が必要。
5. Pack検証はPythonツール。C++ runtime Manifest loader、mmap、Session共有、ABI検証は未実装。
6. 30 MiB実用Packにはライセンス確認済み語彙の選定・インポート・圧縮・カバレッジ測定が必要。
7. Unicode不正シーケンス検証・正規化・原入力byte span対応は次段階。
8. 補正閾値はfixture上の値。実データで誤修正率・見逃し率を評価して決める。

## 6. 総合判定
**実装基盤と単体テストは成功。製品機能としては部分実装。**

要求1〜4は独立したC++参照実装として動作し、要求5の自動テストと要求6のPack検証prototypeも動作する。一方、全文変換パイプライン・実用辞書・実コーパス評価・runtime Pack loaderは未完了であり、現段階で完成版や性能保証とは扱わない。


## 7. N-best形態素ラティス統合（追加実装）
- `MorphologyDecoder` を追加し、読み位置ごとの `LatticeEdge` と複数の `LatticeCandidate` から完全経路を探索する。
- 各候補の形態素スコアは `-word_cost - connection_cost`。過去最大2トークンの文脈を `MaskedNgramScorer` に渡して加算し、後続形態素による先行候補の再順位付けを行う。
- Beam幅、位置ごとのEdge数、Edgeあたり候補数、返却経路数に上限を設ける。無効な範囲・到達不能な読みは経路を生成しない。
- デコード結果は上位N経路とトークン範囲、候補ID・表層、形態素スコア・文脈スコアを返す。
- テスト追加: 後続トークンの文脈で先行同音異義語が選択されること、読み末尾まで到達しないラティスの拒否、Beam/出力件数上限。
- 注意: これはラティス入力を受け取るデコーダAPIであり、辞書からのラティス自動生成、実用日本語形態素辞書、Viterbi互換の言語Pack接続、ストリーミングCommit制御との統合はまだ未完了。
- 今回、GitHub Actionsワークフロー `.github/workflows/ci.yml` を追加。push / PRでCMakeビルド、CTest、Pack検証を実行する。2026-11-18のGitHub Actions run #4（commit `86583728e1d3de3750641aa7b044f7c0e213a6f0`）でConfigure / Build / CTest / Pack検証がすべて成功。初回CIで検出したfixture Manifestのサイズ・SHA-256不整合を修正し、再実行でPASSを確認した。
