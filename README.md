# Silk-Codec-Android
支持主流音频与 SILK 格式高速转换。

项目体积极小，完整库仅 500KB 左右。

基于 Silk SDK、dr_libs、stb_vorbis、lame 实现底层解码，JNI 封装，支持格式自动识别，最高支持 48000Hz 采样率。

---

##  支持格式
**输入：**
mp3 / wav / flac / ogg / oga / amr / pcm / raw / silk / slk

**输出：**
silk / slk / mp3 / pcm / raw

---

##  采样率支持
**8000 / 12000 / 16000 / 24000 / 32000 / 44100 / 48000 Hz**

---

## 使用示例
```java
// 初始化编解码器
SilkCodec codec = new SilkCodec();

// 1. 任意格式 → Silk
int result = codec.autoToSilk("/sdcard/test.mp3", "/sdcard/out.silk", 24000);

// 2. Silk → MP3
result = codec.silkToMp3("/sdcard/out.silk", "/sdcard/result.mp3", 24000);

// 3. 任意格式 → PCM
result = codec.autoToPcm("/sdcard/test.wav", "/sdcard/result.pcm");

// 4. PCM → Silk
result = codec.pcmToSilk("/sdcard/test.pcm", "/sdcard/out.silk", 24000, 48000, 1);

// 5. 获取文件真实类型
// 返回值：0=未知 1=Silk 2=MP3 3=WAV 4=FLAC 5=OGG 6=PCM 7=M4A
int type = codec.getFileType("/sdcard/somefile");
```

###  错误码说明
- `0` = 成功
- `-1` = 无法获取文件扩展名
- `-2` = 不支持的音频格式
- `-3` = PCM 转 Silk 需要额外参数，请使用 pcmToSilk
- `-4` = 输入已是 PCM 格式
- `-5` = 输入已是 Silk 格式，无需转换
- `-6` = Silk 转 PCM 请使用 silkToPcm
- `-10` = 输出必须是 .silk 或 .slk
- `-11` = 输出必须是 .mp3
- `-12` = 输出必须是 .pcm 或 .raw
- `-201 ~ -202` = Silk 转 MP3 文件错误
- `-301 ~ -302` = MP3 解码/文件错误
- `-401 ~ -402` = OGG 解码/文件错误
- `-501 ~ -502` = WAV 解码/文件错误
- `-601 ~ -602` = FLAC 解码/文件错误
- `-701 ~ -703` = PCM 参数/文件错误

---

##  开源协议
**Apache License 2.0**

