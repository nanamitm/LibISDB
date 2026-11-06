# MPC Audio Renderer の22.2ch空間出力

対応する MPC Audio Renderer を音声レンダラーとして選択すると、LibISDB は
`IMpcAudioRendererSpatial` の IID を使って対応を検出し、AAC-LATM の22.2chを
専用の24ch PCM形式で渡します。レンダラー名の文字列には依存しません。

対応レンダラーの実装:
[nanamitm/MPC-BE-standalone-filters, 63a64c3a4](https://github.com/nanamitm/MPC-BE-standalone-filters/commit/63a64c3a4)

## 動作

- 24chの各スピーカー位置を検証し、FFmpeg native `AV_CH_LAYOUT_22POINT2` の順に並べ替えます。
- 必要なら48kHzへ変換し、PCM16で出力します。位置が不明な24chには専用形式を付けません。
- 空間出力の使用可否や、使用できない場合の通常出力へのダウンミックスはレンダラーが判断します。
- ステレオなど24ch以外のLATMは通常のPCMで出力し、24chに水増ししません。
- 非対応レンダラー、または中間の音声フィルターを指定した場合は既存のダウンミックス設定を使います。
- `UseNullAudioRenderer` を指定した場合は従来どおり、生のチャンネル順とサンプルレートを保ちます。

`ViewerFilter::OpenSettings::EnableMpcSpatialAudio` の既定値は `true` です。
`false` にすると、対応レンダラーでもLibISDB側のダウンミックスとINIプリセットを使用します。
自動検出が有効な場合、ダウンミックスの担当はレンダラーへ移ります。

`OpenSettings::AudioRendererFilter` に生成済みのフィルターを渡すこともできます。
これは `AudioDevice` より優先され、フィルター登録を行わないアプリケーションやテストで利用できます。
Nullレンダラーを指定した場合は使いません。

## 接続形式

`LibISDB/Windows/Viewer/DirectShow/AudioDecoders/MpcSpatialAudio.hpp` を参照してください。

- Major type: `MEDIATYPE_Audio`
- Subtype: `{3740B29C-02A5-4F75-A8C9-908D9BACB857}`
- Format: `FORMAT_WaveFormatEx` / `WAVEFORMATEXTENSIBLE`
- 24ch、48000Hz、PCM16、valid bits 16、channel mask 0、block alignment 48
- 順序: `FL FR FC LFE BL BR FLC FRC BC SL SR TC TFL TFC TFR TBL TBC TBR LFE2 TSL TSR BFC BFL BFR`

旧版のMPCレンダラーは対応検出に成功しないため、通常の出力が選ばれます。
TVTestなどへの反映には、このLibISDBを使ったアプリケーションの再ビルドと、
対応レンダラーの読み込みが必要です。この変更だけでは既存の実行ファイルや登録情報は更新されません。

## 検証

x64 Releaseをビルドし、対応AXの絶対パスをテストに渡します。テストはフィルター登録や設定保存を行いません。

```powershell
$env:LIBISDB_TEST_MPC_RENDERER = 'F:\path\MpcAudioRenderer-fork.ax'
.\Projects\x64\Release\libisdbtest.exe
```

`[spatial]` のテストでは、独自順序の24chをスピーカー位置から並べ替える処理、
未知の位置の拒否、サンプルレート変換、既存のraw出力・ダウンミックスを検証します。
環境変数を指定すると、実際のAXを読み込み、自動検出の有効・無効、Null出力、
グラフ接続、専用メディアタイプのネゴシエーションも検証します。

放送の22.2ch素材をTVTestで再生する試験、映像との同期、定位・音質、長時間運用は別途確認が必要です。
