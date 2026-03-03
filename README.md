# Silk-Codec-Android
Android 平台一站式音频编解码工具，支持主流音频与微信 SILK 格式高速转换。

基于 Silk SDK、dr_libs、stb_vorbis 实现底层解码，JNI 封装，全格式自动识别，支持高采样率 48000Hz。

---

## 🌟 支持功能

### 🔹 音频 → Silk
- MP3 → Silk
- WAV → Silk
- FLAC → Silk
- OGG → Silk
- PCM → Silk
- 自动识别任意音频 → Silk

### 🔹 Silk → 音频
- Silk → MP3

### 🔹 音频 → PCM
- MP3 → PCM
- WAV → PCM
- FLAC → PCM
- OGG → PCM
- 自动识别任意音频 → PCM

### 🔹 工具方法
---

## 📌 支持格式
**输入：**
mp3 / wav / flac / ogg / oga / m4a / aac / amr / pcm / raw / silk / slk

**输出：**
silk / slk / mp3 / pcm / raw

---

## 🎵 采样率支持（最高 48000Hz）
**8000 / 12000 / 16000 / 24000 / 32000 / 44100 / 48000 Hz**

---

## 🧩 快速使用示例
```java
// 初始化编解码器
SilkCodec codec = new SilkCodec();

// 1. 任意音频 → Silk
int result = codec.autoToSilk("/sdcard/test.mp3", "/sdcard/out.silk", 24000);

// 2. Silk → MP3
result = codec.silkToMp3("/sdcard/out.silk", "/sdcard/result.mp3", 24000);

// 3. 任意音频 → PCM
result = codec.autoToPcm("/sdcard/test.wav", "/sdcard/result.pcm");

// 4. PCM → Silk（需传入参数）
result = codec.pcmToSilk("/sdcard/test.pcm", "/sdcard/out.silk", 24000, 48000, 1);

// 5. 获取文件真实类型（文件头识别）
int type = codec.getFileType("/sdcard/somefile");
