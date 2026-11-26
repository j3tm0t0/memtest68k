# XEiJ 画面確認用ブリッジ

JDK と XEiJ が必要です。プロジェクトのルートで実行します。

```sh
mkdir -p build/bridge build/emulator
javac -d build/bridge tools/XEiJBridge.java
printf 'Premain-Class: XEiJBridge\n' > build/bridge-manifest.txt
jar cfm build/bridge.jar build/bridge-manifest.txt -C build/bridge .
```

検証用にコピーした Human68k 起動ディスクへ `build/memtest68.x` を入れてから起動します。以下の `XEiJ.jar` とディスクのパスを実環境に合わせてください。

```sh
java -javaagent:"$PWD/build/bridge.jar=$PWD/build/emulator/bridge.cmd" \
  -Djava.io.tmpdir="$PWD/build/emulator" \
  -jar /path/to/XEiJ.jar \
  -ini="$PWD/build/emulator/XEiJ.ini" -saveonexit=off -config=default \
  -model=XVI -memory=4 -boot=fd0 \
  -fd0="$PWD/build/emulator/memtest68.xdf" \
  -flrinformed=on -usecanvas=off -keyboard=none
```

`build/emulator/bridge.cmd` にコマンドを保存すると、ブリッジが読み取り、コマンドファイルを削除します。書き込み途中の読み取りを避けるため、一時ファイルを rename して渡してください。

```sh
printf 'screen %s/build/emulator/screen.png\n' "$PWD" > build/emulator/bridge.tmp
mv build/emulator/bridge.tmp build/emulator/bridge.cmd
```

| コマンド | 動作 |
| --- | --- |
| `screen /absolute/path.png` | エミュレータの実表示パネルを PNG に保存 |
| `key c` | c キーを押して離す |
| `key return` | Enter キーを押して離す |
| `key esc` | ESC キーを押して離す |
| `regs` | エミュレータのレジスタを Java の標準出力へ表示 |
| `dump ADDRESS LENGTH /absolute/path.bin` | デバッグ用にエミュレータ RAM を保存（数値は `0x` 形式も可） |
| `exit` | XEiJ を正常終了 |

キー名は XEiJ の `CONDevice.conKeyMap` に従います。`screen` の画像はモックではなく、実行中の XEiJ パネルが描画した内容です。コマンドファイル・保存先のパスには空白を含めないでください。ディスクへのファイル転送は、XEiJ を終了した状態で行ってください。

低位 RAM 検査の開発時は `fault-low VERIFY_PC END_PC` を使えます。VRAM へ移した検査がパターン 0 の verify 範囲を実行している間に、エミュレータ RAM の `$FFFF` のビット 0 を一度だけ反転します。PC 範囲は `standalone.elf` の `verify` / `good` と `low_probe_image_start` の差に `$E40000` を加えて求めます。60 秒以内に該当しなければ何も変更しません。これは検証専用のホスト側機能で、FD の本体には含みません。
