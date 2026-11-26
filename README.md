# memtest68k — Memtest68

X68000 / X68030 用のメモリ診断ツールです。Memtest86+ のクラシックな画面を参考に、青い画面へ Pass / Test の進捗、周回数、エラーのアドレス・期待値・実測値を表示します。

Human68k で実行する版と、OS をロードせず FD / SCSI HDD から直接起動する版があります。XVI・68000/16 MHz・12 MB で Human68k / HDS 各 1 周・エラーなしの利用者報告があります（v0.2）。開発環境では XEiJ と run68x で検証しています。

![SCSI HDD から起動し、全 8 項目を一周完了した XEiJ の実画面](docs/screenshots/hds-68030-v03.png)

## 起動形式

| 形式 | ビルド成果物 | 起動方法・検査範囲 | 詳細 |
| --- | --- | --- | --- |
| Human68k | `build/memtest68.x` | DOS から確保できる通常 RAM を検査。OS・常駐ソフト・本体の使用領域は除外 | [操作・引数](docs/human68k.md) |
| FD 単独起動 | `build/memtest68-fd.xdf` | 1232 KiB XDF。68000 は先頭 1 KiB を除外、68030 は通常 RAM 全域を検査 | [FD 起動手順](docs/fd-boot.md) |
| SCSI HDD 単独起動 | `build/memtest68-hd.hds` | 16 MiB HDS。FD 版と同じ検査範囲。Human68k 不要 | [HDS 起動手順](docs/hds-boot.md) |

単独起動版 v0.3 は、`$10000` 以上の通常 RAM に共通の 7 種類の検査を実行します。先頭 64 KiB はテキスト VRAM へ退避し、VRAM 上のコードとスタックで専用の 6 パターンを実行して復元します。68000 は固定ベクタ表の 1 KiB を除いた 63 KiB、68030 は VBR を VRAM に移して 64 KiB 全体を検査します。

| CPU | 検査容量の例 | 未検査の通常 RAM |
| --- | --- | --- |
| 68000 | 1 MiB 構成: 1023 KiB、12 MiB 構成: 12287 KiB | 先頭 1 KiB（CPU ベクタ表） |
| 68030 | 4 MiB 構成: 4096 KiB、12 MiB 構成: 12288 KiB | なし（全項目選択時） |

FD / HDS は診断専用イメージです。OS・ROM・Human68k のファイルシステムは含みません。HDS 版には SCSI ROM IOCS が必要で、SASI / HDF 用ではありません。

## ビルド

必要なもの:

- [elf2x68k](https://github.com/yunkya2/elf2x68k) の `m68k-xelf-gcc` と binutils、Newlib 環境
- GNU make、Python 3
- ホスト側テスト用の C コンパイラ（AddressSanitizer / UndefinedBehaviorSanitizer 対応）

```sh
git clone https://github.com/j3tm0t0/memtest68k.git
cd memtest68k
make
```

`build/` に `.x`・`.xdf`・`.hds` とデバッグ用 ELF を生成します。各起動イメージにはサイズ・チェックサム・SHA-256 を記録した JSON も生成します。

```sh
make fd      # FD 版だけを生成
make hds     # HDS 版だけを生成
make test    # ホスト側テスト
make check   # 全形式のビルドとテスト
```

`build/` は Git の管理対象外です。このリポジトリから各形式をビルドして使用してください。

## CI と headless XEiJ

[GitHub Actions](https://github.com/j3tm0t0/memtest68k/actions/workflows/ci.yml) で全形式をビルドし、ASan / UBSan とディスク生成テストを実行します。続いて Java 26 / Xvfb 上の XEiJ で FD / HDS × 68000 / 68030 の全 8 項目を 1 周実行します。検査容量、低位 RAM の復元、VBR の切替・復元も検証します。68000 の `$1000` と 68030 の `$100` への故障注入では、1 件の検出と復元を確認します。

成果物として X / XDF / HDS、チェックサム JSON、エミュレータの実画面・ログ・結果 JSON を保存します。XEiJ 本体はバージョンと SHA-256 を固定して公式配布元から取得し、成果物には含めません。手元での実行手順は [検証補助ツール](tools/README.md) を参照してください。

## 実行例

Human68k では `memtest68.x` を転送して実行します。

```text
A>memtest68
A>memtest68 --passes 1 --size 1024
```

引数なしでは空き RAM を連続検査します。`--size` は KiB 単位です。検査対象の内容は上書きされます。

HDS 版を XEiJ の SCSI ID 0 から起動する例:

```sh
java -jar /path/to/XEiJ.jar \
  -ini="$PWD/build/hds-preview.ini" -saveonexit=off -config=default \
  -model=XVI -memory=2 -boot=sc0 \
  -sc0="$PWD/build/memtest68-hd.hds" -flrinformed=on
```

FD 版は `-boot=fd0 -fd0="$PWD/build/memtest68-fd.xdf"` を指定します。単独起動版の RAM 容量は SRAM の設定値を使用するため、物理搭載量と設定を合わせてください。

## 主な操作

| キー | 動作 |
| --- | --- |
| `c` | 設定画面。検査項目を切り替える |
| `p` | 一時停止 / 再開 |
| Space / Enter | エラー一覧の表示固定 / 固定解除 |
| ESC（Human68k 版） | 中断して DOS に戻る |
| ESC → `y`（FD 版） | FD から再起動 |
| ESC → `y`（HDS 版） | 起動した SCSI ID から診断本体を再ロード |

設定画面の `w` で Walking の分散方式 / 全 32 ビット方式を切り替えられます。Human68k では `--full-walking` でも従来方式を指定できます。変更は次の Pass から適用します。

設定画面では `1`〜`7`（単独起動版は `8` も）で項目を選択し、Enter で再開します。選択変更は次の Pass から反映します。単独起動版は `l` で周回上限を設定できます。HDS の再ロードは診断プログラムの再スタートで、ハードウェア全体のリセットではありません。

## 検査項目

1. 自身のアドレスとアドレス反転
2. 全 0 / 全 1 の moving inversions
3. チェッカーボード `55555555` / `AAAAAAAA`
4. 分散 Walking bits（既定は 4 走査。従来の全 32 ビット・128 走査も選択可）
5. アドレスと周回 seed による疑似乱数パターン
6. March C-
7. バイト / ワード単位の書き込みと longword 確認
8. 低位 RAM の VRAM 退避検査（単独起動版のみ）

## 速度と Walking の選択

v0.4 は MC68000 用の専用アセンブリループを使用し、進捗通知・エラー報告を内側のループから分離しています。乱数の期待値も 1 KiB の変換表を使って計算し、元のアドレス・seed ごとのパターンを維持します。

Walking は語ごとに検査ビットをずらす分散方式を既定とし、全体の走査数を **166 → 42** に減らしました。各アドレスで全 32 ビットの Walking を実施するには、同じ選択で 32 周連続実行するか、設定 `w` / Human68k `--full-walking` で従来方式を選びます。1 周の Walking の検査内容は従来方式と異なります。

XEiJ XVI / 16 MHz、64 KiB のエンジン測定では、旧 C 版 **55.27 秒**に対し、アセンブリ版は従来 Walking で **7.42 秒**、分散 Walking で **2.30 秒**でした。描画を含まない測定で、実機の所要時間を示す値ではありません。[測定条件・結果・再現手順](docs/performance.md)を参照してください。

## 対応範囲と制限

- MC68000 命令でビルドし、標準 68000 と X68030 を対象としています。
- 68030 のデータキャッシュは無効にして検査します。68040 / 68060 のアクセラレータは未対応です。
- 通常 RAM のみを対象とします。VRAM・SRAM・ハイメモリは診断しません。68000 の単独起動版は先頭 1 KiB を未検査とします。
- 低位検査中の例外は VRAM 上のハンドラで復元を試み、検査を中止します。一般のバスエラーから継続検査へ復帰する機能はありません。
- DRAM の長時間保持試験、DMA 転送試験、故障した SIMM / チップ位置の特定は未実装です。
- エラー 0 でも途中終了した検査は合格を意味しません。エミュレータ上の成功は実機の正常性を保証しません。

## 検証と構成

XEiJ では FD / HDS からの起動、低位 RAM を含む全 8 項目の一周、故障注入時の検出、読込チェックサム、再起動 / 再ロードを確認しました。Human68k 版は run68x でも生成した 68000 バイナリを実行しています。対象構成・検査容量・結果は [検証記録](docs/verification.md) を参照してください。

| ディレクトリ | 内容 |
| --- | --- |
| `src/`・`include/` | 共通検査エンジン、Human68k フロントエンド |
| `boot/` | 自作 FD / HDS ローダ、単独起動フロントエンド、低位 RAM 検査 |
| `tools/` | ディスク生成、任意の [XEiJ 検証補助](tools/README.md) |
| `tests/` | 正常系・故障注入・境界・イメージ構成のホスト側テスト |
| `docs/` | 起動形式ごとの説明、検証記録、実画面 |

画面は Memtest86+ のクラシック UI を参考にした独自実装です。Memtest86 / Memtest86+ の移植版ではありません。
