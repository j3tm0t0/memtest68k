# headless XEiJ の自動検証

Java 26 以降の JDK、XEiJ 0.26.10.08、付属の `data/` と `jSerialComm-2.11.4.jar`（XEiJ.jar と同じディレクトリ）、Python 3 と `make check` 済みの成果物が必要です。Linux では Xvfb / xauth を使います。XEiJ は AWT を使うため `-Djava.awt.headless=true` では実行できません。

```sh
xvfb-run -a -s '-screen 0 1280x1024x24' python3 tools/test_xeij.py \
  --xeij /path/to/XEiJ.jar --medium fd --cpu 68000
xvfb-run -a -s '-screen 0 1280x1024x24' python3 tools/test_xeij.py \
  --xeij /path/to/XEiJ.jar --medium hds --cpu 68030 --fault
```

`--medium fd|hds`、`--cpu 68000|68030` を組み合わせて実行します。macOS の画面あり環境では `xvfb-run ...` を省略できます。`--java-home` で JDK の場所を指定できます。

各ケースは専用のディスクコピーと設定を使用します。検証のためクロックを 1000 MHz に設定し、68000 は 1 MiB、68030 は 4 MiB を検査します。実時間や表示帯域は実機の性能測定には使えません。

ホスト側 Java エージェントは、VRAM 上の最初の照合命令へ一度だけブレークポイントを置き、実際の検査開始アドレス・VBR・書込済みパターンを確認します。`--fault` はその時点で 68000 の `$1000`、68030 の `$100` を 1 ビットだけ反転します。どちらも v0.3 で検査対象に追加した領域です。通常の `c` → `l` → Enter 操作で周回上限を 1 に設定します。

ターゲットの読み取り専用観測情報 `boot_report` を使い、全 8 項目の周回完了、Walking 方式と走査数、検査容量、低位検査回数、エラー数、64 KiB の復元照合結果、VBR の切替・復元を検証します。故障注入は 1 件の検出とそのアドレスも照合します。配布するブートイメージに故障注入や検証専用の操作機能はありません。

`build/xeij/<ケース>/` に `result.json`、XEiJ の実パネル `screen.png`、`emulator.log` を保存します。タイムアウト、異常終了、観測値の不一致は非ゼロ終了します。`--memory 12` で 12 MiB、`--full-walking` で設定から従来 Walking へ切り替えて 2 周実行できます（1 周目は分散、2 周目は全 32 ビット）。

GitHub Actions ではこの検証を 8 ケース実行し、失敗時も証拠を保存します。

# エンジンの測定・実68000コードの照合

[高速化・測定手順](../docs/performance.md) を参照してください。`make test-native RUN68=/path/to/run68` でアセンブリと C 参照実装の照合を実行できます。

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
