# NesDQ 1.0.0 リリース

承認されたA案（カセット＋剣）を assets/icon に保存し、Windowsの実行ファイルリソース・ウィンドウアイコン、Androidの従来型／Adaptive Iconへ組み込んだ。原稿・背景透過用原稿は組み込み画像生成ツールで制作。プラットフォーム形式の書出しは tools/generate_icons.py（Pillow）で再生成できる。制作プロンプトは assets/icon/README.md。ウィンドウのリソースアイコン適用はWiizのapp/window_icon.hppを再利用した。

WindowsはMSVCランタイムを静的リンクし、OS標準以外のDLL配置を不要にした。CMakeのVERSIONおよびWindowsバージョンリソースは1.0.0。AndroidはversionName=1.0.0、versionCode=10。Release署名はリポジトリーの同階層のNesDQ_signing/release.propertiesとnesdq-release.p12を読む。署名設定がないReleaseビルドはpreReleaseBuildで停止し、未署名版を配布しない。

署名初期化は tools/initialize_signing.ps1 を使用する。既存鍵・設定は上書きしない。新規鍵はRSA 3072ビット、PKCS12、10000日有効。暗号学的乱数でパスワードを生成し、キーコマンドには環境変数で渡す。秘密情報は標準出力へ出さず、NesDQ_signing内のrelease.propertiesに保存する。鍵・パスワード設定はGitと配布物へ含めない。将来の更新も同じ署名鍵を使う。

配布先は `../NesDQ_app`、署名管理先は `../NesDQ_signing`。NesDQ_appのルートへWindows exe・署名済みarm64 APK・README.MD・licenses・Rom・SHA256SUMSを配置する。ROM、テスト用セーブ、開発ログ、秘密鍵は含めない。正式署名は従来デバッグ署名と異なるため、Androidの移行方法を利用者READMEに記載した。

Windowsの依存DLL一覧がOS標準のみであること、バージョン1.0.0、アイコンの複数サイズ、Androidの署名検証・非debuggable・arm64 ABI・Adaptive Iconを確認する。4作品のWindows --checkを再実行して、映像・音声・5枠の保存復元・SRAMを確認する。Android実機やクリーンな別Windows端末での確認は未実施。

最終結果：Windows ReleaseとAndroid署名済みReleaseのビルド成功。Windows実行ファイルの16／24／32／48／64／128／256pxのアイコンリソースをLoadImageで検査し、OS標準DLLだけに依存することをdumpbinで確認。4作品の--checkが成功。APKはversionName 1.0.0／versionCode 10、非debuggable、arm64-v8a、Adaptive Iconを確認し、APK Signature v1・v2の検証が成功した。配布先の全チェックサム一致と、ROM・セーブ・秘密鍵・署名パスワード設定が含まれないことを確認した。
