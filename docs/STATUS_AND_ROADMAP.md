# Pocket Euclidean — 現状仕様・実装状況・ロードマップ

**文書の目的:** 共有された製品仕様と UI・入力仕様、および現在のソースコードを照合し、確定している要求、実装上の暫定値、実装状況、今後の作業を一か所にまとめる。

**この文書の位置づけ:** 共有仕様で明示された要件を「要求」、仕様の未決事項に対して現行コードが選んだ値を「暫定決定」とする。暫定決定は、製品要求として確定したことを意味しない。仕様間で明白な矛盾は見当たらないが、未決事項と未検証事項は残っている。

**引き継ぎ時の読み方:** 実装済みはソース上に存在することを示し、テスト成功や実機動作を自動的に意味しない。検証状態は「検証済み／未検証」を参照する。Windows x86-64でのコアビルド、CTest 2/2、SOUND／ファイルセレクタのGUI確認は済み。次に着手する優先作業は (1) Brick Pro実機での入力・音声・保存・終了確認、(2) Save Asとサンプル欠落時の復旧導線、(3) 実機でのCPU・RSS・callback deadline計測である。スレッド所有権の詳細は [ARCHITECTURE.md](ARCHITECTURE.md)、実機ツールチェーンは [PLATFORMS.md](PLATFORMS.md) を参照する。

## 1. プロジェクト概要

- C++17 と CMake を使う、8トラックの独立したユークリッド・リズム・グルーヴボックス。
- 第一優先ターゲットは TrimUI Brick Pro（Allwinner A133P、ARM64対応 Cortex-A53 クラス、RAM 1 GB、画面設計基準 1024×768）、NextUI 上で PortMaster から起動する構成。
- 1 GB RAMはNextUI／OSと共有される。Brick Pro向けの初期目標はアプリRSS 128 MiB以内、デコード済みサンプル64 MiB以内、全体同時発音数32 voice以内、UI 30 fps。これらは実機計測前の暫定的な上限候補であり、上限なしの確保は禁止する。
- Linux デスクトップ、Windows、macOS、および他のレトロハンドヘルドも対象候補とし、共通コアと機種別バックエンド／配布設定を分離する。
- UI／入力は SDL2 を候補とし、初期描画は SDL2 2D Renderer（OpenGL依存なし）。すべての入力機器を共通論理アクションへ変換し、生ボタン番号へ依存させない。
- Brick Pro の NextUI／PortMaster バージョン、実行環境ABI、コンパイラー、SDL2リンク方式、入力・音声・保存・終了動作は未検証。AArch64ビルド成功だけで対応済みとはしない。
- デバイスごとに OS の ABI、コンパイラー、SDL2 ランタイム、入力・音声バックエンド、配布方法を確認する。CPU アーキテクチャが同じでも、同じ実行ファイルが動くとは限らない。
- 既存実装はアプリケーション全体ではなく、シーケンサー／LFOコア、UI状態・入力ロジック、プラットフォーム境界インターフェースの一部である。

## 2. 要求仕様のうち合意済みの範囲

| 分野 | 要求 |
| --- | --- |
| トラック | 初期版は8トラック。各トラックに独立したパターン長、フィル、オフセット、再生位置、楽器、音量、ミュートを持つ |
| ユークリッド・リズム | 長さ `Eleng` は2–32、フィル `Efill` は1–`Eleng`、オフセット `Eoffs` は0–31。フィル数を維持し、できるだけ均等にパルスを配置する |
| クロック | 全トラックで共有するマスタークロックを使い、16分音符単位でトラック長の違いを独立して進行させる。START は全トラックを先頭に同期リセットし、STOP はクロックを停止する。UI フレームレートに音楽タイミングを依存させない |
| 楽器 | 初期の楽器種は WAV サンプラーと軽量な VA シンセ。オーディオコールバックでファイル I/O を行わない |
| エフェクト | ディレイと基本フィルター。ルーティングは実装時に定義する |
| UI | MAIN / EDIT / SOUND / MIXER / MODULATION / TURING / SYSTEM の7画面。Project Menu は global system settings から独立したモーダル。物理コントローラーとデスクトップキーボードから主要機能にアクセスし、マウスは必須にしない |
| プロジェクト | 8トラック、テンポ、ミュート、楽器・各パラメーター、エフェクト、サンプル参照を保存できるバージョン付き形式 |
| 変調 | トラックごとに独立LFOを用意し、Eleng/Efill/Eoffsの基準値と有効値を分離。整数値の決定的量子化・クランプと、パターン長変化時の位相処理を定義 |
| Jitter | Euclideanパターンを変更せず、トリガー直前のイベント処理でInsert/Drop/Shift/Mixを適用。強度、マスター同期Rate、Shift範囲、Seedをトラック単位で保持 |
| Mixer | 各トラックの音量・正規化Pan・Muteをプロジェクトに保存。Master Volumeはプロジェクト間で一定のグローバルAudio設定。ピークメーターは実行時値 |
| MIDI-ready | マスタークロック／トランスポート、トリガーイベント、音声・入力・MIDIのプラットフォームI/Oを別インターフェースにする。特定MIDIライブラリーへシーケンサーを依存させない |
| SYSTEM 設定 | クロック、MIDI、音声、入力、画面、保存、診断をカテゴリ化し、音楽プロジェクトとは別の版付きモデルにする |
| クロスプラットフォーム | プラットフォーム共通のコアと、ターゲット別ビルド・入力・配布設定を分ける |
| Brick Pro 音声・表示 | 1024×768を表示設計目標とし、SDL2オーディオコールバックを使用。48 kHzは候補値であり、実機で確認する |

## 3. 実装で採用した暫定決定

以下は仕様の未決事項に対する現時点の初期値であり、必要なら変更する。

| 項目 | 暫定値 |
| --- | --- |
| 初期テンポ・範囲・分割 | 120.0 BPM。40–240 BPM、4分音符 BPM を基準に1ティックを16分音符とする（範囲・分割は提案値） |
| パターン向き | オフセット適用前のステップ0は `((step * fill) % length) < fill` の規則で計算する |
| オフセット方向 | 正の値で発音ステップを高いステップ番号側へ回転する。生成では `offset % length` を使い、設定値自体は0–31を保持する |
| フィル0 | 非対応。無音化にはトラックミュートを使う |
| 再生中のリズム編集 | 即時反映。長さ変更で再生位置が新しい長さを超える場合は剰余で正規化する |
| START / STOP | START は全トラックとマスター位置を0に同期リセットして再生開始。STOP はクロックを停止し、次の START も先頭から始める。再生中の START 呼び出しは位相をリセットしない |
| トラック位相 | 各共有クロックティックで全トラックが進み、それぞれの長さで個別に折り返す |
| 初期 UI 操作 | ゲームパッドの論理 A/B/X/Y、D-pad、肩・トリガー、Start/Select を共有アクションへ割り当てる。キーボードは矢印、Enter、Escape、Space、M、Q/E 等を使う |
| 安全な確認 | 未保存変更があるときの新規・読み込み等は確認対象とし、確認の初期選択は Cancel とする |

### 拡張性追補の初期実装決定

- データモデルは各トラック最大4基の独立LFOを保持する。初期UIで編集するのは各トラックの1基目。各LFOは Sine / Triangle / Square / Saw Up / Saw Down / Random (Sample & Hold)、SYNC/FREE、Amount、単一Destinationを持つ。
- FREE rate は0.01–1 Hzでテンポ変更の影響を受けない。SYNC周期は全音符から16分音符までの各音価についてストレート／付点／3連符を選択でき、マスタークロックのテンポに追従する。
- LFO位相と出力はオーディオ時間基準のマスター16分音符ティックごとに更新し、UIフレームレートに依存しない。S&Hは各LFO周期の完了時にトラック・LFO別の決定的PRNG値を更新する。
- ±1のLFO値と符号付き整数Amountを各出力先の基準値に加算し、複数LFOが同じ出力先を変調する場合は寄与を合算してから最近整数へ丸め、範囲制限する。基準値は保持される。
- 有効パターン長が変わった時は該当トラックだけを
  `position % effectiveLength` で正規化する。マスタークロックはリセット
  しない。STARTではクロックとともにLFO位相も初期化する。
- Sample-and-Hold はトラックごとの再現可能な疑似乱数シーケンスを使う。
- Jitterは変調後Euclideanパターンの後、楽器トリガーの前に適用する。Insertは元の非アクティブステップだけ、Dropは元イベントを確率抑制、Shiftは符号付き整数ティック数だけ移動し、パターン端で循環する。衝突は元ステップ番号が小さいイベントを優先し、MixはDrop→Shift→Insertの順で処理する。
- JitterのRateは16分／8分／4分／2分／全音符のマスターティック境界で確率判断を更新する。イベントマスクは現在の有効パターンから毎ティック構成する。強度は0–100%。Shift範囲は最大8ティック。Mixでは3操作が同じ強度を共有する。カスタムSeedを使える。START/Resetで乱数状態を初期化し、STOP中は状態を進めない。設定変更は次のティックから反映し、すでに配信したイベントは変更しない。
- 現実装のShiftはマスターティック単位で、サブティックのサンプル位置調整ではない。サブティック移動には音声スケジューラーの先読み境界が必要。
- SYSTEM設定と ProjectData は別々の型とフォーマットバージョンを持つ。
- 音声、入力、MIDI、トランスポートの抽象インターフェースを置く。SDL2
  音声／入力とWindows WinMM MIDI入力はその背後に実装し、シーケンサーは
  OS固有APIに依存しない。

### 例示プリセット（自動ロードはしない）

| Track | 役割例 | `Eleng` | `Efill` |
| --- | --- | ---: | ---: |
| 1 | キック | 16 | 4 |
| 2 | スネア | 16 | 3 |
| 3 | ハイハット | 12 | 5 |
| 4 | パーカッション | 7 | 3 |
| 5 | ベースシンセ | 15 | 4 |
| 6 | シンセ／アクセント | 8 | 5 |
| 7 | テクスチャ | 11 | 2 |
| 8 | アクセント | 5 | 2 |

この例の全パターン長が同時に先頭へ戻る周期は 18,480 ティック。
120 BPM では1ティック125 ms、1拍500 ms。

詳しい根拠・未決事項は [DECISIONS.md](DECISIONS.md)、入力割り当ては [UI_CONTROLS.md](UI_CONTROLS.md) を参照。

## 4. 現在の実装状況

### 実装済み（ソース上で確認できる範囲）

- **ユークリッド・パターン生成:** 設定範囲の検証、決定的なパターン生成、回転、個別ステップ判定。
- **8トラックのシーケンサー状態:** 独立位置、リズム設定、ミュート、楽器種別、0–100 の音量値、テンポ値。
- **ミキサーDSP:** 0–100%音量カーブ、正規化パン、Monoのequal-power / Stereoのbalance、5msゲインスムージング、トラック別加算ヘッドルーム、マスターsoft limiter、atomic peak meter出力。
- **サンプルvoiceプールと再生経路:** 固定長32 voice、事前ロード済みmono PCM参照、サンプルレート変換の線形補間、0.25–4倍再生速度、古いvoiceからの決定的stealingを実装。`SamplerMixerRenderer`がティックのsample offsetでcallbackブロックを分割し、sampler voiceをMixer DSP・meter・limiterへ接続する。固定長SPSCキューで音量／Pan／Mute／AD／filter／シンセ各値／センド／ディレイ／master volumeのUI値をcallback block境界で渡す基盤とUIコントローラー接続を追加。**再生中のサンプル差し替えは、容量32のSPSC要求キューと適用IDのACKで行う。旧バッファは音声スレッドの適用確認後までUI側が保持し、再生中でも安全にWAVを交換できる。** SDL音声デバイス接続と実機期限測定は一部未検証。
- **シンセ音源の差し替え境界と軽量VA基準実装:** 固定8スロットの`SynthVoiceRack`と`ISynthVoice`契約に加え、外部依存のないモノフォニック`BasicVaVoice`を実装した。サイン近似／三角／鋸歯／pulse間のmorph、pulse幅timbre、簡易2次倍音colorを提供する。Attack/Decayはサンプラーとシンセ共通のトラック別VCAとして実装し、トラック別ローパスと合わせProjectData v10の設定モデルへ追加した。SOUND状態コントローラーからAttack 1–250 ms、Decay 20 ms–4 s、ローパス有効化と100 Hz–20 kHz cutoffを変更できる。**Plaits風`PlaitsVoice`（16モデル）と、エンジン種別で両者を切り替える`SelectableSynthVoice`を追加した。エンジン切替は`TrackSynthEngineType`、モデル選択は独立した`TrackSynthModel`としてSPSCキュー経由で音声スレッドへ渡し、UIスレッドからラックを再構成しない。SOUND画面には MODEL 行（番号＋短縮名）を追加し、Morphはモデル内の連続パラメーターとして残した。** これはBraids／Plaits互換実装ではなく、音質と実機性能は未測定。CPU予算、Brick Pro deadline、8 voice負荷を実機で検証する。
- **WAVデコードとサンプル参照:** 有界RIFF/WAVE parserがPCM 8/16/24/32-bitとIEEE float32のmono/stereoを呼び出し側の固定バッファへmono floatでデコードする。`SampleCache`はcaller-owned arenaと固定8スロットmetadataで、各ファイルと合計のデコードPCMを64 MiB以内に制限する。first-fitで空き連続領域を使い、既存データを移動しない。PCM/float WAVEFORMATEXTENSIBLE、unknown padded chunks、不正／非有限／容量超過データを扱う。**アプリ側はUIスレッドでWAVをデコードしてトラック別に所有し、`SamplerVoicePool`の有界キューで再生中に差し替える。プロジェクトv10はトラック別サンプルパスを保存し、読み込み時に相対／絶対パスを解決して復元する。読み込み失敗時はビルトインへフォールバックする。** `SampleCache`はライブラリとテストには残るが、現行アプリの経路では未使用。
- **MIXER画面状態:** 8トラック選択、音量／パン／ミュート／フレーズLOCK／フレーズDICE（新シード）／リバーブセンド／ディレイセンド／マスター音量のキーボード・コントローラー操作。描画バックエンドは実装済み。Master VolumeはGlobal SystemSettings、トラック値・センド・LOCKはProjectData v11に保存する方針。
- **トランスポートとティック:** 初期テンポ120 BPM、開始時の全トラック同期リセット、停止、位置リセット、共有マスター・ティック位置、サンプル数とサンプルレートに基づく16分音符スケジュール計算。START/STOPはトグルで、押下エッジのみ反応し、キーやボタンの離上では再実行しない。コールバックへサンプルオフセットと全トラックのトリガー結果を渡す。120 BPM ではティック間隔125 ms。
- **プラットフォーム音声契約:** `IAudioBackend`は要求／取得音声仕様を分けたfloat stereo callback契約を持ち、コールバックへ単調な絶対開始フレームを渡す。取得仕様の範囲検証関数と、sample offsetでSequencerを進めて照明SPSCキューへ接続する`AudioCallbackProcessor`を追加。sampler/synth voiceはfixed buffers経由でMixerへ到達し、トラック別AD VCAと任意の一次ローパスを通る。リアルタイム対象パラメーターは固定長SPSCキューでUI側shadowからcallbackへ渡す。SDL2実装、実シンセDSP／delay effects、残りのSequencer／transportパラメーターの安全なライブ受け渡しは未実装。
- **論理入力変換:** キーボードとSDL GameController論理操作を共通アクションへ変換する。固定長のコントローラープロファイルは割当変更とボタン無効化に対応し、未知値を拒否する。機種別プロファイルの選択・永続化とJoystick fallbackは未実装。
- **入力リピート:** 押下直後の操作、初期遅延後の方向リピート、および長押し時の加速。最大32個の同時押下を独立追跡し、重複key-downで遅延をリセットしない。トグル系アクションを方向リピート対象から除外する。**非リピート系アクションは押下エッジでのみ実行し、キー離上では再実行しない（`InputMapping::shouldDispatch`）。フォーカス喪失・最小化・デバイス切断時は保持リピートと修飾キー状態をクリアする。**
- **UI 状態コントローラー:** MAIN / EDIT / SOUND / MIXER / MODULATION / TURING / SYSTEM の画面状態、トラック選択、基本的なフィールド編集、楽器選択、音量・リズム値、LFO/Jitter/Turing編集、SYSTEMカテゴリ選択、ヘルプ／プロジェクト等のモーダル状態。
- **MAIN表示モデル／レイアウト／描画コマンド:** `MainScreenSnapshot`が有効パターン等を固定長データで提供し、`layoutMainScreen`が座標化、`buildMainScreenDrawList`が意味付きの固定長・メモリ確保なし描画コマンドを生成する。`TickResult`にティック時点の各トラックのステップ番号を追加し、容量32のSPSCキューで音声側からUIへ発音イベントを渡す。`AudioCallbackProcessor`から発音イベントをキューへ発行し、UI側の8×32固定長`StepLightBank`が絶対音声フレームから照明エンベロープを評価する。未来のイベントは時刻まで保留し、寿命を過ぎたイベントは破棄する。lock-free要件を満たさないターゲットではオーディオ開始を拒否する必要がある。SDL実描画と発音照明の画面反映は未実装。
- **UI発音照明エンベロープ:** SDL2に依存しない`StepLightEnvelope`が単調時刻から照度を評価し、固定長`StepLightBank`が音声フレーム時刻付きイベントから8×32ステップの状態を更新する。Attack 18ms / Hold 25ms / Decay 170ms、再トリガー時は現在照度から滑らかに再上昇する。SDL描画、実オーディオコールバックとの接続、実機での視認性調整は未実装。
- **テーマ配色:** `ThemePalette`が背景／主文字／抑制色／アクセントの意味色とQuiet Dark・Warm Paper・High Contrastに加え、Neon RGB・Outrun・Amber CRT・Phosphor・VGA DOSの計8プリセットを提供。レトロコンピュータ系は暗い背景と高彩度のRGB蛍光色（シアン／マゼンタ／アンバー／グリーン／VGAイエロー）で構成し、同じ4ロールのまま全画面を一括で切り替える。SYSTEM > Displayで左右キーからグローバルテーマを変更でき、プロジェクトのdirty stateとは独立する。SystemSettings設定モデル、永続化、SDL描画への適用は実装済み。
- **LFO変調エンジン:** トラックあたり最大4スロットを持つデータモデル、初期UIは1基を編集。6波形、FREE/SYNCレート、1基あたり単一出力先、複数寄与の合算、基準値と有効値の分離、整数量子化・範囲制限。長さ変調でトラック位相のみ正規化する。
- **Jitterイベント処理:** 各トラック独立のInsert/Drop/Shift/Mix、強度・同期間隔・符号付きシフト範囲・任意Seed。固定サイズの非破壊イベントマスクを生成し、Euclideanパターンは保持する。MODULATION画面で主要設定を編集できる。
- **Turing Machine（S&Hフレーズ生成）:** `src/sequencer/Turing.{h,cpp}`がトラック別32バイトのレジスター、頭位置、CHANCEによる再抽選、スケール／ROOT量子化、±24半音クランプ、ループ境界DRIFT、シード再生成を実装する。クロックはTEMPO（16分tick）、EUCLID（jitter後のトラックトリガー）、FREE（Hz、テンポ非依存）の3モードで、RATEの解釈をモード別に持つ。生成した半音オフセットは`TickResult.pitchOffsets`で音声側へ渡し、サンプラーのplaybackRateとシンセのpitchHzへ反映する。GLIDEは最大0.5秒のブロック平滑化、ライブ表示は`ModulationMirror`経由でhead・32バイト・半音を公開する。TURING画面でENABLE／LOCKを含む13パラメーターを編集できる。LOCKはレジスターを固定したままヘッドだけを進め、CHANCEの再抽選とDRIFTの移調を止める。DICE（Confirmタップ）は0–65535の新シードを書き込み、CST SEEDをONにしてプロジェクトをmodifiedにするため、画面表示・発音・保存が同じフレーズを指す。MIXERのMUTE隣にもLOCK／DICEを配置した。DRIFTはループ末尾で更新し、新しい移調は次のループから適用する。プロジェクト形式v11で保存し、v10はLOCK=false、v6–v9は無効状態で移行する。
- **MIDI境界とデータモデル:** トランスポートクロック、音声・入力・MIDI・トリガーのインターフェース。SystemSettingsとProjectDataは独立したバージョン番号を持ち、SystemCapabilitiesによる設定カテゴリの機種別可用性判定を備える。Windows向けにWinMM入力バックエンド、24 PPQNクロック追従、Start/Stop/Continue、SYSTEM MIDIトグルを実装済み。
- **プロジェクト操作:** UIからの保存・読み込み・新規・上書き確認要求をSDLアプリが受け取り、`ProjectStore`がv11形式でシリアライズ／復元する。v10はLOCK=falseで、v6–v9はTuring無効で読み込み時に移行する。トラック別サンプルパスは百分率エンコードして保存し、復元時は相対／絶対パスを解決する。
- **自動テストのソース:** シーケンサーの境界条件、分布、位相、ティックスケジュール、LFO、Jitter、Turingの決定性／動作モード、および入力・UI状態遷移のテストコード。
- **ビルド設定のひな型:** CMake のコア・ライブラリーとテストターゲット、Linux ARM hard-float と AArch64 用のクロスコンパイル設定例。

### 未実装

- **デスクトップ音声接続（Phase 4 開始）:** `src/app/SdlAudioBackend.h`が`IAudioBackend`をSDL2 audio（float32 stereo、周波数／バッファー変更許可）で実装し、SDLバッファーを4096フレーム以下の連続チャンクへ分割して`blockStartFrame`を単調に渡す。`src/app/DesktopAudioEngine.h`が`AudioCallbackProcessor`・`SamplerVoicePool`・`Mixer`・`SamplerMixerRenderer`・パラメーターSPSCキュー・`StepLightBank`を配線する。WAVローダー統合までの暫定として、トラック別の内蔵合成音（キック／ノイズ系／音程付きサイン）を使用する。再生は音声クロック駆動で、UIは`framesRendered`を基準に`StepLightBank`の発音照明を描画する（`--play`起動で確認）。SDL2音声が使えない場合は従来の125ms wall-clockティックによる視覚のみのフォールバックへ戻る。**Sequencer handoff:** UIとaudioはそれぞれ別の`Sequencer`を持つ。UI側の編集は`SequencerSync`が差分を検出し、容量256のSPSC lock-freeキュー（`src/audio/SequencerCommandQueue.h`）経由で、`AudioCallbackProcessor`がブロック先頭で音声側の`Sequencer`へ適用する（デバイスロック不使用。キュー満杯時は未送信分を次回syncで再送）。UI側`Sequencer`の再生位置は進まず、発音照明はイベントキュー由来。テスト追加済み（差分送信・満杯後の収束）。**PortMaster向け整備（未実機検証）:** `--fullscreen`（ネイティブ解像度・カーソル非表示）、`--rate`／`--buffer`（実機で音声設定を調整）、アクセラレーター付きレンダラーが使えない場合のソフトウェア描画フォールバック、Select+Start同時押しでの終了、`./samples`の自動読み込みを追加。CMakeに`PORTMASTER_BUILD`（Release、テスト無効、SDL2必須、libstdc++静的リンク、出力名`pocketeuclidean.<arch>`）を追加。`packaging/portmaster/pocketeuclidean/`にport.json、gameinfo.xml、README.md、起動スクリプト`Pocket Euclidean.sh`（公式ガイドの構成、gptokeybなし）、screenshot.png（PCの640×480描画。実機の画面ではない）、licenses／samplesのひな型を用意。640×480の描画は目視確認済み。AArch64クロスビルド、実機での起動／入力／音声／終了、ライセンスファイル同梱は未実施。 **R36S実機での起動（ユーザー報告）:** 修正版パッケージがR36Sで正常に動作したとの報告を受けた（確認項目の詳細・音声／入力の個別確認結果は未記録）。Brick Proは未確認。 **設定の永続化:** `src/platform/SettingsStore.{h,cpp}`が`settings.cfg`（`PocketEuclideanSettings`ヘッダー、version 4）へテンポ初期値・音声（レート／バッファー／マスター音量）・入力・MIDI（有効／入出力チャンネル／デバイス）・テーマを保存／復元する（厳格パース、temp+rename、不正ファイルは無視して既定値）。`--data`ディレクトリ（PortMasterでは`$GAMEDIR/conf/`）に置かれ、起動時に読み込み、マスター音量は音声開始後にキューへ反映、設定が変化したら即保存する。往復・拒否のテスト追加、Windowsでテーマ（HIGH CONTRAST）とマスター40%の復元を目視確認。R36Sでの書き込みは未確認。 **AArch64クロスビルド（コンパイル確認済み・実行未確認）:** `scripts/build-portmaster-aarch64.sh`がzig 0.14.1（`aarch64-linux-gnu.2.28`）でSDL2（リンク用に2.32.10をソースからビルド）とアプリを`PORTMASTER_BUILD=ON`でビルドし、`pocketeuclidean.aarch64`（ELF 64-bit ARM aarch64、strip済み約760KB、動的依存は`libSDL2-2.0.so.0`・libc・libm・libpthread・libdlのみ、要求glibcは2.17まで、libc++は静的）と、スクリプト実行権付きの`pocketeuclidean.zip`（約300KB）を生成する。警告なし。WSL Ubuntuで確認。**R36S実機での初回起動失敗と修正:** 初版は`symbol lookup error: undefined symbol: __aarch64_ldadd8_relax`で起動しなかった。原因は、リンク用にビルドしたlibSDL2がzigのcompiler-rt（`__aarch64_*`、`__divti3`）をweak exportしており、zigがアプリ側へcompiler-rtを埋め込まずそのSDL側の記号へ動的束縛したこと（実機のlibSDL2にはこれらが無い）。リンク用SDL2をバージョンスクリプトで`SDL_*`のみexportするよう変更し、アプリ側にcompiler-rtが埋め込まれるようにした。ビルドスクリプトは`nm -D`で`__aarch64_*`の動的未解決記号が残っていれば失敗する。修正版のR36S実機での起動は未確認（ホストにaarch64のローダー／ランタイムが無く、ビルド環境では実行できない）。ライセンスファイルは未同梱（プロジェクト自体のライセンスが未決定）。**MIXERデスクトップ表示:** 全8トラックの音量／Pan／Muteとピークメーター、マスター行、選択中の編集項目を表示（メーターは`Mixer`の`PeakMeters`をUIスレッドで読み、即時アタック・約0.3秒リリースで平滑化。`--play --screen 3`で目視確認、1024×768）。**コールバック負荷計測:** `SdlAudioBackend`がチャンクごとの処理時間をバッファー時間比で記録し、終了時に平均／p99／最大／期限超過数をstderrへ出す。Windows開発機のSDL2（48kHz/480フレーム、`--play`、8トラック、60秒）で 5939チャンク、平均4.4%、p99 12%以下、最大20.8%、期限超過0（ビルド種別は既定のまま、Brick Pro実機の値ではない）。聴感・長時間の安定性は未確認。`--samples <dir>`でWAVをトラック1〜8へ読み込める（起動時のみ）。 **プロジェクト保存／読み込み:** `src/project/ProjectStore.{h,cpp}`がテキスト形式（`PocketEuclideanProject`ヘッダー、version 6）でテンポ・全8トラックのリズム／LFO×4／Jitter／Mute／音量／Pan／エンベロープ／フィルターを保存・復元する。パーサーは厳格（不明キー・範囲外の値・版違いは拒否し出力を変更しない）、保存は`.tmp`へ書いてからリネーム、読み込みは作業用Sequencerで検証してから適用する。アプリは`--data <dir>`（既定`./conf`、PortMaster起動スクリプトは`$GAMEDIR/conf/`を指定）の`project.pec`の1スロットを扱い、Project menuのNEW／LOAD／SAVE／SAVE ASと破棄確認に接続（ファイル選択UIは未実装のためLOAD／SAVE ASも同スロット）、起動時に存在すれば自動読み込みし、画面下部に結果（SAVED／LOADED等）を3秒表示する。往復・拒否・上書きのテスト追加済み、`--data`の読み込みと表示（77 BPM、T1=9ステップ）を目視確認。エフェクト設定はSequencer外のため値は保存されるが未接続。システム設定（テーマ・音声など）の永続化、ファイル選択UI、実機での保存先書き込みは未実施。 `pocket_euclidean_app`ターゲットを追加し、SDL2が検出された場合はウィンドウ作成・レンダラー・キーボード／GameController入力の論理アクション変換・7画面の基本描画を行う最小シェルを実装した。SDL2未検出時はheadlessフォールバックで起動し、明示メッセージを表示する。GameController未対応デバイス向けのSDL Joystick fallback（汎用ボタン0-9／ハット）、トリガー軸→L2/R2、抜き差し対応、未認識コントローラーのGUID診断ログ（stderr）を追加。テキスト描画（内蔵5x7ビットマップフォント、`src/app/BitmapFont.h`）を追加し、Mainのテンポ／再生状態／トラックラベル、EDIT／SOUND／MIXER／SYSTEMの項目名と値、MODULATIONの項目名と選択トラックのLFO1／Jitter現在値（`--screen 4`で目視確認、値の編集操作の実機確認は未実施）、Help／Project／破棄確認モーダルを描画する。Mainのデスクトップ描画に加え、`--capture`による1フレーム出力で7画面・Helpモーダル・480×320を目視確認済み（1024×768で項目名／値／選択下線が読める。画面内の文字サイズは統一）。ProjectMenu／破棄確認モーダル・1920×1080・実入力は目視未確認。実音声デバイス初期化は下記「デスクトップ音声接続」を参照。1024×768以外の可読性評価は未実施。
- **実 UI（1024×768基準・Super Flat 7画面描画）:** [UI_DESIGN_POLICY.md](UI_DESIGN_POLICY.md) に準拠する7画面のフラットバー描画を `src/app/main.cpp` に実装済み。MAINの選択トラック強調マーカー・ヘッダー状態表示・発音照明、EDITの上部ライブEuclideanパターンプレビュー＋フラットバー（ELENG/EFILL/EOFFS/VOLUME等）、SOUND・MODULATION・TURING・SYSTEMの単極／双極（Pan・Depth中央基準）／離散トグルのフラットバー、MIXERの音量・Bipolar Pan・ピークメーターを描画する。また音声再生中のUI shadow（`effectiveTrackState`）の画面・プロジェクト保存反映およびMAIN／SYSTEM > CLOCKでのBPM操作を統合済み。Brick Pro実機での目視・フレーム時間測定は未実施。
- **MAIN画面のSDL描画:** 表示モデル・テーマ連動レイアウト・固定長描画コマンドはあるが、1024×768上の文字・背景・行・ステップバーの実描画は未実装。
- **描画側イベント連携:** `AudioCallbackProcessor`から音声フレーム時刻付きイベントを有界SPSCキューへ発行し、ステップごとの照明状態へ渡す基盤を実装済み。実SDL音声デバイスへの接続、描画反映、実機での表示確認は未実装。
- **オーディオ統合:** サンプルスケジューラー、sampler／軽量VA synth voice、Mixer DSP、発音イベントキューをつなぐプラットフォーム非依存経路は実装済み。実SDL音声デバイス、WAVキャッシュからのvoice設定、より高度なシンセDSP、effects、UIからの安全なパラメーター受け渡し、実機レイテンシー調整は未実装。アプリ側はSDL2 Audio APIを使い、Linux（Brick Pro / PortMaster）ではSDL2のALSAバックエンドを第一候補とする。対象のSDL2ビルド設定・ALSAデバイス・対応サンプルレート／バッファーは実機未確認。
- **MIXER画面描画:** 8トラック一覧、フェーダー／パン／MUTE／LOCK／DICE／REV・DLYセンドバー／ピークメーター描画とフォーカス案内。LOCK／DICEはMUTEの直後に並ぶフラットなジオメトリックバッジ（南京錠／サイコロ）で、LOCKは保持状態を本体の塗りで示し、DICEはフォーカス時のみ明度が上がる。センドはポストフェーダーで、専用リバーブ／ディレイバスへ送出する。
- **楽器:** WAVファイルの読み込み・トラック別選択・再生中の差し替え・プロジェクト参照保存は実装済み。残るのは、軽量VA／Plaitsの実機計測・改善、Braids／Plaits候補の比較、より高度なフィルター／callback-safeな設定更新である。
- **エフェクト:** ディレイ、フィルター、エフェクトルーティング。
- **プロジェクト管理:** バージョン付きシリアライズ、`conf/project.pec`への保存・読み込み、v6–v9移行、上書き確認、未保存変更時の Save/Discard/Cancel、サンプルパスの保存・復元は実装済み。残るのは、Save Asの保存先選択UI、OS標準ファイルダイアログ、サンプル欠落時のユーザー向け復旧導線である。
- **SYSTEM設定永続化:** 設定モデル（テーマID・MIDI設定を含む）、Displayテーマ切り替え、カテゴリ可用性判定、能力に応じたカテゴリ移動、`settings.cfg`への保存・読み込み・v2/v3からの移行は実装済み。SystemSettings形式はv4。残るのは、実機での書き込み先確認とMIDIポートのホットプラグ検出である。
- **ミキサー実音声統合:** Mixer DSP、atomic meter、sampler rendererのcallback接続は実装済み。実SDLデバイスとUI描画は未実装。
- **MIDI:** OS/MIDIライブラリー非依存の`IMidiBackend`境界、WinMM入力アダプター、24 PPQNクロック追従（40–240 BPM）、Start/Stop/Continue、SYSTEM MIDIトグル、`--midi-list`／`--midi-device`を実装済み。Linux/PortMasterはnull backendでMIDIカテゴリを隠す。未実装はClock Out、Note Out/In、CC mapping、MIDI Learn、複数ポート同時利用、ホットプラグ、実機でのクロック追従精度確認である。
- **実機固有の入力・保存・配布:** Brick Pro/NextUIを含む各機種のボタンプロファイル、書き込み先、PortMaster起動スクリプト、同梱ランタイム、メタデータ／スクリーンショット／ライセンスを含むパッケージ。
- **非 Linux 対応:** Windows/macOS ネイティブビルドの実証、および非 Linux ハンドヘルドへの移植。

### 検証済み／未検証

- Windows x86-64でLLVM-MinGW Clang 23.1.3、CMake 4.3.2、Ninjaを使ったネイティブCMakeビルドを確認済み。
- **MAINのフッター削除とトラック表示の明確化（2026-10-28）。** MAIN最下部のキーヒント帯を削除し、8レーンの行高を拡張してリズムを画面いっぱいに表示するようにした。MAINヘッダーには選択トラックを`TRK1`…`TRK8`として明示し、EDIT／SOUND／MIXER／MODULATIONのタイトル横にも同一のアクセント付き`TRKn`バッジを追加した。MIXERヘッダーに重複していた`T1`表記を解消し、画面ごとのフッターを文脈依存の短いヒントへ更新した。SOUND／MODULATIONの2列パラメーターは、通常の←→で同一行の左右列へ移動し、A（ゲームパッド）またはCtrl（キーボード）を押しながら←→で値を変更するトラッカー方式に変更した（M8系の操作体系）。Shift併用で微調整。`--inject-keys`へ`ctrl`を追加し、列移動と値変更をGUIキャプチャで確認した。1024×768と640×480のMAIN／SOUND／MODULATIONを確認済み。実機での操作感は未確認。
- **Turing Machineを追加（2026-10-28）。** トラック別のS&Hフレーズ生成を`Screen::Turing`（MAIN→EDIT→SOUND→MIXER→MODULATION→TURING→SYSTEM）として追加した。TEMPO／EUCLID／FREEの3クロック、モード別RATE、LENGTH・CHANCE・RANGE・SCALE・ROOT・GLIDE・DRIFT・SEEDを編集でき、Confirmタップでレジスターを再シードする。音声側は`TickResult.pitchOffsets`をサンプラーのplaybackRateとシンセのpitchHzへ反映し、GLIDEをブロック単位で平滑化する。`ModulationMirror`がライブのhead・32バイト・半音を公開し、TURING画面のレジスター表示とピッチ表示を更新する。プロジェクト形式をv10へ上げ、v6–v9はTuring無効・LENGTH 8で移行する。Windowsでビルドと全テスト（sequencer／UI）が成功し、1024×768と640×480のTURING画面をキャプチャ確認した。実機での音声・操作性は未確認。
- **Turing MachineにフレーズLOCKとDICEを追加（2026-10-28）。** TURINGのENABLE直下にLOCKを置き、ONの間はCHANCEによる再抽選とDRIFTの移調を止めてレジスターを固定する（ヘッドは進み続ける）。Confirm/Aは「レジスターの非永続再シード」から「0–65535の新シードを振るDICE」へ置き換え、SEEDを実パラメーターとして書き換えるため表示・発音・保存が一致する。MIXERのMUTE隣にもLOCK／DICEバッジを追加し、DICEは左右どちらの入力でも振れる。DRIFTの新しい移調が読んだ音に混ざらないよう、ループ末尾の更新を次ループから適用するよう修正した。プロジェクト形式をv11へ上げ、v10のturing行はLOCK=falseで読み込む。sequencerテスト（ロック／DRIFT凍結／v10移行／往復）とUIテスト（TURING LOCK・DICE、MIXER LOCK・DICE）がWindowsで成功し、1024×768と640×480のTURING／MIXERをキャプチャ確認した。実機での操作感・聴感は未確認。
- **Windows自動テストを実行し、CTest 2/2件成功（2026-10-27）。** Plaits全16モデル、モデル範囲拒否、サンプル差し替えキューのACK、v8→v9のモデル／サンプル移行、ファイルブラウザ操作、SOUNDナビゲーションを含む。これはポータブルコアとテスト実行ファイルの確認であり、GUIアプリや実音声デバイスの動作確認ではない。
- **Windows GUIでSOUND画面のMODEL／SAMPLE行、SELECT WAVファイルセレクタ、WAV選択後の表示更新をキャプチャ確認済み（2026-10-27）。** `--play`中のWAV差し替えでcallback failure 0、プロジェクト保存→再読み込みでv9のサンプルパス復元を確認した。聴感・長時間安定性・実機動作は未確認。
- **パラメータ画面の文字サイズを統一（2026-10-27）。** 2列レイアウトのラベル34%／バー26%／値36%へ再配分し、値の余白を広げた。1列レイアウトのバーは54%→44%へ短縮。各行が個別に縮小しないよう、画面ごとに単一の文字倍率を選び、8文字のラベル／値を基準に倍率を決める方式へ変更した。MODELは番号＋4文字名（`1 VA`、`8 HHAT`など）、SAMPLEは8文字までの省略表示、MODULATIONの`LFO SYNC`／`SHIFT`、SEEDの8桁16進表示へ短縮した。1024×768と640×480でSOUND／EDIT／MODULATION／SYSTEM／HELP／PROJECTをキャプチャし、行内で文字倍率が混在しないことを確認した。実機パネルでの視認性は未確認。
- **MAIN／EDITのステップ描画を更新（2026-10-27）。** 音声スレッドが公開するjitter適用後のトリガーマスクを表示し、LFOの`MOD`と並べて`JIT`表示を追加した。あわせて、発音ステップのアクセント上線、休符ステップの内側罫線・中心ドット、再生位置の上下フレームを削除し、各ステップを単一のフラット塗りに統一した。jitterによって挿入またはシフトされたトリガーは、セル幅の半分とアクセント寄りの塗りで表示し、通常のEuclideanステップと区別する。WindowsのキャプチャでjitterのDrop／Insert／Shift／Mixと`JIT`表示を目視確認済み。実機での視認性と描画負荷は未確認。
- 現在の環境で利用可能な ARM bare-metal C++ コンパイラーにより、コア、UI、テストの各 C++ ソースをオブジェクト単位でコンパイルできることを確認済み。CMake の ARM toolchain テンプレートの構文も確認済み。
- SDL2 2.32.10（MinGW開発パッケージ）でWindowsのSDL2アプリがビルドされ、ウィンドウが起動することは確認済み。ただし描画内容、キーボード／コントローラー実入力は目視・実機未確認。
- Windows 上で SDL2 音声を48 kHz／480フレームで開き、3秒間再生してcallback failure 0・発音イベントdrop 0を確認し、発音照明の描画をスクリーンショットで確認した。実際の音の聴感、長時間安定性、レイテンシー、callback処理時間は未測定。
- デスクトップUI／音声（上記以外）、Brick Pro/NextUI/PortMasterのABI・起動・入力・保存・音声・終了、性能と長時間安定性は未検証。
- したがって、現時点で「Pocket Euclideanアプリがデスクトップまたはハンドヘルドで動作確認済み」とは扱わない。
- Brick ProのRSS、サンプルキャッシュ、最大voice負荷、SDL描画時間、ALSA/SDLのcallback deadlineは実測していない。初期予算・合否条件は [PLATFORMS.md](PLATFORMS.md) を参照し、実機プロファイル後にのみ調整する。

## 5. 今後のロードマップ

依存関係を意識し、各段階でテスト可能な成果を作る。

### Phase 1 — 開発ビルドとテストを実行可能にする

- Windows x86-64のコアビルドとCTest（2/2件）は確認済み。別のデスクトップコンパイラー／OS、および選定したSDK/sysrootでのクロスビルドは個別に検証する。
- 可能なら CI にコア・UI のビルドとテストを追加する。
- ARM toolchain ファイルの実際のクロスビルドを、対象とABIを確定したSDK／sysrootで確認する。`linux-armhf.cmake`は32-bit ARM hard-float用であり、Brick Pro向けではない。
- 開発用に音声コールバック時間、プロセスRSS、voice数、描画時間を記録できる計測経路を用意する。リアルタイムコールバック内でログ出力しない。

**完了条件:** デスクトップで自動テストが実行でき、選定したクロスターゲットでも SDK に応じたビルド成否を確認できる。

### Phase 2 — SDL2 プラットフォーム・シェル

- SDL2 の依存検出とアプリ実行ターゲットを追加する。
- SDL2 2D Rendererとメインループを用意し、1024×768のBrick Pro表示を基準に可読性を確認する。異なる画面サイズでは適切にスケールする。
- SDL keyboard / GameController イベントを共通論理アクションへ接続し、未認識コントローラーへの診断と可能な範囲の joystick fallback を設ける。
- デスクトップキーボード操作とコントローラー操作を実機なしで先に確認する。

**進捗（2026-10-23）:** 依存検出付きの`pocket_euclidean_app`を追加し、SDL2がある環境ではウィンドウ起動・入力変換・7画面の基礎描画ループを実行できる。SDL2未導入環境でも`--headless`で起動可能。実機入力プロファイル、SDL2ランタイム配布、オーディオデバイス接続は引き続き未完了。

**完了条件:** GUI を必須とせず起動し、7画面の基本描画と共通入力操作を確認できる。

### Phase 3 — 7画面 UI と操作完成

- Main画面のsnapshot、bounded layout/draw list、step-light event modelは実装済み。次はSDL2描画ホストを接続し、8トラック概要、パターン、再生位置、選択・ミュート状態を描画する。
- EDITのlogical stateと数値範囲は一部実装済み。描画、フォーカス表示、操作ヒントを追加して状態と連動させる。
- 画面遷移、HELP、方向リピート、モーダル優先、操作ヒントを実装する。
- [UI_DESIGN_POLICY.md](UI_DESIGN_POLICY.md) のフラットな視覚言語を7画面に適用し、選択状態を箱型ボタンではなく一貫した明度・文字・下線表現で示す。
- StepLightEnvelope、StepLightBank、およびコールバックからUIへの固定長発音イベントキューは実装済み。SDL描画へ接続し、目標エンベロープ時間を実機・デスクトップで評価する。
- MODULATION / SYSTEM 画面を描画し、SYSTEM内にクロック、MIDI、音声、
  入力、表示、保存、診断カテゴリを提示する。Project Menuとは分離する。
- SYSTEM > Displayのテーマ選択操作を描画へ反映し、意味色4ロールに基づいて全画面を一括切替する。未知のテーマIDは明示的に無効として扱う。
- MIXERに8トラックの音量、Pan、Mute、Reverb/Delayセンド、track/master peak meterを表示する。
- ゲームパッドとキーボードから同一の UI ロジックが呼ばれることを検証する。

**完了条件:** 7画面で状態を確認・操作でき、必要な基本操作がマウスなしで行える。Euclideanパターンが視覚上の主役で、選択・操作状態が装飾枠なしで分かり、発音照明が滑らかかつ描画フレームレート非依存である。1024×768のBrick Pro設計基準で可読性を確認する。

### Phase 4 — オーディオ基盤と最初の演奏可能版

- `AudioCallbackProcessor`、フレーム単位スケジューラー、sampler/synth renderer、Mixer DSPのプラットフォーム非依存経路は実装済み。SDL2の実音声デバイスへの接続はデスクトップ（Windows）で内蔵合成音により実装済み。残りは実WAV・Brick Pro実機。
- Mixer／AD／filter／master volume用の固定長SPSCパラメーターキューは実装済み。残るSequencerパラメーターの手渡し方式を設計し、UI threadとaudio threadの競合を避ける。
- WAVデコード、SampleCache、固定長サンプルvoice pool、voiceのMixer routingは実装済み。WAVローダー／project参照とvoiceのライフタイムを統合する。
- 初期voice上限32、サンプルキャッシュ64 MiB、アプリRSS目標128 MiBを越えない設計とし、超過時の決定的voice stealing／明示的なサンプル拒否を定義する。
- WAV decoderの形式・上限・変換方式は実装済み。実ファイルローダーをコールバック外に実装し、ファイル選択・sample cacheへ統合する。
- 対象機器別にサンプルレート、バッファー、Polyphony を測定・調整する。
- 取得したSDL audio specを使いcallback deadlineを計算し、Brick Proでp99処理時間50%未満／ストレス時最悪80%未満を初期目標として測る。

**完了条件:** 実時間のティックにサンプル再生が同期し、複数トラックと重複ボイスを安全に再生できる。

### Phase 5 — 音源・エフェクトと SOUND 画面

- `ISynthVoice`、固定8スロットの`SynthVoiceRack`、軽量`BasicVaVoice`、サンプラー／シンセ共通AD VCA、トラック別ローパスは実装済み。**Plaits風マルチエンジン実装 (`PlaitsVoice`) を追加済み（16エンジン: VA/WT/FM/Additive/Chord/Speech/BD/SD/HH/Grain/String/Modal/Noise/Particle/PD/WS）。** これらをベースラインとして測定し、Braids／Plaits候補と同じ条件で比較する。外部実装を取り込む前に対象ファイル単位のライセンスを確認する。
- 8トラック同時発音のBrick Pro CPU時間・メモリー・callback期限を測り、予算を満たす最小構成を選定する。測定前にエンジン数や同時voice性能を保証しない。
- SOUND画面のAD／filter／シンセパラメーター操作と、そのaudio queue handoffは実装済み。**シンセエンジンタイプ（BasicVA/Plaits）切替と独立したMODEL選択（16モデル、番号＋短縮名）をSOUND画面に追加済み（プロジェクト保存・読み込み対応、形式v10）。Morphはモデル内の連続パラメーターとして維持。**
- ディレイと、既存の一次ローパスより高度なフィルター／採用する共有・トラック別ルーティングを実装する。
- SOUND 画面で選択中の音源・エフェクトに関係する項目のみを編集可能にする。
- 低負荷機器で CPU 使用率とオーディオ安定性を確認する。

**完了条件:** サンプラー／シンセの両方を各トラックに割り当て、仕様範囲の音源・効果操作ができる。

### Phase 6 — プロジェクト保存とファイル操作

- バージョン付き形式（v11）、入力検証、未知／非対応バージョンのエラーは実装済み。v6–v10の移行も実装済み。
- プロジェクトは`--data`配下の`project.pec`へ保存する。Save Asの保存先選択とOSごとの標準ファイルダイアログは未実装。
- トラック別WAVはファイルセレクタで選択し、v11にパスを保存する。相対／絶対パスの解決と、読み込み失敗時のビルトインフォールバックを実装済み。欠落サンプルのユーザー向け復旧導線は未実装。
- UI のプロジェクト要求を保存層へ接続し、成功した場合のみ変更済み状態を解除する処理は実装済み。
- New / Load / Quit 時の Save / Discard / Cancel を実装し、キャンセルを安全側の初期値としている。

**完了条件:** 8トラックを含む全必須状態を保存・復元し、失敗時にデータを黙って破棄しない。

### Phase 7 — SYSTEM 設定と LFO 変調

- 設定モデルの保存・読み込み・版移行、SYSTEM画面での編集を実装し、
  各プラットフォームで利用可能な設定のみを表示する。
- SYNC/FREEのテンポ挙動、直線／付点／3連符の各周期、6波形、
  決定的Sample-and-Hold、複数LFOのルーティングを検証する。
- JitterのRate境界、再生中設定変更、シフト衝突／折り返しをデスクトップ
  テストランナーで実行し、実音声スケジューラーのコミット窓でも検証する。
- 長さ変調時の位相正規化とmaster transportが維持されることを試験する。

**完了条件:** プロジェクト設定とシステム設定が分離して永続化され、
各トラックの基準値を保った変調が安定する。

### Phase 8 — MIDI クロックとトランスポート同期

- [x] MIDI backend adapterを追加し、Clock InとStart/Stop/Continueを実装。
  WindowsはWinMM、Linux/PortMasterはnull backend。Clock Outは未実装。
- [ ] 外部クロック接続／切断、マスター優先順位、位相の扱いを定義する。
  現状はMIDI入力ONで外部クロックを優先し、OFFで内部クロックへ戻る。
- [x] MIDI未接続時にもstandalone grooveboxとしての動作を維持する。

**完了条件:** MIDI clock / transportを差し替え可能なbackend経由で利用できる。
Clock In／transportは満たす。Clock Outと複数ポート運用は未完了。

### Phase 9 — MIDI ノート、CC、Learn

- Note Out、CC入力とパラメーター割り当て、MIDI Learn、チャンネルと
  ポート設定を段階的に追加する。
- 共通設定モデルとプラットフォーム機能検査を使用する。

**完了条件:** MIDI機能がシーケンサー／UIのOS固有実装に侵入しない。

### Phase 10 — デバイス別クロスビルドと配布

- Brick Pro上のNextUI／PortMaster実装と、選定した各バージョンのPortMaster環境を実機で確認する。
- Allwinner A133PのCPU能力だけでなく、実際のLinuxユーザーランドABI、コンパイラー／sysroot、SDL2のリンクと依存ライブラリーを確認し、AArch64クロスビルドを再現可能にする。
- SDL2の表示・GameController・音声コールバック初期化を実機検証する。48 kHzは前提にせず、デバイスが受理するサンプルレートとバッファーを確認する。
- PortMaster公式Packaging Guideに従い、Launch script、`port.json`、README、ゲーム画面を示す4:3の`640×480`以上のスクリーンショット、必要な実行ファイル／アセット、第三者ライセンスを含める。
- ランチャーから設定／プロジェクトの永続保存先、ログ、必要なPortMaster環境初期化、正常終了とNextUIへの復帰を検証する。
- SDL2／OS共有ライブラリーの依存とライセンスを監査し、対象互換性を確認していないライブラリーを同梱しない。
- デスクトップ Windows / Linux / macOS と、優先する実機ハンドヘルドを個別にビルド・テストする。

**完了条件:** 再現可能ビルド、Brick Pro/NextUIでの実起動、8トラック負荷での長時間音切れ試験、プロジェクト保存／再読込、正常終了／再起動、PortMasterメタデータ・ライセンス一式、および正確なNextUI／PortMasterバージョン記録がそろう。

### Phase 11 — リリース前の信頼性確認

- 8トラック同時トリガー、多数のサンプルボイス、エフェクト負荷、長時間再生を試験する。
- Brick ProでRSS／decoded sample cache／同時voice数／render frame time／audio callback deadlineを記録し、初期resource budgetを満たすことを確認する。
- 再生中編集、画面切り替え、モーダル表示、保存・読み込み失敗からの復帰を確認する。
- Brick Proの1024×768操作性、CPU／メモリー、音切れ、温度・電源条件を確認する。
- 実機でsleep/resume、メニューショートカット、システムボタンの制限を記録し、SDカード上の保存とNextUIへの復帰を確認する。
- 操作説明、ビルド方法、対象機種別の制限と検証履歴を公開する。

**完了条件:** 定義済みの実機・デスクトップ試験項目を満たし、未確認の環境をサポート済みと誤表示しない。

## 6. 優先して解消すべき未決事項

1. Brick Proで試験するNextUIおよびPortMasterの具体的バージョンと、OSユーザーランドABI／SDK／PortMaster互換SDL2の入手方法。
2. SDL2の配布方法と、プラットフォームごとの音声バックエンドおよび実機サンプルレート。
3. SDL2 を使う実行アプリ・描画・音声を、どの段階でコアへ統合するか。
4. ディレイ／フィルターの共有またはトラック別ルーティング。
5. WAV 形式、最大再生時間、サンプルレート変換、同時ボイス上限。
6. 保存場所、ファイル拡張子、プロジェクト形式、サンプル同梱方針。
7. ゲームパッド非対応・未認識時の具体的な再割り当て UI と診断方法。
8. 実機のボタン名・SDL マッピング、オーディオのサンプルレート／バッファー値。
9. Jitter MixでInsert / Drop / Shift個別強度を将来の版でどう露出するか。
10. サブティックShiftを行う場合の音声イベント先読み幅と遅延。
11. MIDI Clock Out併用時の優先クロックと位相、複数入力ポートの選択、ホットプラグ時の再同期。
12. PortMasterガイドの更新を反映した最終パッケージ metadata、機種別起動制約、ライセンスとランタイム依存。

## 7. 参照ドキュメント

- [DECISIONS.md](DECISIONS.md) — 未決事項に対する現在の実装初期値。
- [UI_CONTROLS.md](UI_CONTROLS.md) — 論理アクション、キーボード／コントローラー割り当て、手動確認項目。
- [UI_DESIGN_POLICY.md](UI_DESIGN_POLICY.md) — 7画面のスーパーフラットな視覚言語、リズム表示、照明アニメーション、描画性能、デザイン受け入れ基準。
- [PLATFORMS.md](PLATFORMS.md) — デスクトップと Linux ARM 系のビルド方針・クロスビルド例。
- [MIDI.md](MIDI.md) — MIDI Clock In／トランスポートの構成、設定、制限。
- [ARCHITECTURE.md](ARCHITECTURE.md) — 音楽状態、UI、システム設定、変調、プラットフォーム／MIDI境界と未実装のスレッド安全要件。
- [../README.md](../README.md) — 現状の概要と簡易ビルド案内。
