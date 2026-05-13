# Silk-Codec-Android

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Android%2026+-green.svg)](https://developer.android.com)
[![Architecture](https://img.shields.io/badge/Architecture-arm64--v8a-yellow.svg)](#)
[![Min SDK](https://img.shields.io/badge/Min%20SDK-26-orange.svg)](#)
[![Download](https://img.shields.io/badge/Download-JitPack-blueviolet.svg)](https://jitpack.io)
[![JitPack](https://jitpack.io/v/YunJavaPro/Silk-Codec-Android.svg)](https://jitpack.io/#YunJavaPro/Silk-Codec-Android)

> 轻量级 Android 音频编解码库，基于 Silk SDK / dr_libs / stb_vorbis / lame 实现底层解码，JNI 封装，最高支持 48000Hz 采样率。

---

## 特性

| 特性 | 说明 |
|------|------|
| 体积极小 | 完整库仅 ~500KB |
| 高速转换 | 原生 C/C++ 实现 |
| 格式丰富 | mp3 / wav / flac / ogg / silk 等 |
| 自动识别 | 输入输出格式智能检测 |
| 高采样率 | 最高 48000Hz |

---

## 支持格式

| 输入 | 输出 |
|------|------|
| mp3 / wav / flac / ogg / silk / amr / pcm | silk / mp3 / pcm |

**采样率：** 8000 / 12000 / 16000 / 24000 / 32000 / 44100 / 48000 Hz

---

## 快速使用

```java
SilkCodec codec = new SilkCodec();

// 任意格式 → Silk
codec.autoToSilk("/sdcard/input.mp3", "/sdcard/output.silk", 24000);

// Silk → MP3
codec.silkToMp3("/sdcard/input.silk", "/sdcard/output.mp3", 24000);

// 任意格式 → PCM
codec.autoToPcm("/sdcard/input.wav", "/sdcard/output.pcm");

// 获取文件类型
int type = codec.getFileType("/sdcard/somefile");
// 返回：0=未知 1=Silk 2=MP3 3=WAV 4=FLAC 5=OGG 6=PCM 7=M4A 8=AAC

// 获取音频时长（毫秒）
long duration = codec.getDuration("/sdcard/somefile");
```

---

## 错误码

| 返回值 | 含义 |
|:------:|------|
| 0 | 成功 |
| -1 | 无法获取文件扩展名 |
| -2 | 不支持的音频格式 |
| -3 | PCM 转 Silk 需要额外参数 |
| -4 | 输入已经是 PCM 格式 |
| -5 | 输入已经是 Silk 格式 |
| -6 | Silk 转 PCM 请使用 silkToMp3 |
| -10 | 输出必须是 .silk 或 .slk |
| -11 | 输出必须是 .mp3 |
| -12 | 输出必须是 .pcm 或 .raw |
| -13 | 文件格式与方法不匹配 |
| -201 | Silk 转 MP3 文件错误 |
| -202 | Silk 转 MP3 文件错误 |
| -301 | MP3 解码错误 |
| -302 | MP3 文件错误 |
| -401 | OGG 解码错误 |
| -402 | OGG 文件错误 |
| -501 | WAV 解码错误 |
| -502 | WAV 文件错误 |
| -601 | FLAC 解码错误 |
| -602 | FLAC 文件错误 |
| -701 | PCM 参数错误 |
| -702 | PCM 文件错误 |
| -703 | PCM 参数错误 |

---

## 引入依赖

settings.gradle.kts:
```kotlin
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
        maven { url = uri("https://jitpack.io") }
    }
}
```

build.gradle.kts (Module):
```kotlin
dependencies {
    implementation("com.github.YunJavaPro:Silk-Codec-Android:$version")
}
```

---

## 系统要求

- Android SDK 26+
- arm64-v8a

---

## 协议

Apache License 2.0
