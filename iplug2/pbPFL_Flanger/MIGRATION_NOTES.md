# pbPFL Flanger — JUCE → iPlug2 移行パイロット

JUCE 8 ベースの Flanger を **iPlug2 + WebView (WebView2 / WKWebView)** へ移植した先行パイロット。
目的: 脱JUCE・商用無料ライセンス・**ImGui版デザインの採用**・**機能/パラメータの完全維持**。

## 構成
| ファイル | 役割 |
|---|---|
| `config.h` | プラグイン識別子（`PrFl` / mfr `PbTL`）、2-2、740×214 |
| `PbPFLFlanger.h` / `.cpp` | iPlug2 Plugin。7パラメータ+Bypass、Flanger DSP、UI↔DSP メッセージ |
| `resources/web/{index.html,style.css,script.js}` | WebView UI（両ホスト対応ブリッジ込み） |
| `CMakeLists.txt` | `iplug_add_plugin(... UI WEBVIEW)` |

## ビルド（Windows）
```
cmake -B build -G "Visual Studio 17 2022" -A x64 -DIPLUG2_DIR="C:/Users/b_pan/Project/iPlug2"
cmake --build build --config Debug --target pbPFL_Flanger-app    # standalone
cmake --build build --config Release --target pbPFL_Flanger-vst3 # VST3
```
- iPlug2 は `C:/Users/b_pan/Project/iPlug2`（別ロケーションは `-DIPLUG2_DIR` で上書き）。
- **VST3 デプロイ先**: `C:/Program Files/Common Files/VST3`（全DAW共通スキャン先）。CMake の
  `IPLUG_VST3_DEPLOY_PATH` で設定済み。ビルド毎にバイナリ＋`web/` がここへコピーされる。
- WebView2 SDK は iPlug2 が `build/_deps/webview2` に自動DL。オフライン環境ではローカル NuGet
  パッケージ（`%LOCALAPPDATA%/PackageManagement/NuGet/Packages/Microsoft.Web.WebView2.*`）の
  `build/` を同パスへ配置すればDLを回避できる。
- VST3 SDK は `iPlug2/Dependencies/IPlug/download-vst3-sdk.sh` で取得済み前提。
- **Windows の Web 資産配置**: iPlug2 の `WEB_RESOURCES` は macOS バンドルにしかコピーされない
  （`MACOSX_PACKAGE_LOCATION` のみ設定）。本プロジェクトは CMake POST_BUILD で `resources/web` を
  プラグインバイナリ隣の `web/` へコピーし、Release の `mEditorInitFunc` が `PluginPath()+web\index.html`
  を `LoadFile` する。Debug は `__FILE__` 経由でソースツリーを直接ロード（高速反復用）。

## DSP（品質劣化ゼロの方針）
`ProcessBlock` は元 `PrimitiveFxProcessor::processFlanger` + `mixDryWet` の**1:1忠実移植**。
- 可変ディレイ = 線形補間（`juce::dsp::DelayLine<Linear>` と等価の `FlangerDelayLine`）。
- LFO(sin) で `base + sin*depth` 変調、双極フィードバック、ステレオ Spread、Dry/Wet、Output、Bypass=完全パススルー。
- DSP調査結果: トラステッドな置換候補は **DaisySP `DelayLine`(MIT)**（Hermite補間で更に高品質化可能）。
  パリティ最優先のため現状は等価な自前線形補間を採用。差し替えは1クラス分。

## パラメータ（元と完全一致・id/範囲/既定/単位）
rate 0.03–8Hz/0.32, depth 0–100%/55, delay 0.1–15ms/4.5, feedback −95–95%/42,
spread 0–180deg/135, dryWet 0–100%/50, output −24–12dB/0, bypass=bool。

## ホスト抽象ブリッジ（`script.js` 冒頭 `Host`）
- iPlug2: `SPVFUI/BPCFUI/EPCFUI/SAMFUI` ↔ `SPVFD/SAMFD`（paramは正規化、id↔index変換）。
- JUCE: `__JUCE__.backend.emitEvent` ↔ `window.handleHostMessage`。
- standalone ブラウザ: ホスト無しでもプレビュー可。
→ 1つのUI資産で**現JUCEビルドと新iPlug2ビルドの両方**を駆動。

## 機能パリティ状況（ImGui版＝上位集合 基準）— フルパリティ達成
完了:
- 7ノブ（ドラッグ/ホイール/ダブルクリック既定/数値直接入力）
- **テクスチャ・ノブをピクセル忠実に再現**: ImGui の `GenerateKnobPixels` プロシージャル
  シェーダを `knob-render.js` で Canvas に1:1移植（シアンLEDリング270°/暗いキャップ/白針/青チップ/
  双極リング/LFOリング）。ImGuiデモ(`ImGuiAudioKnobDemo.exe`)と照合済み。
- **LFO chip "L" ポップアップ本体**（`DrawLfoChip` 準拠）: Type(SINE/TRI/SAW/RAMP/SQR/S&H) /
  Rate(0.1–8Hz or tempo sync) / Depth(0–1) のミニLEDノブ＋波形アイコン＋Rateノブの
  LFOリング・アニメーション。
- テンポシンク "S"（ノート↔Hz、host BPM 連動 / `OnIdle` で BPM 配信）。Rateのみ S/L タブ（ImGui同様）。
- A/B、Undo/Redo（64段）、Bypass、Help オーバーレイ
- **10ファクトリプリセット（ImGui `factoryPresetValues` のshape配列を完全再現＝同値）**
- ヘッダー/フッター/配色/ボックスレス・レイアウト = ImGui版準拠（ヘッダー題字は淡色 #e0e6f0）

軽微な残（パリティ中立。必要時に対応）:
- [ ] ユーザープリセット/A/B の永続化（元JUCEもエディタ内のみ＝中立）。
- [ ] Help リンクのプラグインからの外部ブラウザ起動（現状はオーバーレイ表示のみ）。
- [ ] LFO chip→Rate の接続三角ポインタは簡易版（ImGuiは可変頂点）。

## 検証
- ブラウザ（740×214）で UI 描画一致を確認。
- `pbPFL_Flanger-app.exe`（standalone）起動 → WebView2 が UI を描画。
- **VST3 を VST plugin checker (VPC) でロード → エディタにフルUIが描画されることを確認**（下記2修正後）。

## Windows VST3 で遭遇した重要バグと修正（必須・横展開時も必要）
1. **エディタが真っ暗（最重要）**: iPlug2 の `IWebViewImpl::OpenWebView` は**サイズ引数を無視**し、
   WebView2 の bounds は `Resize`/`OnParentWindowResize` でしか設定されない。VPC 等はエディタを開く際に
   リサイズイベントを発火しないため、WebView2 が未初期化 bounds（例: 961×1170）のまま描画され**画面外＝黒**。
   → 対策: `mEditorInitFunc` 内で `SetWebViewBounds(0,0,GetEditorWidth(),GetEditorHeight(),1.0f)` を明示呼び出し。
2. **WebView ブリッジ関数欠落**: iPlug2 が注入するのは低レベルの `IPlugSendMsg` のみ。`SPVFUI/SAMFUI/BPCFUI/EPCFUI`
   （UI→DSP送信ラッパ）は**アプリ側 JS で定義が必要**（テンプレ script.js にあったもの）。`script.js` 冒頭で定義。
3. **真っ暗の調査知見**: WebView2 の内容は **GDI/PrintWindow スクリーンキャプチャでは黒く写る**（GPU 合成）。
   UI 実描画の確認は「C++→診断ファイル + JS の段階メッセージ」で行うのが確実（恒久コードには残さない）。
4. 列挙子 `kOutput` は `iplug::ERoute::kOutput` と衝突するため `kOutputGain` に改名。
5. `gHINSTANCE` は VST3 では未設定になり得るため、`web/` 解決は `GetModuleHandleEx`（自シンボルのアドレス）で自モジュールパスを取得。
