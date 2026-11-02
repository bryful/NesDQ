# FF2・FF3の等速音声解析（2026-10-26再開）

優先順位は **戦闘 → ダンジョン → フィールド → その他**。通常の戦闘BGM・攻撃・魔法・被ダメージ・決着、その後に移動中のBGM・地形・扉・場面切替を扱う。低頻度イベントの専用音は後回しにし、全SEへの対応を有効化の前提にしない。ただし未対応音が主要場面のBGMやSEを壊さないことは確認する。

通常版へBGM・主要SEの等速再生を組み込んだ。接続は `app/ff_audio.inc`。対象ROMの識別子と入力プロファイル122／123を維持し、DQ3・DQ4と同じ音声専用コア方式で実行する。

## 再開時点

- FF2：既存のBGM（bank16k 13 `$9800`）、戦闘SE（bank16k 5 `$BA03`開始／`$BA00`更新）、地形音・扉音・ミュート接続を再利用する。詳細は `ff2.md`。
- FF2：bank16k 3の直接APU操作の記録再生は未完。通常戦闘との関係と重なりを実行ログで切り分け、低頻度イベントへの対応を先に広げない。
- FF3：等速音声接続は未実装。FF2の番地や16 KiBバンク構成を流用せず、実行時の8 KiB物理バンクを記録して調査する。

上記は再開時点の状態。組込み後の仕様は以下のとおり。

## 通常版への組込み

FF2は既存のBGM・戦闘SE・メニューSE・地形音・扉音・ミュートの接続を使用。イベント音の記録再生実験は組み込まない。FF3は8 KiBバンク54の `$8000`（初期化）と `$8003`（更新）を原作のバンク切替 `$FB87` 経由で呼ぶ。BGMデータのバンク切替は原作ドライバー自身に任せる。

FF3はゲーム側の `$7F40`・`$7F42`・`$7F43`・`$7F49` への要求書込を転送する。バンク54／55のドライバー自身の書込は転送しない。定期更新は `$801C: STA $4015` の通知（オペランド読取後PC `$801F`）で数える。`ASL $7F40` はRMWのため2回書込となり、更新回数の観測点に使わない。

1ホスト更新の高速側1〜6フレームからBGM更新回数の最大値を採用し、音声側は1フレームだけ実行する。FF2の戦闘SEはゲーム側の終了後も音声側で毎フレーム更新を続ける。連続する同種地形通知はまとめ、再生中の位相は音声側で1ずつ進める。BGM・SE要求がゲーム進行により早く発生すること自体は維持する。

元ROMファイルとゲーム用ROMの命令は変更しない。音声用コピーだけでベクターを接続コードへ向け、FF2の地形音・扉音の末尾を音声側へ戻す。FF3用コードは `tools/generate_ff3_harness.py` から生成する。

ステートは既存の `NESDQST2` に両コア・キュー・速度・音声経路を保存。ゲームのネイティブスナップショットサイズは変えない。0.4.0のゲーム単独＋音声圧縮状態と、以前のゲーム単独形式を読み込む場合はBGMを再開し、圧縮途中の値は検証したうえで破棄する。

低頻度の直接APU操作は等速側へ接続していない。FF2のbank16k 3の専用イベント音、FF3のドライバー外の直接APU操作などは未対応。全場面・全SE対応とは扱わない。

## 共通観測ツール

```powershell
cmake --build build --config Release --target nesdq_ff_audio_probe nesdq_ff2_check -- /p:PreferredToolArchitecture=x64 /m
.\build\Release\nesdq_ff_audio_probe.exe .rom/game.nes 9000 tests/replays/ff2-probe.txt build/checks/ff-audio-resume/ff2
.\build\Release\nesdq_ff_audio_probe.exe .rom/game.nes 9000 tests/replays/ff2-probe.txt build/checks/ff-audio-resume/ff3
.\build\Release\nesdq_ff2_check.exe .rom/game.nes
```

対象は内容照合で機能の接続先を確認したデータだけ。ROM・SRAM・既存ステートを書き換えず、入力リプレイで起動から実行する。リプレイは「開始フレーム・終了フレーム（含まない）・ボタンマスク」。FF2用入力列のFF3への適用は観測ツールの動作確認に限り、同じ場面に到達する根拠にはしない。

- `apu.csv`：APU書込ごとのフレーム、8 KiB物理バンク、オペランド読取後PC、レジスター、値。数値はすべて10進表記。
- `sites.csv`：バンク・PC・レジスターごとの書込数と最初・最後のフレーム。
- 観測コアと観測なしの参照コアを同じ入力で走らせ、毎フレームの音声サンプルと最終映像の完全一致を検査する。

このツールは要求の転送・音声用ドライバー実行・倍速化を行わない。既存のFF2要求捕捉が取りこぼす直接書込も観測する。1個のAPU通知枠を専有するため、音声分離プロファイルと同時には使用しない。

## 優先場面の判定条件

戦闘では開始・通常攻撃・魔法・被ダメージ・勝利／逃走・マップ復帰について、要求入口、ドライバー更新頻度、直接APU操作、BGMとSEの競合を確認する。ダンジョン／フィールドでは移動、扉、地形音、戦闘への出入りを確認する。

実ゲームから捕捉した要求を使い、1〜6倍で音声側が等速で進むこと、連続SEや曲切替で滞留しないこと、保存復元後に音声が一致することを検証する。人工的な要求列のサンプル一致と、実ゲームでの捕捉・聴感確認は区別して記録する。

## 観測ツール追加時の確認結果

- Windows x64 Releaseで新しい共通観測ツールと既存FF2検査をビルド成功。
- FF2・FF3とも上記入力列で9000フレーム完走。観測による音声・最終映像の差なし。書込元（バンク・PC・レジスターの組）はFF2が59、FF3が53。
- FF3では8 KiB物理バンク54、書込後PC `$801F` の `$4015` 書込を8932回観測（フレーム5〜8999）。同じバンクの `$8003` からの逆アセンブルに `$4015` 読取・OR・書込と複数のサブルーチン呼出がある。定期音声処理の候補。固定バンクの `$FA47: JSR $8003` も静的に確認したが、呼出時のバンク対応や更新回数・要求入口は今後照合する。
- 再ビルドしたFF2検査が成功。同一要求列の1〜6倍サンプル一致、BGM表31項目・SE表33項目の各240フレーム、待機SE・地形音・更新停止中の保存復元を確認。
- `python tools/check_ff2_environment.py .rom/game.nes` 成功。地形音2種類×256位相、ミュート、扉音で原作とのAPU書込一致。

ログは `build/ff2-audio-resume.log`、`build/ff3-audio-resume.log`、`build/ff2-audio-resume-regression.log`。ここまでは場面到達を確認する前の観測結果。その後の画像照合と通常版への接続・検証は以下に記録する。

## 組込み後の検証

```powershell
cmake --build build --config Release --target nesdq nesdq_ff_audio_check nesdq_ff_input_check nesdq_dq3_check nesdq_sound_check -- /p:PreferredToolArchitecture=x64 /m:1 /nr:false
.\build\Release\nesdq_ff_audio_check.exe .rom/game.nes
.\build\Release\nesdq_ff_audio_check.exe .rom/game.nes
.\build\Release\nesdq.exe --check .rom/game.nes build/checks/ff2-integrated
.\build\Release\nesdq.exe --check .rom/game.nes build/checks/ff3-integrated
```

- 起動・名前入力等を含む9000フレームで、FF2は157要求／BGM更新7068回、FF3は123要求／更新8983回を捕捉。捕捉あり・なしのゲーム音声と最終映像が完全一致した。
- 同区間の音声エネルギーはFF2がゲーム71100.4／分離70853.2、FF3がゲーム67836.2／分離67857.9。完全な波形・聴感一致を示すものではない。
- ゲームを1〜6フレームずつ進めながら同じホスト時刻に要求を送る検査で、各速度の音声287232サンプルが一致。音声用コアの保存復元後も一致。
- 通常版の `--check` が両作品で成功。2〜6倍での映像・音声の保存復元、5枠、SRAM、旧ネイティブ形式、破損チェックサムの拒否を確認。
- 実際の0.4.0のFF2・FF3ステートを検証用フォルダーへコピーし、SDLの読込操作で両作品とも再開成功。元の検証データは変更していない。
- `tests/replays/ff2-audio-scenes.txt` の6000フレームで両作品の冒頭戦闘、9000フレームでFF2の町とFF3のダンジョンを画像で確認。FF3にも同じ入力列を使用できた。ゲーム全体の戦闘・ダンジョン検証ではない。
- `tests/replays/ff-audio-turbo-scenes.txt` は5500フレーム付近で1→6倍、保存・復元、1倍へ戻す操作を行う。両作品とも7000ホスト更新をSDLのdummy映像・音声で完走。
- FF2・FF3の入力補正検査、DQ3・DQ4の音声回帰検査が成功。
- Windows x64 ReleaseとAndroid arm64 Debug APKをビルド成功。Android実機・聴感、フィールド移動や全戦闘コマンドの網羅は未確認。

Windowsパッケージは `build/ff-audio-package/nesdq.exe`、Android APKは `android/app/build/outputs/apk/debug/app-debug.apk`。
