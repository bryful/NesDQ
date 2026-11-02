# 再利用したソース

- binjnes: `third_party/binjnes/PROVENANCE.json` の上流コミットとWiizの変更を保持。C11コア、MIT。パレットはMIT-0。DQ4のMapper 1を既存コアで実行する。
- SDL 3.2.28: https://github.com/libsdl-org/SDL/tree/release-3.2.28 。zlibライセンス。OSの描画・音声・ゲームパッドとAndroidのJava接続を利用。
- PicoSHA2: `third_party/picosha2` の既存MIT表示を保持。ROM内容のSHA-256で保存先を分離。
- Wiiz: `app/core_bridge.c` の入力・フレーム実行・SRAM・ステート接続、`app/gamepad_input.hpp` とSDL Android Java接続を再利用。Wizardry固有の音声制御、RAM操作、補助機能は含めない。接続層のWiizCore名称は由来を保持。

ROMは同梱しない。エミュレーターを新規実装せず、UIと接続処理のみを追加する。
ステートはbinjnesのシリアライザーとWiizの描画・音声復元処理を使用し、ビルド間の互換性は保証しない。

この環境のSDLソースにはWiizのSwitch互換修正が適用済み。差分・出典は `third_party/sdl3`、再適用スクリプトは `cmake/PatchSDLSwitch.cmake` に保持する。NesDQの構成処理では共有するWiizのSDLソースを書き換えない。新規取得したSDLでは当該互換修正が未適用になる。


DQ4の倍速はWiizの別コアによる等速音声方式を再利用する。DQ4自身の既存ドライバーをbinjnesで実行し、ROM固有の接続コードだけを追加した。詳細・対応ROM・確認した呼出し位置は `docs/turbo.md`。新しいエミュレーター・APU実装と外部ランタイムの追加はない。
SharedFolder.java・SaveQueue.java・user_storage.hppと縦横タッチ配置もWiizから再利用し、NesDQの名前・保存先へ接続した。

DQ3にも既存binjnesとWiiz由来の音声専用コア方式を再利用。DQ3自身の音声ルーチンを呼び、追加は作品別の接続コードのみ。対象ハッシュと根拠は `docs/dq3.md`。上流コアの追加変更や実行時依存はない。

FF2の等速音声実験はユーザーの指定により中断し、接続コードを `tools/experimental` へ退避した。通常版のFF2・FF3は既存のRAM書込フィルターで入力を補正し、音声はゲーム速度へ連動する。実験用の読み取り専用APU通知（`third_party/binjnes/nesdq-apu-observer.patch`）は通常版では設定しない。現行仕様は `docs/ff-input.md`、過去の調査は `docs/ff2.md`。元ROM・Wiizプロジェクト・SDL共有ソースは変更していない。
