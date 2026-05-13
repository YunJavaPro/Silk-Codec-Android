/**
 * Silk Codec JNI 接口
 *
 * 功能：多种音频格式与 Silk 格式互转
 * 支持：MP3/WAV/FLAC/OGG <-> Silk
 *
 * 注意：输出的 Silk 文件包含微信专属头 (0x02 + #!SILK_V3)
 */

#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==================== Silk SDK 接口 ==================== */
#include "silk/interface/SKP_Silk_SDK_API.h"
#include "silk/interface/SKP_Silk_control.h"
#include "silk/interface/SKP_Silk_typedef.h"

/* ==================== LAME MP3 编码器 ==================== */
#include "lame/lame.h"

/* ==================== dr_libs 音频解码库 ==================== */
#define DR_MP3_IMPLEMENTATION
#include "auto/dr_mp3.h"

#define DR_WAV_IMPLEMENTATION
#include "auto/dr_wav.h"

#define DR_FLAC_IMPLEMENTATION
#include "auto/dr_flac.h"

#undef STB_VORBIS_HEADER_ONLY
#include "auto/stb_vorbis.c"

/* ==================== 常量定义 ==================== */

/**
 * MAX_ARITHM_BYTES - Silk 编码器输出缓冲区最大字节数
 * 
 * Silk 编码器每帧输出的最大字节数。Silk 使用算术编码，
 * 理论上每帧最大输出不会超过 2048 字节。
 * 这个值必须足够大以容纳任何可能的编码输出。
 */
#ifndef MAX_ARITHM_BYTES
#define MAX_ARITHM_BYTES 2048
#endif

/**
 * MAX_API_FS_KHZ - API 支持的最大采样率 (kHz)
 * 
 * Silk SDK 支持的最大采样率为 48kHz。
 * 此值用于计算 PCM 缓冲区大小：最大采样率 × 帧时长(毫秒)
 * 例如：48000 × 20ms / 1000 = 960 个样本每帧
 */
#ifndef MAX_API_FS_KHZ
#define MAX_API_FS_KHZ 48
#endif

/**
 * FRAME_LENGTH_MS - Silk 编码帧时长 (毫秒)
 * 
 * Silk 编码器的标准帧时长为 20ms。
 * 这是 Silk 编码的基本时间单位，每帧包含的样本数为：
 *   样本数 = 采样率 × 帧时长 / 1000
 * 例如：在 44100Hz 采样率下，每帧包含 882 个样本
 * 
 * 注意：此值必须与 Silk SDK 内部设置保持一致，
 * 不建议修改，否则可能导致编码错误。
 */
#ifndef FRAME_LENGTH_MS
#define FRAME_LENGTH_MS 20
#endif

/* ==================== Silk 编码参数配置 ==================== */

/**
 * SILK_BIT_RATE - 目标比特率 (bps)
 * 
 * Silk 编码的目标比特率，影响编码质量和文件大小。
 * - 范围：5000 ~ 100000 bps
 * - 推荐值：
 *   - 语音通话：12000 ~ 20000 bps (窄带语音)
 *   - 音乐/高质量语音：25000 ~ 40000 bps
 *   - 高质量音频：40000+ bps
 * 
 * 较高的比特率提供更好的音质，但会增加文件大小。
 * 25000 bps 是语音和音乐的平衡点，适合微信语音消息。
 */
#define SILK_BIT_RATE 25000

/**
 * SILK_COMPLEXITY - 编码复杂度
 * 
 * 控制编码器的计算复杂度，影响编码速度和质量。
 * - 范围：0 ~ 10
 * - 0：最低复杂度，最快编码，较低质量
 * - 10：最高复杂度，最慢编码，最高质量
 * 
 * 推荐值：
 *   - 实时通话：0 ~ 2 (低延迟优先)
 *   - 文件转换：4 ~ 6 (质量优先)
 *   - 离线处理：8 ~ 10 (最高质量)
 * 
 * 复杂度 2 是移动设备上的推荐值，在质量和性能之间取得平衡。
 */
#define SILK_COMPLEXITY 2

/**
 * SILK_USE_DTX - 不连续传输 (Discontinuous Transmission)
 * 
 * DTX 用于在静音或低能量段减少比特率。
 * - 0：禁用 DTX (推荐用于音乐)
 * - 1：启用 DTX (推荐用于语音通话)
 * 
 * 启用 DTX 时，静音段只发送少量舒适噪声参数，
 * 可以显著降低平均比特率（语音通话可降低 50% 以上）。
 * 
 * 对于文件转换场景，建议禁用 DTX 以保持音频连续性。
 */
#define SILK_USE_DTX 0

/**
 * SILK_USE_IN_BAND_FEC - 带内前向纠错 (Forward Error Correction)
 * 
 * FEC 在当前帧中嵌入前一帧的冗余信息，用于丢包恢复。
 * - 0：禁用 FEC
 * - 1：启用 FEC
 * 
 * 启用 FEC 会增加约 20% 的比特率，但可以在丢包情况下
 * 恢复前一帧的音频内容。
 * 
 * 对于文件转换场景（无丢包），建议禁用 FEC。
 */
#define SILK_USE_IN_BAND_FEC 0

/* ==================== 微信 Silk 文件头 ==================== */
static const unsigned char WECHAT_SILK_HEADER[] = {0x02, '#', '!', 'S', 'I',
                                                   'L',  'K', '_', 'V', '3'};
#define WECHAT_SILK_HEADER_LEN 10

/* ==================== Silk 结束标记 ==================== */
static const unsigned char SILK_TERMINATOR[] = {0xFF, 0xFF};
#define SILK_TERMINATOR_LEN 2

/* ==================== 辅助函数：检测文件实际类型 ==================== */
/**
 * 文件类型常量定义
 */
#define FILE_TYPE_UNKNOWN   0   /* 未知类型 */
#define FILE_TYPE_SILK      1   /* Silk 音频格式 */
#define FILE_TYPE_MP3       2   /* MP3 音频格式 */
#define FILE_TYPE_WAV       3   /* WAV 音频格式 */
#define FILE_TYPE_FLAC      4   /* FLAC 无损音频格式 */
#define FILE_TYPE_OGG       5   /* OGG Vorbis 音频格式 */
#define FILE_TYPE_PCM       6   /* 原始 PCM 数据 */
#define FILE_TYPE_M4A       7   /* M4A 音频格式 */

/**
 * detectFileType - 通过文件头检测文件实际类型
 * 
 * @param path 文件路径
 * @return     文件类型常量（FILE_TYPE_*）
 * 
 * 说明：
 *   此函数通过读取文件头（魔数/签名）来检测文件的实际格式，
 *   而不是依赖文件扩展名。
 * 
 * 支持检测的格式：
 *   - Silk:  以 0x02 + "#!SILK_V3" 或 "#!SILK_V3" 开头
 *   - MP3:   以 0xFF 0xE* (帧同步) 或 "ID3" (ID3标签) 开头
 *   - WAV:   以 "RIFF....WAVE" 开头
 *   - FLAC:  以 "fLaC" 开头
 *   - OGG:   以 "OggS" 开头
 *   - M4A:   以 ftyp 开头（MP4 容器）
 *   - PCM:   无文件头，作为默认返回值
 */
static int detectFileType(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return FILE_TYPE_UNKNOWN;
  
  unsigned char header[32];
  int readLen = fread(header, 1, 32, f);
  fclose(f);
  
  if (readLen < 4) return FILE_TYPE_UNKNOWN;
  
  /* 检测 Silk 格式（微信语音格式） */
  if (readLen >= 10) {
    if (header[0] == 0x02 && memcmp(header + 1, "#!SILK_V3", 9) == 0) {
      return FILE_TYPE_SILK;
    }
    if (memcmp(header, "#!SILK_V3", 9) == 0) {
      return FILE_TYPE_SILK;
    }
  }
  
  /* 检测 MP3 格式 (帧同步: 0xFF 0xE* 或 ID3 标签) */
  if (memcmp(header, "ID3", 3) == 0) {
    return FILE_TYPE_MP3;
  }
  if (header[0] == 0xFF && (header[1] & 0xE0) == 0xE0) {
    return FILE_TYPE_MP3;
  }
  
  /* 检测 WAV 格式 */
  if (readLen >= 12 && memcmp(header, "RIFF", 4) == 0 && memcmp(header + 8, "WAVE", 4) == 0) {
    return FILE_TYPE_WAV;
  }
  
  /* 检测 FLAC 格式 */
  if (memcmp(header, "fLaC", 4) == 0) {
    return FILE_TYPE_FLAC;
  }
  
  /* 检测 OGG Vorbis 格式 */
  if (memcmp(header, "OggS", 4) == 0) {
    return FILE_TYPE_OGG;
  }
  
  /* 检测 M4A 格式 (MP4 容器)
   * MP4 文件结构: [大小(4字节)] + "ftyp" + [品牌标识]
   */
  if (readLen >= 12 && memcmp(header + 4, "ftyp", 4) == 0) {
    return FILE_TYPE_M4A;
  }
  
  /* 无法识别的格式，为原始 PCM 数据 */
  return FILE_TYPE_PCM;
}

extern "C" {

/* =========================================================================
 * 获取文件类型
 * 
 * 通过文件头检测文件实际类型
 * 返回值: 文件类型常量
 *   0 = 未知类型
 *   1 = Silk
 *   2 = MP3
 *   3 = WAV
 *   4 = FLAC
 *   5 = OGG
 *   6 = PCM
 *   7 = M4A
 * ========================================================================= */
JNIEXPORT jint JNICALL Java_me_yun_silk_SilkCodec_getFileType(
    JNIEnv *env, jobject thiz, jstring filePath) {
  const char *path = env->GetStringUTFChars(filePath, 0);
  int type = detectFileType(path);
  env->ReleaseStringUTFChars(filePath, path);
  return type;
}

/* =========================================================================
 * MP3 转 Silk
 * ========================================================================= */
JNIEXPORT jint JNICALL Java_me_yun_silk_SilkCodec_mp3ToSilk(
    JNIEnv *env, jobject thiz, jstring mp3Path, jstring silkPath, jint hz) {
  const char *in_p = env->GetStringUTFChars(mp3Path, 0);
  const char *out_p = env->GetStringUTFChars(silkPath, 0);

  /* ---------- 初始化 MP3 解码器 ---------- */
  drmp3 mp3;
  if (!drmp3_init_file(&mp3, in_p, NULL)) {
    env->ReleaseStringUTFChars(mp3Path, in_p);
    env->ReleaseStringUTFChars(silkPath, out_p);
    return -301;
  }

  /* ---------- 打开输出文件 ---------- */
  FILE *fout = fopen(out_p, "wb");
  if (!fout) {
    drmp3_uninit(&mp3);
    env->ReleaseStringUTFChars(mp3Path, in_p);
    env->ReleaseStringUTFChars(silkPath, out_p);
    return -302;
  }
  setvbuf(fout, NULL, _IOFBF, 65536);

  /* ---------- 写入微信 Silk 文件头 ---------- */
  fwrite(WECHAT_SILK_HEADER, 1, WECHAT_SILK_HEADER_LEN, fout);

  /* ---------- 初始化 Silk 编码器 ---------- */
  SKP_int32 encSize;
  SKP_Silk_SDK_Get_Encoder_Size(&encSize);
  void *psEnc = malloc(encSize);
  SKP_SILK_SDK_EncControlStruct encStatus;
  SKP_Silk_SDK_InitEncoder(psEnc, &encStatus);

  /* ---------- 音频参数配置 (集中设置) ---------- */
  SKP_SILK_SDK_EncControlStruct encCtrl;
  memset(&encCtrl, 0, sizeof(encCtrl));

  int mp3_sample_rate = mp3.sampleRate;
  int mp3_channels = mp3.channels;
  int upsample_factor = 1;
  int api_hz = mp3_sample_rate;

  if (mp3_sample_rate == 22050) {
    api_hz = 44100;
    upsample_factor = 2;
  } else if (mp3_sample_rate == 11025) {
    api_hz = 44100;
    upsample_factor = 4;
  } else if (mp3_sample_rate != 8000 && mp3_sample_rate != 12000 &&
             mp3_sample_rate != 16000 && mp3_sample_rate != 24000 &&
             mp3_sample_rate != 32000 && mp3_sample_rate != 44100 &&
             mp3_sample_rate != 48000) {
    api_hz = 44100;
  }

  int frameSize = (api_hz * FRAME_LENGTH_MS) / 1000;

  encCtrl.API_sampleRate = api_hz;
  encCtrl.maxInternalSampleRate = hz;
  encCtrl.packetSize = frameSize;
  encCtrl.bitRate = SILK_BIT_RATE;
  encCtrl.complexity = SILK_COMPLEXITY;
  encCtrl.useDTX = SILK_USE_DTX;
  encCtrl.useInBandFEC = SILK_USE_IN_BAND_FEC;

/* ---------- PCM 缓冲区 ---------- */
#define READ_FRAMES 1152
  short *pcm_buffer = (short *)malloc(frameSize * 10 * sizeof(short));
  int pcm_buffer_len = 0;
  drmp3_int16 mp3_pcm[READ_FRAMES * 2];

  /* ---------- 编码循环 ---------- */
  while (1) {
    int frames_read = drmp3_read_pcm_frames_s16(&mp3, READ_FRAMES, mp3_pcm);
    if (frames_read <= 0)
      break;

    for (int i = 0; i < frames_read; i++) {
      short sample;
      if (mp3_channels == 2) {
        sample = (short)(((int)mp3_pcm[i * 2] + (int)mp3_pcm[i * 2 + 1]) / 2);
      } else {
        sample = mp3_pcm[i];
      }

      for (int u = 0; u < upsample_factor; u++) {
        pcm_buffer[pcm_buffer_len++] = sample;
      }
    }

    while (pcm_buffer_len >= frameSize) {
      SKP_uint8 outBuf[MAX_ARITHM_BYTES];
      SKP_int16 nBytesOut = MAX_ARITHM_BYTES;

      int ret = SKP_Silk_SDK_Encode(psEnc, &encCtrl, pcm_buffer, frameSize,
                                    outBuf, &nBytesOut);
      if (ret == 0 && nBytesOut > 0) {
        unsigned char len_bytes[2];
        len_bytes[0] = nBytesOut & 0xFF;
        len_bytes[1] = (nBytesOut >> 8) & 0xFF;
        fwrite(len_bytes, 1, 2, fout);
        fwrite(outBuf, 1, nBytesOut, fout);
      }

      pcm_buffer_len -= frameSize;
      memmove(pcm_buffer, pcm_buffer + frameSize,
              pcm_buffer_len * sizeof(short));
    }
  }

  /* ---------- 写入结束标记 ---------- */
  fwrite(SILK_TERMINATOR, 1, SILK_TERMINATOR_LEN, fout);

  /* ---------- 资源释放 ---------- */
  free(pcm_buffer);
  free(psEnc);
  drmp3_uninit(&mp3);
  fclose(fout);

  env->ReleaseStringUTFChars(mp3Path, in_p);
  env->ReleaseStringUTFChars(silkPath, out_p);
  return 0;
}

/* =========================================================================
 * WAV 转 Silk
 * ========================================================================= */
JNIEXPORT jint JNICALL Java_me_yun_silk_SilkCodec_wavToSilk(
    JNIEnv *env, jobject thiz, jstring wavPath, jstring silkPath, jint hz) {
  const char *in_p = env->GetStringUTFChars(wavPath, 0);
  const char *out_p = env->GetStringUTFChars(silkPath, 0);

  /* ---------- 初始化 WAV 解码器 ---------- */
  drwav wav;
  if (!drwav_init_file(&wav, in_p, NULL)) {
    env->ReleaseStringUTFChars(wavPath, in_p);
    env->ReleaseStringUTFChars(silkPath, out_p);
    return -501;
  }

  /* ---------- 打开输出文件 ---------- */
  FILE *fout = fopen(out_p, "wb");
  if (!fout) {
    drwav_uninit(&wav);
    env->ReleaseStringUTFChars(wavPath, in_p);
    env->ReleaseStringUTFChars(silkPath, out_p);
    return -502;
  }

  /* ---------- 写入微信 Silk 文件头 ---------- */
  fwrite(WECHAT_SILK_HEADER, 1, WECHAT_SILK_HEADER_LEN, fout);

  /* ---------- 初始化 Silk 编码器 ---------- */
  SKP_int32 encSize;
  SKP_Silk_SDK_Get_Encoder_Size(&encSize);
  void *psEnc = malloc(encSize);
  SKP_SILK_SDK_EncControlStruct encStatus;
  SKP_Silk_SDK_InitEncoder(psEnc, &encStatus);

  /* ---------- 音频参数配置 (集中设置) ---------- */
  SKP_SILK_SDK_EncControlStruct encCtrl;
  memset(&encCtrl, 0, sizeof(encCtrl));

  int wav_sample_rate = wav.sampleRate;
  int wav_channels = wav.channels;
  int frameSize = (wav_sample_rate * FRAME_LENGTH_MS) / 1000;

  encCtrl.API_sampleRate = wav_sample_rate;
  encCtrl.maxInternalSampleRate = hz;
  encCtrl.packetSize = frameSize;
  encCtrl.bitRate = SILK_BIT_RATE;
  encCtrl.complexity = SILK_COMPLEXITY;
  encCtrl.useDTX = SILK_USE_DTX;
  encCtrl.useInBandFEC = SILK_USE_IN_BAND_FEC;

  /* ---------- PCM 缓冲区 ---------- */
  short *readBuf = (short *)malloc(frameSize * wav_channels * sizeof(short));
  short *monoBuf = (short *)malloc(frameSize * sizeof(short));

  /* ---------- 编码循环 ---------- */
  while (drwav_read_pcm_frames_s16(&wav, frameSize, readBuf) ==
         (drwav_uint64)frameSize) {
    for (int i = 0; i < frameSize; i++) {
      if (wav_channels == 2) {
        monoBuf[i] =
            (short)(((int)readBuf[i * 2] + (int)readBuf[i * 2 + 1]) / 2);
      } else {
        monoBuf[i] = readBuf[i];
      }
    }

    SKP_uint8 outBuf[MAX_ARITHM_BYTES];
    SKP_int16 nBytesOut = MAX_ARITHM_BYTES;

    if (SKP_Silk_SDK_Encode(psEnc, &encCtrl, monoBuf, frameSize, outBuf,
                            &nBytesOut) == 0) {
      unsigned char len_bytes[2];
      len_bytes[0] = nBytesOut & 0xFF;
      len_bytes[1] = (nBytesOut >> 8) & 0xFF;
      fwrite(len_bytes, 1, 2, fout);
      fwrite(outBuf, 1, nBytesOut, fout);
    }
  }

  /* ---------- 写入结束标记 ---------- */
  fwrite(SILK_TERMINATOR, 1, SILK_TERMINATOR_LEN, fout);

  /* ---------- 资源释放 ---------- */
  free(readBuf);
  free(monoBuf);
  free(psEnc);
  drwav_uninit(&wav);
  fclose(fout);

  env->ReleaseStringUTFChars(wavPath, in_p);
  env->ReleaseStringUTFChars(silkPath, out_p);
  return 0;
}

/* =========================================================================
 * FLAC 转 Silk
 * ========================================================================= */
JNIEXPORT jint JNICALL Java_me_yun_silk_SilkCodec_flacToSilk(
    JNIEnv *env, jobject thiz, jstring flacPath, jstring silkPath, jint hz) {
  const char *in_p = env->GetStringUTFChars(flacPath, 0);
  const char *out_p = env->GetStringUTFChars(silkPath, 0);

  /* ---------- 初始化 FLAC 解码器 ---------- */
  drflac *pFlac = drflac_open_file(in_p, NULL);
  if (!pFlac) {
    env->ReleaseStringUTFChars(flacPath, in_p);
    env->ReleaseStringUTFChars(silkPath, out_p);
    return -601;
  }

  /* ---------- 打开输出文件 ---------- */
  FILE *fout = fopen(out_p, "wb");
  if (!fout) {
    drflac_close(pFlac);
    env->ReleaseStringUTFChars(flacPath, in_p);
    env->ReleaseStringUTFChars(silkPath, out_p);
    return -602;
  }

  /* ---------- 写入微信 Silk 文件头 ---------- */
  fwrite(WECHAT_SILK_HEADER, 1, WECHAT_SILK_HEADER_LEN, fout);

  /* ---------- 初始化 Silk 编码器 ---------- */
  SKP_int32 encSize;
  SKP_Silk_SDK_Get_Encoder_Size(&encSize);
  void *psEnc = malloc(encSize);
  SKP_SILK_SDK_EncControlStruct encStatus;
  SKP_Silk_SDK_InitEncoder(psEnc, &encStatus);

  /* ---------- 音频参数配置 (集中设置) ---------- */
  SKP_SILK_SDK_EncControlStruct encCtrl;
  memset(&encCtrl, 0, sizeof(encCtrl));

  int flac_sample_rate = pFlac->sampleRate;
  int flac_channels = pFlac->channels;
  int frameSize = (flac_sample_rate * FRAME_LENGTH_MS) / 1000;

  encCtrl.API_sampleRate = flac_sample_rate;
  encCtrl.maxInternalSampleRate = hz;
  encCtrl.packetSize = frameSize;
  encCtrl.bitRate = SILK_BIT_RATE;
  encCtrl.complexity = SILK_COMPLEXITY;
  encCtrl.useDTX = SILK_USE_DTX;
  encCtrl.useInBandFEC = SILK_USE_IN_BAND_FEC;

  /* ---------- PCM 缓冲区 ---------- */
  short *readBuf = (short *)malloc(frameSize * flac_channels * sizeof(short));
  short *monoBuf = (short *)malloc(frameSize * sizeof(short));

  /* ---------- 编码循环 ---------- */
  while (drflac_read_pcm_frames_s16(pFlac, frameSize, readBuf) ==
         (drflac_uint64)frameSize) {
    for (int i = 0; i < frameSize; i++) {
      if (flac_channels == 2) {
        monoBuf[i] =
            (short)(((int)readBuf[i * 2] + (int)readBuf[i * 2 + 1]) / 2);
      } else {
        monoBuf[i] = readBuf[i];
      }
    }

    SKP_uint8 outBuf[MAX_ARITHM_BYTES];
    SKP_int16 nBytesOut = MAX_ARITHM_BYTES;

    if (SKP_Silk_SDK_Encode(psEnc, &encCtrl, monoBuf, frameSize, outBuf,
                            &nBytesOut) == 0) {
      unsigned char len_bytes[2];
      len_bytes[0] = nBytesOut & 0xFF;
      len_bytes[1] = (nBytesOut >> 8) & 0xFF;
      fwrite(len_bytes, 1, 2, fout);
      fwrite(outBuf, 1, nBytesOut, fout);
    }
  }

  /* ---------- 写入结束标记 ---------- */
  fwrite(SILK_TERMINATOR, 1, SILK_TERMINATOR_LEN, fout);

  /* ---------- 资源释放 ---------- */
  free(readBuf);
  free(monoBuf);
  free(psEnc);
  drflac_close(pFlac);
  fclose(fout);

  env->ReleaseStringUTFChars(flacPath, in_p);
  env->ReleaseStringUTFChars(silkPath, out_p);
  return 0;
}

/* =========================================================================
 * OGG 转 Silk
 * ========================================================================= */
JNIEXPORT jint JNICALL Java_me_yun_silk_SilkCodec_oggToSilk(
    JNIEnv *env, jobject thiz, jstring oggPath, jstring silkPath, jint hz) {
  const char *in_p = env->GetStringUTFChars(oggPath, 0);
  const char *out_p = env->GetStringUTFChars(silkPath, 0);

  /* ---------- 初始化 OGG 解码器 ---------- */
  int error;
  stb_vorbis *v = stb_vorbis_open_filename(in_p, &error, NULL);
  if (!v) {
    env->ReleaseStringUTFChars(oggPath, in_p);
    env->ReleaseStringUTFChars(silkPath, out_p);
    return -401;
  }

  stb_vorbis_info info = stb_vorbis_get_info(v);
  int ogg_sample_rate = info.sample_rate;
  int ogg_channels = info.channels;

  /* ---------- 打开输出文件 ---------- */
  FILE *fout = fopen(out_p, "wb");
  if (!fout) {
    stb_vorbis_close(v);
    env->ReleaseStringUTFChars(oggPath, in_p);
    env->ReleaseStringUTFChars(silkPath, out_p);
    return -402;
  }
  setvbuf(fout, NULL, _IOFBF, 65536);

  /* ---------- 写入微信 Silk 文件头 ---------- */
  fwrite(WECHAT_SILK_HEADER, 1, WECHAT_SILK_HEADER_LEN, fout);

  /* ---------- 初始化 Silk 编码器 ---------- */
  SKP_int32 encSize;
  SKP_Silk_SDK_Get_Encoder_Size(&encSize);
  void *psEnc = malloc(encSize);
  SKP_SILK_SDK_EncControlStruct encStatus;
  SKP_Silk_SDK_InitEncoder(psEnc, &encStatus);

  /* ---------- 音频参数配置 ---------- */
  SKP_SILK_SDK_EncControlStruct encCtrl;
  memset(&encCtrl, 0, sizeof(encCtrl));

  /* 直接使用 OGG 原始采样率，Silk 编码器会自动处理 */
  int frameSize = (ogg_sample_rate * FRAME_LENGTH_MS) / 1000;

  encCtrl.API_sampleRate = ogg_sample_rate;
  encCtrl.maxInternalSampleRate = 24000;
  encCtrl.packetSize = frameSize;
  encCtrl.bitRate = SILK_BIT_RATE;
  encCtrl.complexity = SILK_COMPLEXITY;
  encCtrl.useDTX = SILK_USE_DTX;
  encCtrl.useInBandFEC = SILK_USE_IN_BAND_FEC;

  /* ---------- PCM 缓冲区 ---------- */
#define OGG_READ_FRAMES 4096
  short *readBuf = (short *)malloc(OGG_READ_FRAMES * ogg_channels * sizeof(short));
  short *pcmBuffer = (short *)malloc(frameSize * 2 * sizeof(short));
  int pcmBufferLen = 0;

  /* ---------- 编码循环 ---------- */
  while (1) {
    int n = stb_vorbis_get_samples_short_interleaved(v, ogg_channels, readBuf, OGG_READ_FRAMES * ogg_channels);
    if (n <= 0) break;

    for (int i = 0; i < n; i++) {
      short sample;
      if (ogg_channels == 2) {
        sample = (short)(((int)readBuf[i * 2] + (int)readBuf[i * 2 + 1]) / 2);
      } else {
        sample = readBuf[i];
      }
      pcmBuffer[pcmBufferLen++] = sample;
    }

    while (pcmBufferLen >= frameSize) {
      SKP_uint8 outBuf[MAX_ARITHM_BYTES];
      SKP_int16 nBytesOut = MAX_ARITHM_BYTES;

      if (SKP_Silk_SDK_Encode(psEnc, &encCtrl, pcmBuffer, frameSize, outBuf,
                              &nBytesOut) == 0) {
        unsigned char len_bytes[2];
        len_bytes[0] = nBytesOut & 0xFF;
        len_bytes[1] = (nBytesOut >> 8) & 0xFF;
        fwrite(len_bytes, 1, 2, fout);
        fwrite(outBuf, 1, nBytesOut, fout);
      }

      pcmBufferLen -= frameSize;
      memmove(pcmBuffer, pcmBuffer + frameSize, pcmBufferLen * sizeof(short));
    }
  }

  /* ---------- 写入结束标记 ---------- */
  fwrite(SILK_TERMINATOR, 1, SILK_TERMINATOR_LEN, fout);

  /* ---------- 资源释放 ---------- */
  free(readBuf);
  free(pcmBuffer);
  free(psEnc);
  stb_vorbis_close(v);
  fclose(fout);

  env->ReleaseStringUTFChars(oggPath, in_p);
  env->ReleaseStringUTFChars(silkPath, out_p);
  return 0;
}

/* =========================================================================
 * PCM 转 Silk
 * 
 * 参数说明:
 *   pcmPath  - 输入 PCM 文件路径
 *   silkPath - 输出 Silk 文件路径
 *   hz       - Silk 编码内部采样率 (8000/12000/16000/24000/32000/44100/48000)
 *   pcmHz    - 输入 PCM 文件采样率
 *   channels - 输入 PCM 文件声道数 (1=单声道, 2=立体声)
 * 
 * PCM 格式要求:
 *   - 16-bit 有符号整数 (little-endian)
 *   - 无文件头，原始 PCM 数据
 * ========================================================================= */
JNIEXPORT jint JNICALL Java_me_yun_silk_SilkCodec_pcmToSilk(
    JNIEnv *env, jobject thiz, jstring pcmPath, jstring silkPath, jint hz, jint pcmHz, jint channels) {
  const char *in_p = env->GetStringUTFChars(pcmPath, 0);
  const char *out_p = env->GetStringUTFChars(silkPath, 0);

  if (pcmHz <= 0 || channels <= 0 || channels > 2) {
    env->ReleaseStringUTFChars(pcmPath, in_p);
    env->ReleaseStringUTFChars(silkPath, out_p);
    return -701;
  }

  /* ---------- 打开输入文件 ---------- */
  FILE *fin = fopen(in_p, "rb");
  if (!fin) {
    env->ReleaseStringUTFChars(pcmPath, in_p);
    env->ReleaseStringUTFChars(silkPath, out_p);
    return -702;
  }
  setvbuf(fin, NULL, _IOFBF, 65536);

  /* ---------- 打开输出文件 ---------- */
  FILE *fout = fopen(out_p, "wb");
  if (!fout) {
    fclose(fin);
    env->ReleaseStringUTFChars(pcmPath, in_p);
    env->ReleaseStringUTFChars(silkPath, out_p);
    return -703;
  }
  setvbuf(fout, NULL, _IOFBF, 65536);

  /* ---------- 写入微信 Silk 文件头 ---------- */
  fwrite(WECHAT_SILK_HEADER, 1, WECHAT_SILK_HEADER_LEN, fout);

  /* ---------- 初始化 Silk 编码器 ---------- */
  SKP_int32 encSize;
  SKP_Silk_SDK_Get_Encoder_Size(&encSize);
  void *psEnc = malloc(encSize);
  SKP_SILK_SDK_EncControlStruct encStatus;
  SKP_Silk_SDK_InitEncoder(psEnc, &encStatus);

  /* ---------- 音频参数配置 ---------- */
  SKP_SILK_SDK_EncControlStruct encCtrl;
  memset(&encCtrl, 0, sizeof(encCtrl));

  int upsample_factor = 1;
  int api_hz = pcmHz;

  if (pcmHz == 22050) {
    api_hz = 44100;
    upsample_factor = 2;
  } else if (pcmHz == 11025) {
    api_hz = 44100;
    upsample_factor = 4;
  } else if (pcmHz != 8000 && pcmHz != 12000 &&
             pcmHz != 16000 && pcmHz != 24000 &&
             pcmHz != 32000 && pcmHz != 44100 &&
             pcmHz != 48000) {
    api_hz = 44100;
  }

  int frameSize = (api_hz * FRAME_LENGTH_MS) / 1000;

  encCtrl.API_sampleRate = api_hz;
  encCtrl.maxInternalSampleRate = hz;
  encCtrl.packetSize = frameSize;
  encCtrl.bitRate = SILK_BIT_RATE;
  encCtrl.complexity = SILK_COMPLEXITY;
  encCtrl.useDTX = SILK_USE_DTX;
  encCtrl.useInBandFEC = SILK_USE_IN_BAND_FEC;

  /* ---------- PCM 缓冲区 ---------- */
#define PCM_READ_FRAMES 4096
  short *readBuf = (short *)malloc(PCM_READ_FRAMES * channels * sizeof(short));
  short *pcmBuffer = (short *)malloc(frameSize * 10 * sizeof(short));
  int pcmBufferLen = 0;

  /* ---------- 编码循环 ---------- */
  while (1) {
    int n = fread(readBuf, sizeof(short) * channels, PCM_READ_FRAMES, fin);
    if (n <= 0)
      break;

    for (int i = 0; i < n; i++) {
      short sample;
      if (channels == 2) {
        sample = (short)(((int)readBuf[i * 2] + (int)readBuf[i * 2 + 1]) / 2);
      } else {
        sample = readBuf[i];
      }

      for (int u = 0; u < upsample_factor; u++) {
        pcmBuffer[pcmBufferLen++] = sample;
      }
    }

    while (pcmBufferLen >= frameSize) {
      SKP_uint8 outBuf[MAX_ARITHM_BYTES];
      SKP_int16 nBytesOut = MAX_ARITHM_BYTES;

      if (SKP_Silk_SDK_Encode(psEnc, &encCtrl, pcmBuffer, frameSize, outBuf,
                              &nBytesOut) == 0) {
        unsigned char len_bytes[2];
        len_bytes[0] = nBytesOut & 0xFF;
        len_bytes[1] = (nBytesOut >> 8) & 0xFF;
        fwrite(len_bytes, 1, 2, fout);
        fwrite(outBuf, 1, nBytesOut, fout);
      }

      pcmBufferLen -= frameSize;
      memmove(pcmBuffer, pcmBuffer + frameSize, pcmBufferLen * sizeof(short));
    }
  }

  /* ---------- 写入结束标记 ---------- */
  fwrite(SILK_TERMINATOR, 1, SILK_TERMINATOR_LEN, fout);

  /* ---------- 资源释放 ---------- */
  free(readBuf);
  free(pcmBuffer);
  free(psEnc);
  fclose(fin);
  fclose(fout);

  env->ReleaseStringUTFChars(pcmPath, in_p);
  env->ReleaseStringUTFChars(silkPath, out_p);
  return 0;
}

/* =========================================================================
 * Silk 转 MP3
 * ========================================================================= */
JNIEXPORT jint JNICALL Java_me_yun_silk_SilkCodec_silkToMp3(
    JNIEnv *env, jobject thiz, jstring silkPath, jstring mp3Path, jint hz) {
  const char *in_p = env->GetStringUTFChars(silkPath, 0);
  const char *out_p = env->GetStringUTFChars(mp3Path, 0);

  /* ---------- 打开输入文件 ---------- */
  FILE *fin = fopen(in_p, "rb");
  if (!fin) {
    env->ReleaseStringUTFChars(silkPath, in_p);
    env->ReleaseStringUTFChars(mp3Path, out_p);
    return -201;
  }

  /* ---------- 打开输出文件 ---------- */
  FILE *fout = fopen(out_p, "wb");
  if (!fout) {
    fclose(fin);
    env->ReleaseStringUTFChars(silkPath, in_p);
    env->ReleaseStringUTFChars(mp3Path, out_p);
    return -202;
  }

  setvbuf(fin, NULL, _IOFBF, 65536);
  setvbuf(fout, NULL, _IOFBF, 65536);

  /* ---------- 解析 Silk 文件头 ---------- */
  char head_buf[16];
  int read_len = fread(head_buf, 1, 16, fin);
  int data_start = 0;

  for (int i = 0; i <= read_len - 9; i++) {
    if (memcmp(head_buf + i, "#!SILK_V3", 9) == 0) {
      data_start = i + 9;
      break;
    }
  }
  fseek(fin, data_start, SEEK_SET);

  /* ---------- 初始化 Silk 解码器 ---------- */
  SKP_int32 decSize;
  SKP_Silk_SDK_Get_Decoder_Size(&decSize);
  void *psDec = malloc(decSize);
  SKP_Silk_SDK_InitDecoder(psDec);

  /* ---------- 解码参数配置 ---------- */
  SKP_SILK_SDK_DecControlStruct decCtrl;
  memset(&decCtrl, 0, sizeof(decCtrl));
  decCtrl.API_sampleRate = hz;

  /* ---------- 初始化 LAME MP3 编码器 ---------- */
  lame_t lame = lame_init();
  lame_set_in_samplerate(lame, hz);
  lame_set_num_channels(lame, 1);
  lame_set_out_samplerate(lame, hz);
  lame_set_mode(lame, MONO);
  lame_set_VBR(lame, vbr_off);
  lame_set_brate(lame, 24);
  lame_init_params(lame);

  /* ---------- 缓冲区 ---------- */
  SKP_int16 nBytesIn;
  SKP_uint8 inBuf[MAX_ARITHM_BYTES];
  SKP_int16 pcmBuf[MAX_API_FS_KHZ * FRAME_LENGTH_MS];
  unsigned char mp3Buf[8192];

  /* ---------- 解码循环 ---------- */
  while (fread(&nBytesIn, 2, 1, fin) == 1) {
    if (nBytesIn <= 0 || nBytesIn > MAX_ARITHM_BYTES)
      break;
    fread(inBuf, 1, nBytesIn, fin);

    SKP_int16 nSamplesOut;
    if (SKP_Silk_SDK_Decode(psDec, &decCtrl, 0, inBuf, (SKP_int)nBytesIn,
                            pcmBuf, &nSamplesOut) == 0) {
      int wrote =
          lame_encode_buffer(lame, pcmBuf, NULL, nSamplesOut, mp3Buf, 8192);
      if (wrote > 0)
        fwrite(mp3Buf, 1, wrote, fout);
    }

    while (decCtrl.moreInternalDecoderFrames) {
      if (SKP_Silk_SDK_Decode(psDec, &decCtrl, 0, NULL, 0, pcmBuf,
                              &nSamplesOut) == 0) {
        int wrote =
            lame_encode_buffer(lame, pcmBuf, NULL, nSamplesOut, mp3Buf, 8192);
        if (wrote > 0)
          fwrite(mp3Buf, 1, wrote, fout);
      }
    }
  }

  /* ---------- 刷新 LAME 缓冲区 ---------- */
  int wrote = lame_encode_flush(lame, mp3Buf, 8192);
  if (wrote > 0)
    fwrite(mp3Buf, 1, wrote, fout);

  /* ---------- 资源释放 ---------- */
  lame_close(lame);
  free(psDec);
  fclose(fin);
  fclose(fout);

  env->ReleaseStringUTFChars(silkPath, in_p);
  env->ReleaseStringUTFChars(mp3Path, out_p);
  return 0;
}

/* =========================================================================
 * 自动识别音频格式并转 Silk (统一入口)
 * 
 * 支持格式: MP3, WAV, FLAC, OGG, M4A, PCM
 * 返回值: 0=成功, 负数=错误码
 * 
 * 注意: PCM 格式需要指定采样率和声道数，请使用 pcmToSilk 方法
 *       此函数通过文件头自动检测输入格式，不依赖扩展名
 * ========================================================================= */
JNIEXPORT jint JNICALL Java_me_yun_silk_SilkCodec_autoToSilk(
    JNIEnv *env, jobject thiz, jstring audioPath, jstring silkPath, jint hz) {
  const char *in_p = env->GetStringUTFChars(audioPath, 0);
  const char *out_p = env->GetStringUTFChars(silkPath, 0);

  int inputType = detectFileType(in_p);
  jint result = 0;

  switch (inputType) {
    case FILE_TYPE_SILK:
      result = -5;
      break;
    case FILE_TYPE_MP3:
      result = Java_me_yun_silk_SilkCodec_mp3ToSilk(env, thiz, audioPath, silkPath, hz);
      break;
    case FILE_TYPE_WAV:
      result = Java_me_yun_silk_SilkCodec_wavToSilk(env, thiz, audioPath, silkPath, hz);
      break;
    case FILE_TYPE_FLAC:
      result = Java_me_yun_silk_SilkCodec_flacToSilk(env, thiz, audioPath, silkPath, hz);
      break;
    case FILE_TYPE_OGG:
      result = Java_me_yun_silk_SilkCodec_oggToSilk(env, thiz, audioPath, silkPath, hz);
      break;
    case FILE_TYPE_M4A:
      result = -14;
      break;
    case FILE_TYPE_PCM:
      result = -3;
      break;
    default:
      result = -2;
      break;
  }

  env->ReleaseStringUTFChars(audioPath, in_p);
  env->ReleaseStringUTFChars(silkPath, out_p);
  return result;
}

/* =========================================================================
 * MP3 转 PCM
 * ========================================================================= */
JNIEXPORT jint JNICALL Java_me_yun_silk_SilkCodec_mp3ToPcm(
    JNIEnv *env, jobject thiz, jstring mp3Path, jstring pcmPath) {
  const char *in_p = env->GetStringUTFChars(mp3Path, 0);
  const char *out_p = env->GetStringUTFChars(pcmPath, 0);

  /* ---------- 初始化 MP3 解码器 ---------- */
  drmp3 mp3;
  if (!drmp3_init_file(&mp3, in_p, NULL)) {
    env->ReleaseStringUTFChars(mp3Path, in_p);
    env->ReleaseStringUTFChars(pcmPath, out_p);
    return -301;
  }

  /* ---------- 打开输出文件 ---------- */
  FILE *fout = fopen(out_p, "wb");
  if (!fout) {
    drmp3_uninit(&mp3);
    env->ReleaseStringUTFChars(mp3Path, in_p);
    env->ReleaseStringUTFChars(pcmPath, out_p);
    return -302;
  }
  setvbuf(fout, NULL, _IOFBF, 65536);

  /* ---------- 音频参数 ---------- */
  int mp3_channels = mp3.channels;
  int mp3_sample_rate = mp3.sampleRate;

  /* ---------- PCM 缓冲区 ---------- */
  #define PCM_READ_FRAMES 4096
  drmp3_int16 pcm_buf[PCM_READ_FRAMES * 2];
  short mono_buf[PCM_READ_FRAMES];

  /* ---------- 解码循环 ---------- */
  while (1) {
    int frames_read = drmp3_read_pcm_frames_s16(&mp3, PCM_READ_FRAMES, pcm_buf);
    if (frames_read <= 0) break;

    if (mp3_channels == 2) {
      for (int i = 0; i < frames_read; i++) {
        mono_buf[i] = (short)(((int)pcm_buf[i * 2] + (int)pcm_buf[i * 2 + 1]) / 2);
      }
      fwrite(mono_buf, sizeof(short), frames_read, fout);
    } else {
      fwrite(pcm_buf, sizeof(short), frames_read, fout);
    }
  }

  /* ---------- 资源释放 ---------- */
  drmp3_uninit(&mp3);
  fclose(fout);

  env->ReleaseStringUTFChars(mp3Path, in_p);
  env->ReleaseStringUTFChars(pcmPath, out_p);
  return 0;
}

/* =========================================================================
 * WAV 转 PCM
 * ========================================================================= */
JNIEXPORT jint JNICALL Java_me_yun_silk_SilkCodec_wavToPcm(
    JNIEnv *env, jobject thiz, jstring wavPath, jstring pcmPath) {
  const char *in_p = env->GetStringUTFChars(wavPath, 0);
  const char *out_p = env->GetStringUTFChars(pcmPath, 0);

  /* ---------- 初始化 WAV 解码器 ---------- */
  drwav wav;
  if (!drwav_init_file(&wav, in_p, NULL)) {
    env->ReleaseStringUTFChars(wavPath, in_p);
    env->ReleaseStringUTFChars(pcmPath, out_p);
    return -501;
  }

  /* ---------- 打开输出文件 ---------- */
  FILE *fout = fopen(out_p, "wb");
  if (!fout) {
    drwav_uninit(&wav);
    env->ReleaseStringUTFChars(wavPath, in_p);
    env->ReleaseStringUTFChars(pcmPath, out_p);
    return -502;
  }

  /* ---------- 音频参数 ---------- */
  int wav_channels = wav.channels;
  int frame_size = 4096;

  /* ---------- PCM 缓冲区 ---------- */
  short *read_buf = (short *)malloc(frame_size * wav_channels * sizeof(short));
  short *mono_buf = (short *)malloc(frame_size * sizeof(short));

  /* ---------- 解码循环 ---------- */
  while (drwav_read_pcm_frames_s16(&wav, frame_size, read_buf) == (drwav_uint64)frame_size) {
    if (wav_channels == 2) {
      for (int i = 0; i < frame_size; i++) {
        mono_buf[i] = (short)(((int)read_buf[i * 2] + (int)read_buf[i * 2 + 1]) / 2);
      }
      fwrite(mono_buf, sizeof(short), frame_size, fout);
    } else {
      fwrite(read_buf, sizeof(short), frame_size, fout);
    }
  }

  /* ---------- 资源释放 ---------- */
  free(read_buf);
  free(mono_buf);
  drwav_uninit(&wav);
  fclose(fout);

  env->ReleaseStringUTFChars(wavPath, in_p);
  env->ReleaseStringUTFChars(pcmPath, out_p);
  return 0;
}

/* =========================================================================
 * FLAC 转 PCM
 * ========================================================================= */
JNIEXPORT jint JNICALL Java_me_yun_silk_SilkCodec_flacToPcm(
    JNIEnv *env, jobject thiz, jstring flacPath, jstring pcmPath) {
  const char *in_p = env->GetStringUTFChars(flacPath, 0);
  const char *out_p = env->GetStringUTFChars(pcmPath, 0);

  /* ---------- 初始化 FLAC 解码器 ---------- */
  drflac *pFlac = drflac_open_file(in_p, NULL);
  if (!pFlac) {
    env->ReleaseStringUTFChars(flacPath, in_p);
    env->ReleaseStringUTFChars(pcmPath, out_p);
    return -601;
  }

  /* ---------- 打开输出文件 ---------- */
  FILE *fout = fopen(out_p, "wb");
  if (!fout) {
    drflac_close(pFlac);
    env->ReleaseStringUTFChars(flacPath, in_p);
    env->ReleaseStringUTFChars(pcmPath, out_p);
    return -602;
  }

  /* ---------- 音频参数 ---------- */
  int flac_channels = pFlac->channels;
  int frame_size = 4096;

  /* ---------- PCM 缓冲区 ---------- */
  short *read_buf = (short *)malloc(frame_size * flac_channels * sizeof(short));
  short *mono_buf = (short *)malloc(frame_size * sizeof(short));

  /* ---------- 解码循环 ---------- */
  while (drflac_read_pcm_frames_s16(pFlac, frame_size, read_buf) == (drflac_uint64)frame_size) {
    if (flac_channels == 2) {
      for (int i = 0; i < frame_size; i++) {
        mono_buf[i] = (short)(((int)read_buf[i * 2] + (int)read_buf[i * 2 + 1]) / 2);
      }
      fwrite(mono_buf, sizeof(short), frame_size, fout);
    } else {
      fwrite(read_buf, sizeof(short), frame_size, fout);
    }
  }

  /* ---------- 资源释放 ---------- */
  free(read_buf);
  free(mono_buf);
  drflac_close(pFlac);
  fclose(fout);

  env->ReleaseStringUTFChars(flacPath, in_p);
  env->ReleaseStringUTFChars(pcmPath, out_p);
  return 0;
}

/* =========================================================================
 * OGG 转 PCM
 * ========================================================================= */
JNIEXPORT jint JNICALL Java_me_yun_silk_SilkCodec_oggToPcm(
    JNIEnv *env, jobject thiz, jstring oggPath, jstring pcmPath) {
  const char *in_p = env->GetStringUTFChars(oggPath, 0);
  const char *out_p = env->GetStringUTFChars(pcmPath, 0);

  /* ---------- 初始化 OGG 解码器 ---------- */
  int error;
  stb_vorbis *v = stb_vorbis_open_filename(in_p, &error, NULL);
  if (!v) {
    env->ReleaseStringUTFChars(oggPath, in_p);
    env->ReleaseStringUTFChars(pcmPath, out_p);
    return -401;
  }

  /* ---------- 打开输出文件 ---------- */
  FILE *fout = fopen(out_p, "wb");
  if (!fout) {
    stb_vorbis_close(v);
    env->ReleaseStringUTFChars(oggPath, in_p);
    env->ReleaseStringUTFChars(pcmPath, out_p);
    return -402;
  }

  /* ---------- 音频参数 ---------- */
  int frame_size = 4096;
  short *pcm_buf = (short *)malloc(frame_size * sizeof(short));

  /* ---------- 解码循环 ---------- */
  while (1) {
    int n = stb_vorbis_get_samples_short_interleaved(v, 1, pcm_buf, frame_size);
    if (n <= 0) break;
    fwrite(pcm_buf, sizeof(short), n, fout);
  }

  /* ---------- 资源释放 ---------- */
  free(pcm_buf);
  stb_vorbis_close(v);
  fclose(fout);

  env->ReleaseStringUTFChars(oggPath, in_p);
  env->ReleaseStringUTFChars(pcmPath, out_p);
  return 0;
}

/* =========================================================================
 * 辅助函数：获取 Silk 文件时长 (秒)
 * 
 * Silk 文件没有时长头，需要通过遍历所有数据帧来计算
 * ========================================================================= */
static double getSilkDuration(const char *path) {
    FILE *fin = fopen(path, "rb");
    if (!fin) return 0.0;

    // 1. 跳过微信 Silk 文件头 (10字节)
    char head_buf[16];
    int read_len = fread(head_buf, 1, 16, fin);
    int data_start = 0;
    for (int i = 0; i <= read_len - 9; i++) {
        if (memcmp(head_buf + i, "#!SILK_V3", 9) == 0) {
            data_start = i + 9;
            break;
        }
    }
    if (data_start == 0) { // 非标准 Silk
        fclose(fin);
        return 0.0;
    }
    fseek(fin, data_start, SEEK_SET);

    // 2. 遍历所有帧，每一帧代表 20ms (0.02s)
    int frameCount = 0;
    SKP_int16 nBytesIn;
    while (fread(&nBytesIn, sizeof(SKP_int16), 1, fin) == 1) {
        // 遇到 0xFFFF (Silk 结束标记) 则退出
        if (nBytesIn == -1 || (unsigned short)nBytesIn == 0xFFFF) break;
        if (nBytesIn <= 0 || nBytesIn > MAX_ARITHM_BYTES) break;

        // 跳过这一帧的数据内容
        if (fseek(fin, nBytesIn, SEEK_CUR) != 0) break;
        frameCount++;
    }

    fclose(fin);
    return frameCount * 0.020; // 每帧 20ms
}

/* =========================================================================
 * 辅助函数：获取 Silk 文件时长 (毫秒)
 * ========================================================================= */
static long long getSilkDurationMs(const char *path) {
    FILE *fin = fopen(path, "rb");
    if (!fin) return 0;

    // 1. 跳过微信 Silk 文件头
    char head_buf[16];
    int read_len = fread(head_buf, 1, 16, fin);
    int data_start = 0;
    for (int i = 0; i <= read_len - 9; i++) {
        if (memcmp(head_buf + i, "#!SILK_V3", 9) == 0) {
            data_start = i + 9;
            break;
        }
    }
    
    if (data_start == 0) {
        fclose(fin);
        return 0;
    }
    fseek(fin, data_start, SEEK_SET);

    // 2. 遍历帧。Silk 每帧固定 20ms
    long long totalMs = 0;
    short nBytesIn;
    while (fread(&nBytesIn, 2, 1, fin) == 1) {
        // 遇到 0xFFFF 结束符或异常长度则退出
        if (nBytesIn == -1 || (unsigned short)nBytesIn == 0xFFFF) break;
        if (nBytesIn <= 0 || nBytesIn > 2048) break;

        if (fseek(fin, nBytesIn, SEEK_CUR) != 0) break;
        totalMs += 20; // 每帧增加 20 毫秒
    }

    fclose(fin);
    return totalMs;
}

/* =========================================================================
 * 获取音频时长 (单位：毫秒)
 * 
 * 返回值: long (1秒 = 1000)
 * ========================================================================= */
JNIEXPORT jlong JNICALL Java_me_yun_silk_SilkCodec_getDuration(
    JNIEnv *env, jobject thiz, jstring filePath) {
    const char *path = env->GetStringUTFChars(filePath, 0);
    int type = detectFileType(path);
    jlong durationMs = 0;

    switch (type) {
        case FILE_TYPE_SILK:
            durationMs = (jlong)getSilkDurationMs(path);
            break;

        case FILE_TYPE_MP3: {
            drmp3 mp3;
            if (drmp3_init_file(&mp3, path, NULL)) {
                // 总毫秒 = (总采样数 * 1000) / 采样率
                durationMs = (jlong)((mp3.totalPCMFrameCount * 1000) / mp3.sampleRate);
                drmp3_uninit(&mp3);
            }
            break;
        }

        case FILE_TYPE_WAV: {
            drwav wav;
            if (drwav_init_file(&wav, path, NULL)) {
                durationMs = (jlong)((wav.totalPCMFrameCount * 1000) / wav.sampleRate);
                drwav_uninit(&wav);
            }
            break;
        }

        case FILE_TYPE_FLAC: {
            drflac *pFlac = drflac_open_file(path, NULL);
            if (pFlac) {
                durationMs = (jlong)((pFlac->totalPCMFrameCount * 1000) / pFlac->sampleRate);
                drflac_close(pFlac);
            }
            break;
        }

        case FILE_TYPE_OGG: {
            int error;
            stb_vorbis *v = stb_vorbis_open_filename(path, &error, NULL);
            if (v) {
                // stb_vorbis 提供的秒数转为毫秒
                durationMs = (jlong)(stb_vorbis_stream_length_in_seconds(v) * 1000);
                stb_vorbis_close(v);
            }
            break;
        }

        default:
            durationMs = 0;
            break;
    }

    env->ReleaseStringUTFChars(filePath, path);
    return durationMs;
}

/* =========================================================================
 * 自动识别音频格式并转 PCM (统一入口)
 * 
 * 支持格式: MP3, WAV, FLAC, OGG, PCM
 * 输出: 单声道 16-bit PCM
 * 返回值: 0=成功, 负数=错误码
 * 
 * 注意: 此函数通过文件头自动检测输入格式，不依赖扩展名
 * ========================================================================= */
JNIEXPORT jint JNICALL Java_me_yun_silk_SilkCodec_autoToPcm(
    JNIEnv *env, jobject thiz, jstring audioPath, jstring pcmPath) {
  const char *in_p = env->GetStringUTFChars(audioPath, 0);
  const char *out_p = env->GetStringUTFChars(pcmPath, 0);

  int inputType = detectFileType(in_p);
  jint result = 0;

  switch (inputType) {
    case FILE_TYPE_SILK:
      result = -6;
      break;
    case FILE_TYPE_MP3:
      result = Java_me_yun_silk_SilkCodec_mp3ToPcm(env, thiz, audioPath, pcmPath);
      break;
    case FILE_TYPE_WAV:
      result = Java_me_yun_silk_SilkCodec_wavToPcm(env, thiz, audioPath, pcmPath);
      break;
    case FILE_TYPE_FLAC:
      result = Java_me_yun_silk_SilkCodec_flacToPcm(env, thiz, audioPath, pcmPath);
      break;
    case FILE_TYPE_OGG:
      result = Java_me_yun_silk_SilkCodec_oggToPcm(env, thiz, audioPath, pcmPath);
      break;
    case FILE_TYPE_M4A:
      result = -14;
      break;
    case FILE_TYPE_PCM:
      result = -4;
      break;
    default:
      result = -2;
      break;
  }

  env->ReleaseStringUTFChars(audioPath, in_p);
  env->ReleaseStringUTFChars(pcmPath, out_p);
  return result;
}

}
