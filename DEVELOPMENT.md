# 開発

C++20／C11、CMake 3.24以降、SDL3、binjnesを使用します。依存ソースの出典と変更は [docs/PROVENANCE.md](docs/PROVENANCE.md) を参照してください。ROM・セーブ・署名鍵はリポジトリに含めません。

## Windows

Visual Studio 2026のC++開発ツールとCMakeを用意し、x64版をビルドします。SDL3は初回構成時に取得します。

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release -- /p:PreferredToolArchitecture=x64 /m:1 /nr:false
.\build\Release\nesdq.exe
```

オフライン構成では `-DFETCHCONTENT_SOURCE_DIR_SDL3=<SDL3ソースの絶対パス>` を追加します。

## Android

JDK 21、Android SDK 35、NDK 30.0.16248370、CMake 4.1.2を用意します。Gradle 8.12は同梱wrapperで取得します。`android/local.properties` にローカルの `sdk.dir` を設定してください。このファイルはGit管理対象外です。

```powershell
cd android
.\gradlew.bat assembleDebug
```

ローカルのSDL3ソースを使う場合は `-PsdlSource=<SDL3ソースの絶対パス>` を指定します。未指定時はCMakeで取得します。

Release署名は既定で `../NesDQ_signing` の `release.properties` と `nesdq-release.p12` を使用します。`tools/initialize_signing.ps1` は `JAVA_HOME` のJDKで新規鍵を作成し、既存鍵を上書きしません。既存アプリの更新には同じ鍵が必要です。秘密鍵・パスワード・ローカル設定は公開しないでください。

## 検証

ROMを必要とする検証は、利用可能なROMのパスを引数で渡します。ROMファイル自体は公開・同梱しません。

```powershell
cmake --build build --config Release --target nesdq_picker_check nesdq_rom_check -- /p:PreferredToolArchitecture=x64 /m:1 /nr:false
.\build\Release\nesdq_picker_check.exe
.\build\Release\nesdq_rom_check.exe "<検証用ROMフォルダー>"
.\build\Release\nesdq.exe --check "<ROMのパス>" build/checks/game
```

作品別の調査・検証方法は `docs/`、入力リプレイは `tests/replays/` にあります。接続コードの再生成にはPythonと `py65`、アイコンの書き出しにはPillowを使用します。アプリ実行時には不要です。

Android実機の全場面の操作、6倍速性能、OS間のステート互換性は未確認です。
