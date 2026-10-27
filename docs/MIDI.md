# MIDI

## 現在の範囲

Windows デスクトップビルドで、MIDI Clock In とトランスポート入力
（Start / Stop / Continue）を利用できる。

- MIDI 入力の ON/OFF、入出力チャンネル、入力ポート番号は SYSTEM 設定に保存する。
- MIDI 未接続・未設定でも standalone groovebox として動作する。
- Linux / PortMaster ビルドは null backend で、SYSTEM の MIDI カテゴリを表示しない。
- Clock Out、Note Out/In、CC マッピング、MIDI Learn は未実装。

## 構成

```
WinMM callback ──timestamp──> bounded SPSC ring (256)
                                      │
                           UI thread poll (every frame)
                                      │
                          MidiClockFollower (24 PPQN)
                              │                  │
                     transport latch        tempo estimate
                              │                  │
                    Sequencer restart/      Sequencer::setTempo
                    resume/stop                  │
                              └── SequencerSync ──> audio thread
```

- `platform::IMidiBackend` が OS 依存を隔離する。イベントは
  `MidiEvent::timestampMicros`（`steady_clock` の µs、0 は未設定）を持つ。
- `app::SystemMidiBackend` は Windows で WinMM の `midiInOpen` と
  コールバックを使う。コールバック内では確保・ブロック・ログ出力をしない。
  リングが満杯の場合はイベントを破棄する。
- `sequencer::MidiClockFollower` は OS 非依存で、確保もブロックもしない。

## テンポ追従

- 24 PPQN のクロック間隔を直近 24 区間の平均から BPM に変換する。
- 40–240 BPM にクランプする。
- 2 ms 未満または 250 ms 超の間隔、および平均の 3 倍／1/3 未満の外れ値では
  履歴をリセットし、推定値を曲げない。
- 最初の有効な推定値は必ず 1 回通知する。以降は 0.05 BPM 以上の変化で通知する。
- UI スレッドが `Sequencer::setTempo` を呼び、`SequencerSync` が音声側へ
  既存のテンポ差分コマンドとして伝える。

## トランスポート

| MIDI | Sequencer | 意味 |
| --- | --- | --- |
| Start | `restart()` | 先頭（tick 0）から再開 |
| Continue | `resume()` | 現在位置を保持して再開 |
| Stop | `stop()` | 停止。位置は保持 |

- Start / Stop / Continue はラッチされ、UI フレームごとに最新の 1 件を消費する。
  Start は保留中の Stop / Continue を打ち消す。
- `Sequencer` は単調増加する `transportRevision` を持ち、`SequencerSync` は
  play フラグの一致ではなく revision の変化で Start / Continue / Stop を判定する。
  これにより、フレーム間に再生状態が元へ戻ってもエッジが失われない。
- Clock パルスはテンポ推定にのみ使い、ステップの直接スケジュールには使わない。
  トランスポートは Start 受信時に即座に開始する。

## 設定

`settings.cfg` は `SystemSettings` v4。

```
PocketEuclideanSettings
version 4
tempo 120
audio 48000 256 100
input 1 350 120
midi <enabled> <inCh> <outCh> <deviceIndex>
theme 0
```

- v2 / v3 ファイルは MIDI 無効・入力チャンネル 1・デバイス 0 で移行する。
- `deviceIndex` は 0–15。存在しないポートを指定した場合は開けない。
- 入力の有効化はプロジェクトの dirty state を変更しない。

## SYSTEM 画面

- CLOCK の次が MIDI。Left / Right で OFF / ON を切り替える。
- 表示は `OFF`、接続成功で `ON`、有効だがポートを開けない場合 `NO DEV`。
- ポートが後から現れる場合に備え、開けない間は 2 秒ごとに再試行する。

## コマンドライン

```text
pocket_euclidean_app.exe --midi-list
pocket_euclidean_app.exe --midi-device 1
```

- `--midi-list` は入力ポートを `index: name` 形式で表示して終了する。
- `--midi-device` は設定ファイルの `deviceIndex` より優先する（0–15）。

## テスト

`pocket_euclidean_tests` に以下を含む。

- 120 BPM / 125 BPM のクロック推定とテンポ変更のラッチ。
- 長い空白・外れ値による履歴リセット。
- Start / Stop / Continue のラッチと Sequencer の位置保持。
- `SequencerSync` の Start / Stop / Continue コマンド生成。
- 設定 v4 の往復、v2 / v3 からの移行、将来バージョンの拒否。

`pocket_euclidean_ui_tests` は SYSTEM MIDI トグルと dirty state 非変更を確認する。

## 既知の制限

- Windows のみ実バックエンドを持つ。
- Clock Out / Note / CC は `IMidiBackend::send` とイベント解析の土台のみ。
- クロック入力が Stop なしで途切れた場合のタイムアウト停止は未実装。
- 複数入力ポートの同時利用、ホットプラグ時の自動再選択は未実装。
- 機器ごとのクロック揺らぎ・レイテンシーは実機で未計測。
