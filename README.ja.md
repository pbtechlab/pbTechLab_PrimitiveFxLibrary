# pbTechLab Primitive Fx Library（日本語）

[English README](README.md)

![pbTechLab Primitive Fx Library GUI collection](public_gui_screenshots/pbTechLab_PrimitiveFxLibrary_GUI_Collection_Public.png)

pbTechLab Primitive Fx Library は、Windows / macOS 向けの軽量オーディオエフェクト 17 種のコレクションです。各プラグインは **VST3** と **AAX**（Pro Tools）として提供され、共通の WebView ベース・エディタと自己完結型のネイティブ DSP を備えます。

本ライブラリは当初の JUCE 8 実装から **[iPlug2](https://github.com/iPlug2/iPlug2) ＋ WebView UI**（Windows: WebView2 / macOS: WKWebView）へ移行しました。移行済みのビルド可能プロジェクトは [`iplug2/`](iplug2) 配下にあります。各プラグインは独立した iPlug2 プロジェクトで、製品名・4文字プラグインコード・パラメータ・DSP・埋め込み HTML/CSS/JS エディタを個別に持ちます。

## 対応フォーマット / プラットフォーム

| フォーマット | Windows | macOS | 備考 |
|---|:---:|:---:|---|
| VST3 | ✅ | ✅ | 全 DAW が共通フォルダからスキャン |
| AAX Native | ✅ | ✅ | Pro Tools。配布版は PACE 署名が必要 |
| Standalone | ✅ | ✅ | 開発・動作確認用 |

macOS リリース版は codesign・PACE署名（AAX）・公証・staple 済み。配布インストーラは GitHub Releases のみで配布し、リポジトリにはコミットしません。

## 収録プラグイン

| プラグイン | コード | 主なコントロール | 用途 |
|---|---|---|---|
| pbPFL Reverb | `PrRv` | Size, Damping, Diffusion, Mod, Width, Dry/Wet, Output | アルゴリズミック・ステレオリバーブ（Freeverb系 comb/allpass）。 |
| pbPFL Delay | `PrDl` | Time, Feedback, Tone, Spread, Mod Rate/Depth, Dry/Wet, Output | フィードバック・スプレッド・モジュレーション付ステレオディレイ。 |
| pbPFL Distortion | `PrDs` | Drive, Bias, Tone, Shape, Dry/Wet, Output | サチュレーション／ディストーション。 |
| pbPFL Compressor | `PrCp` | Threshold, Ratio, Attack, Release, Makeup, Dry/Wet, Output | フィードフォワード型ダイナミクス（パラレル対応）。 |
| pbPFL Limiter | `PrLm` | Input, Ceiling, Release, Dry/Wet, Output | ピークリミッタ／ラウドネス制御。 |
| pbPFL 3BandEQ | `Pr3E` | Low/Mid/High の gain・freq・Q, Dry/Wet, Output | RBJ biquad 3バンドEQ。 |
| pbPFL 4BandEQ | `Pr4E` | Low shelf・2 bell・High, Dry/Wet, Output | RBJ biquad 4バンドEQ。 |
| pbPFL PitchShifter | `PrPs` | Semitone, Cents, Grain, Crossfade, Tone, Dry/Wet, Output | 時間領域ピッチシフト。 |
| pbPFL Chorus | `PrCh` | Rate, Depth, Delay, Feedback, Spread, Dry/Wet, Output | ステレオコーラス（変調補間ディレイ）。 |
| pbPFL Phaser | `PrPh` | Rate, Depth, Center, Feedback, Dry/Wet, Output | オールパス縦続フェイザー。 |
| pbPFL Flanger | `PrFl` | Rate, Depth, Delay, Feedback, Spread, Dry/Wet, Output | 短い変調ディレイ＋双極フィードバックのフランジャー。 |
| pbPFL StereoEnhancer | `PrSE` | Width, Enhance, Focus, Bass Mono, Dry/Wet, Output | ステレオ拡張＋低域モノ管理。 |
| pbPFL StereoWidth | `PrSW` | Width, Mono, Balance, Rotation, Dry/Wet, Output | ステレオ幅／モノブレンド／バランス／回転。 |
| pbPFL MidSideProcessor | `PrMS` | Mid Gain, Side Gain, Width, Balance, Dry/Wet, Output | Mid-Side ゲイン・幅処理。 |
| pbPFL AutoPan | `PrAP` | Rate, Depth, Phase, Shape, Offset, Dry/Wet, Output | テンポシンク対応オートパン。 |
| pbPFL Tremolo | `PrTR` | Rate, Depth, Shape, Phase, Dry/Wet, Output | 振幅変調（トレモロ）。 |
| pbPFL Vibrato | `PrVB` | Rate, Depth, Delay, Shape, Spread, Dry/Wet, Output | ピッチ変調（ビブラート）。 |

メーカーコード: `PbTL`。

## エディタ（WebView UI）

全プラグインが 1 つの HTML/CSS/JS エディタを共有し、各 OS の WebView（WebView2 / WKWebView）で描画します。意匠は ImGui 版を**画素忠実に再現**（ノブは ImGui のプロシージャル・シェーダを HTML Canvas へ 1:1 移植）。

- **プロシージャル LED ノブ**（`knob-render.js`）：シアン LED リング(270°)・暗いキャップ・白針＋青チップ・双極リング・LFO リング。
- **LFO chip** ポップアップ（Type / Rate / Depth ＋波形アイコン）。
- **テンポシンク**（`S`）：ノート↔Hz 変換、host BPM 連動。
- **A/B** スロット、**Undo/Redo**（64段）、**10 ファクトリプリセット**、**Bypass**、**Help**。
- ノブ操作：ドラッグ・微調整(Shift)・ホイール・ダブルクリック既定・数値直接入力。
- 1 つの UI 資産で iPlug2 ビルドと旧 JUCE WebView ビルドの両対応（`script.js` のホスト抽象ブリッジ）。

## DSP

各プラグインは、商用無料（パーミッシブ）の標準アルゴリズムで信号処理を自己完結実装（JUCE・専有DSPライブラリ非依存）：RBJ biquad（EQ）、Freeverb系ネットワーク（Reverb）、フィードフォワード・コンプ、オールパス縦続（Phaser）、線形補間ディレイ（Flanger/Chorus/Delay/Vibrato）等。信号フローは原実装に忠実：サンプル毎処理 → `dry·(1−wet) + processed·wet` → 出力ゲイン → bypass パススルー、入出力ピークメータ付き。

## ソースからのビルド（iPlug2）

要件：CMake 3.14+、C++17 コンパイラ、[iPlug2](https://github.com/iPlug2/iPlug2) チェックアウト、Visual Studio 2022（Win）/ Xcode（Mac）、Avid AAX SDK（AAX用）、WebView2 SDK（Win、iPlug2 が取得）。

各プラグインは `iplug2/<plugin>/` の独立プロジェクトです。Windows 例：

```powershell
cd iplug2/pbPFL_Flanger
cmake -B build -G "Visual Studio 17 2022" -A x64 -DIPLUG2_DIR="path/to/iPlug2"
cmake --build build --config Release --target pbPFL_Flanger-vst3   # VST3
cmake --build build --config Release --target pbPFL_Flanger-aax    # AAX（AAX SDK 必要）
```

macOS 例（universal）：

```bash
cd iplug2/pbPFL_Flanger
cmake -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DIPLUG2_DIR="$HOME/iPlug2"
cmake --build build --config Release
```

AAX SDK は `-DAAX_SDK_DIR=...` で指定可能。VST3 はシステム VST3 フォルダ、AAX は Avid Plug-Ins フォルダへ自動デプロイされます。

当初の JUCE 8 実装（共通 `Common/` ＋各 `mock/` UI）も参照用にリポジトリへ残しています。

## リポジトリのプライバシー

ビルド成果物・インストーラ・署名/PACE 認証情報・秘密鍵・マシン名・サーバースクリプト・API トークン・ローカル引継ぎ/ログは意図的に除外しています。リリースバイナリは GitHub Releases のみで配布します。
