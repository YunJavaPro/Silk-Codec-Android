package me.yun.silk.utils;

import me.yun.silk.SilkCodec;

public class Conversion {

    public interface ConversionCallback {
        void onMessage(String msg);
    }

    public static void startTransform(SilkCodec codec, int type, String inputPath, String outputPath, int sampleRate, ConversionCallback callback) {
        new Thread(() -> {
            try {
                int fileType = codec.getFileType(inputPath);
                callback.onMessage("真实类型: " + getFileTypeName(fileType) + " (" + fileType + ")");

                int result = -1;
                switch (type) {
                    case 0: result = codec.silkToMp3(inputPath, outputPath, sampleRate); break;
                    case 1: result = codec.mp3ToSilk(inputPath, outputPath, sampleRate); break;
                    case 5: result = codec.autoToSilk(inputPath, outputPath, sampleRate); break;
                    case 6: result = codec.autoToPcm(inputPath, outputPath); break;
                }

                if (result == 0) {
                    callback.onMessage("处理成功！");
                } else {
                    callback.onMessage(getErrorMsg(result));
                }
            } catch (Exception e) {
                callback.onMessage("异常: " + e.getMessage());
            }
        }).start();
    }

    private static String getFileTypeName(int type) {
        switch (type) {
            case 1: return "Silk";
            case 2: return "MP3";
            case 3: return "WAV";
            case 4: return "FLAC";
            case 5: return "OGG";
            case 6: return "PCM";
            case 7: return "M4A";
            case 8: return "AAC";
            default: return "未知";
        }
    }

    private static String getErrorMsg(int code) {
        switch (code) {
            case 0: return "成功";
            case -1: return "错误码:-1 → 无法获取文件扩展名";
            case -2: return "错误码:-2 → 不支持的音频格式";
            case -3: return "错误码:-3 → PCM 转 Silk 需要额外参数";
            case -4: return "错误码:-4 → 输入已经是 PCM 格式";
            case -5: return "错误码:-5 → 输入已经是 Silk 格式";
            case -6: return "错误码:-6 → Silk 转 PCM 请使用 silkToMp3";
            case -10: return "错误码:-10 → 输出必须是 .silk 或 .slk";
            case -11: return "错误码:-11 → 输出必须是 .mp3";
            case -12: return "错误码:-12 → 输出必须是 .pcm 或 .raw";
            case -13: return "错误码:-13 → 文件格式与方法不匹配";
            case -201: case -202: return "错误码:" + code + " → Silk 转 MP3 文件错误";
            case -301: case -302: return "错误码:" + code + " → MP3 解码错误";
            case -401: case -402: return "错误码:" + code + " → OGG 解码错误";
            case -501: case -502: return "错误码:" + code + " → WAV 解码错误";
            case -601: case -602: return "错误码:" + code + " → FLAC 解码错误";
            case -701: case -702: case -703: return "错误码:" + code + " → PCM 参数错误";
            default: return "错误码:" + code + " → 未知错误";
        }
    }
}