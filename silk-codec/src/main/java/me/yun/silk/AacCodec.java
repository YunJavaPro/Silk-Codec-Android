package me.yun.silk;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.media.MediaMuxer;

import java.io.File;
import java.io.FileOutputStream;
import java.io.RandomAccessFile;

public class AacCodec {

    private static final int TIMEOUT_US = 10000;
    private static final int DEFAULT_SAMPLE_RATE = 44100;
    private static final int DEFAULT_CHANNEL_COUNT = 1;
    private static final int DEFAULT_BIT_RATE = 128000;

    public static class AudioInfo {
        int sampleRate;
        int channelCount;

        public AudioInfo(int sampleRate, int channelCount) {
            this.sampleRate = sampleRate;
            this.channelCount = channelCount;
        }
    }

    public interface AacCallback {
        void onProgress(int progress);
        void onMessage(String msg);
    }

    public static AudioInfo getAudioInfo(String aacPath) {
        MediaExtractor extractor = new MediaExtractor();
        try {
            extractor.setDataSource(aacPath);
            for (int i = 0; i < extractor.getTrackCount(); i++) {
                MediaFormat format = extractor.getTrackFormat(i);
                String mime = format.getString(MediaFormat.KEY_MIME);
                if (mime != null && mime.startsWith("audio/")) {
                    int sampleRate = format.containsKey(MediaFormat.KEY_SAMPLE_RATE)
                        ? format.getInteger(MediaFormat.KEY_SAMPLE_RATE) : DEFAULT_SAMPLE_RATE;
                    int channelCount = format.containsKey(MediaFormat.KEY_CHANNEL_COUNT)
                        ? format.getInteger(MediaFormat.KEY_CHANNEL_COUNT) : DEFAULT_CHANNEL_COUNT;
                    extractor.release();
                    return new AudioInfo(sampleRate, channelCount);
                }
            }
            extractor.release();
            return new AudioInfo(DEFAULT_SAMPLE_RATE, DEFAULT_CHANNEL_COUNT);
        } catch (Exception e) {
            extractor.release();
            return new AudioInfo(DEFAULT_SAMPLE_RATE, DEFAULT_CHANNEL_COUNT);
        }
    }

    public static int decodeAacFile(String aacPath, String pcmPath, AacCallback callback) {
        if (callback != null) callback.onMessage("开始解码: " + aacPath);
        
        MediaExtractor extractor = new MediaExtractor();
        try {
            File inputFile = new File(aacPath);
            if (!inputFile.exists()) {
                if (callback != null) callback.onMessage("文件不存在: " + aacPath);
                return -801;
            }

            extractor.setDataSource(aacPath);
            int audioTrackIndex = -1;
            MediaFormat inputFormat = null;
            for (int i = 0; i < extractor.getTrackCount(); i++) {
                MediaFormat format = extractor.getTrackFormat(i);
                String mime = format.getString(MediaFormat.KEY_MIME);
                if (mime != null && mime.startsWith("audio/")) {
                    audioTrackIndex = i;
                    inputFormat = format;
                    if (callback != null) callback.onMessage("找到音频轨道: " + i);
                    break;
                }
            }
            if (audioTrackIndex == -1) {
                if (callback != null) callback.onMessage("未找到音频轨道");
                return -802;
            }
            extractor.selectTrack(audioTrackIndex);

            int sampleRate = inputFormat.containsKey(MediaFormat.KEY_SAMPLE_RATE)
                ? inputFormat.getInteger(MediaFormat.KEY_SAMPLE_RATE) : DEFAULT_SAMPLE_RATE;
            int channelCount = inputFormat.containsKey(MediaFormat.KEY_CHANNEL_COUNT)
                ? inputFormat.getInteger(MediaFormat.KEY_CHANNEL_COUNT) : DEFAULT_CHANNEL_COUNT;

            if (callback != null) callback.onMessage("参数: " + sampleRate + "Hz, " + channelCount + "通道");

            MediaCodec codec = MediaCodec.createDecoderByType(inputFormat.getString(MediaFormat.KEY_MIME));
            codec.configure(inputFormat, null, null, 0);
            codec.start();

            FileOutputStream fos = new FileOutputStream(pcmPath);
            java.nio.ByteBuffer[] inputBuffers = codec.getInputBuffers();
            java.nio.ByteBuffer[] outputBuffers = codec.getOutputBuffers();

            MediaCodec.BufferInfo bufferInfo = new MediaCodec.BufferInfo();
            boolean sawInputEOS = false;
            boolean sawOutputEOS = false;

            while (!sawOutputEOS) {
                if (!sawInputEOS) {
                    int inputBufferIndex = codec.dequeueInputBuffer(TIMEOUT_US);
                    if (inputBufferIndex >= 0) {
                        java.nio.ByteBuffer inputBuffer = inputBuffers[inputBufferIndex];
                        int sampleSize = extractor.readSampleData(inputBuffer, 0);
                        if (sampleSize < 0) {
                            codec.queueInputBuffer(inputBufferIndex, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                            sawInputEOS = true;
                        } else {
                            codec.queueInputBuffer(inputBufferIndex, 0, sampleSize, extractor.getSampleTime(), 0);
                            extractor.advance();
                        }
                    }
                }

                int outputBufferIndex = codec.dequeueOutputBuffer(bufferInfo, TIMEOUT_US);
                if (outputBufferIndex >= 0) {
                    if ((bufferInfo.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) sawOutputEOS = true;
                    if (bufferInfo.size > 0) {
                        java.nio.ByteBuffer outputBuffer = outputBuffers[outputBufferIndex];
                        outputBuffer.position(bufferInfo.offset);
                        outputBuffer.limit(bufferInfo.offset + bufferInfo.size);
                        
                        byte[] buffer = new byte[bufferInfo.size];
                        outputBuffer.get(buffer);
                        
                        if (channelCount == 1) {
                            fos.write(buffer);
                        } else {
                            int monoSize = bufferInfo.size / channelCount;
                            byte[] monoBuffer = new byte[monoSize];
                            for (int i = 0; i < monoSize / 2; i++) {
                                int leftIdx = i * channelCount * 2;
                                short sample = (short)((buffer[leftIdx] & 0xFF) | (buffer[leftIdx + 1] << 8));
                                monoBuffer[i * 2] = (byte)(sample & 0xFF);
                                monoBuffer[i * 2 + 1] = (byte)((sample >> 8) & 0xFF);
                            }
                            fos.write(monoBuffer);
                        }
                    }
                    codec.releaseOutputBuffer(outputBufferIndex, false);
                } else if (outputBufferIndex == MediaCodec.INFO_OUTPUT_BUFFERS_CHANGED) {
                    outputBuffers = codec.getOutputBuffers();
                }
            }

            fos.close();
            codec.stop();
            codec.release();
            extractor.release();
            if (callback != null) callback.onMessage("解码完成");
            return 0;
        } catch (Exception e) {
            if (callback != null) callback.onMessage("解码异常: " + e.getMessage());
            e.printStackTrace();
            return -803;
        }
    }

    public static int encodePcmToAac(String pcmPath, String aacPath, int sampleRate, int channels, AacCallback callback) {
        if (callback != null) callback.onMessage("开始编码 AAC: " + pcmPath);
        
        File pcmFile = new File(pcmPath);
        if (!pcmFile.exists()) {
            if (callback != null) callback.onMessage("PCM 文件不存在");
            return -901;
        }

        try {
            MediaFormat format = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, sampleRate, channels);
            format.setInteger(MediaFormat.KEY_BIT_RATE, DEFAULT_BIT_RATE);
            format.setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC);
            format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 16384);

            MediaCodec codec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC);
            codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            codec.start();

            RandomAccessFile raf = new RandomAccessFile(pcmPath, "r");
            FileOutputStream fos = new FileOutputStream(aacPath);
            java.nio.ByteBuffer[] inputBuffers = codec.getInputBuffers();
            java.nio.ByteBuffer[] outputBuffers = codec.getOutputBuffers();

            MediaCodec.BufferInfo bufferInfo = new MediaCodec.BufferInfo();
            boolean sawInputEOS = false;
            boolean sawOutputEOS = false;
            int frameCount = 0;
            long fileSize = pcmFile.length();

            while (!sawOutputEOS) {
                if (!sawInputEOS) {
                    int inputBufferIndex = codec.dequeueInputBuffer(TIMEOUT_US);
                    if (inputBufferIndex >= 0) {
                        java.nio.ByteBuffer inputBuffer = inputBuffers[inputBufferIndex];
                        inputBuffer.clear();
                        
                        byte[] pcmChunk = new byte[Math.min(4096, (int)(fileSize - raf.getFilePointer()))];
                        int bytesRead = raf.read(pcmChunk);
                        
                        if (bytesRead > 0) {
                            inputBuffer.put(pcmChunk, 0, bytesRead);
                            long timeUs = (frameCount * 1024L * 1000000L) / sampleRate;
                            codec.queueInputBuffer(inputBufferIndex, 0, bytesRead, timeUs, 0);
                            frameCount++;
                        } else {
                            codec.queueInputBuffer(inputBufferIndex, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                            sawInputEOS = true;
                        }

                        if (callback != null && fileSize > 0) {
                            int progress = (int)(raf.getFilePointer() * 100 / fileSize);
                            callback.onProgress(Math.min(progress, 99));
                        }
                    }
                }

                int outputBufferIndex = codec.dequeueOutputBuffer(bufferInfo, TIMEOUT_US);
                if (outputBufferIndex >= 0) {
                    if ((bufferInfo.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) sawOutputEOS = true;
                    
                    if (bufferInfo.size > 0) {
                        java.nio.ByteBuffer outputBuffer = outputBuffers[outputBufferIndex];
                        byte[] aacFrame = new byte[bufferInfo.size];
                        outputBuffer.get(aacFrame);
                        
                        byte[] adtsHeader = createAdtsHeader(sampleRate, channels, aacFrame.length);
                        fos.write(adtsHeader);
                        fos.write(aacFrame);
                    }
                    
                    codec.releaseOutputBuffer(outputBufferIndex, false);
                } else if (outputBufferIndex == MediaCodec.INFO_OUTPUT_BUFFERS_CHANGED) {
                    outputBuffers = codec.getOutputBuffers();
                }
            }

            raf.close();
            fos.close();
            codec.stop();
            codec.release();
            
            if (callback != null) {
                callback.onMessage("编码完成");
                callback.onProgress(100);
            }
            return 0;
        } catch (Exception e) {
            if (callback != null) callback.onMessage("编码异常: " + e.getMessage());
            e.printStackTrace();
            return -902;
        }
    }

    public static int encodePcmToM4a(String pcmPath, String m4aPath, int sampleRate, int channels, AacCallback callback) {
        if (callback != null) callback.onMessage("开始编码 M4A: " + pcmPath);
        
        File pcmFile = new File(pcmPath);
        if (!pcmFile.exists()) {
            if (callback != null) callback.onMessage("PCM 文件不存在");
            return -911;
        }

        try {
            MediaFormat format = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, sampleRate, channels);
            format.setInteger(MediaFormat.KEY_BIT_RATE, DEFAULT_BIT_RATE);
            format.setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC);
            format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 16384);

            MediaCodec codec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC);
            codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            codec.start();

            MediaMuxer muxer = new MediaMuxer(m4aPath, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4);
            int trackIndex = -1;
            boolean muxerStarted = false;

            RandomAccessFile raf = new RandomAccessFile(pcmPath, "r");
            java.nio.ByteBuffer[] inputBuffers = codec.getInputBuffers();
            java.nio.ByteBuffer[] outputBuffers = codec.getOutputBuffers();

            MediaCodec.BufferInfo bufferInfo = new MediaCodec.BufferInfo();
            boolean sawInputEOS = false;
            boolean sawOutputEOS = false;
            int frameCount = 0;
            long fileSize = pcmFile.length();
            int frameSize = 1024 * channels * 2;

            while (!sawOutputEOS) {
                if (!sawInputEOS) {
                    int inputBufferIndex = codec.dequeueInputBuffer(TIMEOUT_US);
                    if (inputBufferIndex >= 0) {
                        java.nio.ByteBuffer inputBuffer = inputBuffers[inputBufferIndex];
                        inputBuffer.clear();
                        
                        byte[] pcmChunk = new byte[Math.min(frameSize, (int)(fileSize - raf.getFilePointer()))];
                        int bytesRead = raf.read(pcmChunk);
                        
                        if (bytesRead > 0) {
                            long timeUs = (frameCount * 1024L * 1000000L) / sampleRate;
                            codec.queueInputBuffer(inputBufferIndex, 0, bytesRead, timeUs, 0);
                            frameCount++;
                        } else {
                            codec.queueInputBuffer(inputBufferIndex, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                            sawInputEOS = true;
                        }

                        if (callback != null && fileSize > 0) {
                            int progress = (int)(raf.getFilePointer() * 100 / fileSize);
                            callback.onProgress(Math.min(progress, 99));
                        }
                    }
                }

                int outputBufferIndex = codec.dequeueOutputBuffer(bufferInfo, TIMEOUT_US);
                if (outputBufferIndex >= 0) {
                    if ((bufferInfo.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) sawOutputEOS = true;
                    
                    if (bufferInfo.size > 0 && !sawOutputEOS) {
                        if (!muxerStarted) {
                            MediaFormat outputFormat = codec.getOutputFormat();
                            trackIndex = muxer.addTrack(outputFormat);
                            muxer.start();
                            muxerStarted = true;
                        }
                        
                        java.nio.ByteBuffer outputBuffer = outputBuffers[outputBufferIndex];
                        muxer.writeSampleData(trackIndex, outputBuffer, bufferInfo);
                    }
                    
                    codec.releaseOutputBuffer(outputBufferIndex, false);
                } else if (outputBufferIndex == MediaCodec.INFO_OUTPUT_BUFFERS_CHANGED) {
                    outputBuffers = codec.getOutputBuffers();
                }
            }

            raf.close();
            codec.stop();
            codec.release();
            if (muxerStarted) muxer.stop();
            muxer.release();
            
            if (callback != null) {
                callback.onMessage("编码完成");
                callback.onProgress(100);
            }
            return 0;
        } catch (Exception e) {
            if (callback != null) callback.onMessage("编码异常: " + e.getMessage());
            e.printStackTrace();
            return -912;
        }
    }

    private static byte[] createAdtsHeader(int sampleRate, int channels, int frameLength) {
        int sampleRateIndex = getSampleRateIndex(sampleRate);
        byte[] adtsHeader = new byte[7];
        adtsHeader[0] = (byte) 0xFF;
        adtsHeader[1] = (byte) 0xF1;
        adtsHeader[2] = (byte) (((2 - 1) << 6) | (sampleRateIndex << 2) | (channels >> 2));
        adtsHeader[3] = (byte) (((channels & 3) << 6) | ((frameLength + 7) >> 11));
        adtsHeader[4] = (byte) (((frameLength + 7) >> 3) & 0xFF);
        adtsHeader[5] = (byte) ((((frameLength + 7) & 7) << 5) | 0x1F);
        adtsHeader[6] = (byte) 0xFC;
        return adtsHeader;
    }

    private static int getSampleRateIndex(int sampleRate) {
        switch (sampleRate) {
            case 96000: return 0;
            case 88200: return 1;
            case 64000: return 2;
            case 48000: return 3;
            case 44100: return 4;
            case 32000: return 5;
            case 24000: return 6;
            case 22050: return 7;
            case 16000: return 8;
            case 12000: return 9;
            case 11025: return 10;
            case 8000: return 11;
            default: return 4;
        }
    }

    public static int mp4ToSilk(String mp4Path, String silkPath, SilkCodec codec, int hz) {
        try {
            String tempPcm = silkPath + ".temp.pcm";
            AudioInfo audioInfo = getAudioInfo(mp4Path);
            int result = decodeAacFile(mp4Path, tempPcm, null);
            if (result != 0) return result - 2000;

            result = codec.pcmToSilk(tempPcm, silkPath, hz, audioInfo.sampleRate, 1);
            new File(tempPcm).delete();
            return result;
        } catch (Exception e) {
            e.printStackTrace();
            return -1031;
        }
    }

    public static int silkToM4a(String silkPath, String m4aPath, SilkCodec codec, int hz) {
        try {
            String tempPcm = m4aPath + ".temp.pcm";
            int result = codec.silkToPcm(silkPath, tempPcm, hz);
            if (result != 0) return result;

            result = encodePcmToM4a(tempPcm, m4aPath, hz, 1, null);
            new File(tempPcm).delete();
            return result;
        } catch (Exception e) {
            e.printStackTrace();
            return -1001;
        }
    }

    public static int mp4ToM4a(String mp4Path, String m4aPath, int hz) {
        try {
            String tempPcm = m4aPath + ".temp.pcm";
            AudioInfo audioInfo = getAudioInfo(mp4Path);
            int result = decodeAacFile(mp4Path, tempPcm, null);
            if (result != 0) return result - 2000;

            result = encodePcmToM4a(tempPcm, m4aPath, audioInfo.sampleRate, 1, null);
            new File(tempPcm).delete();
            return result;
        } catch (Exception e) {
            e.printStackTrace();
            return -1061;
        }
    }

    public static int mp4ToAac(String mp4Path, String aacPath, int hz) {
        try {
            String tempPcm = aacPath + ".temp.pcm";
            AudioInfo audioInfo = getAudioInfo(mp4Path);
            int result = decodeAacFile(mp4Path, tempPcm, null);
            if (result != 0) return result - 2000;

            result = encodePcmToAac(tempPcm, aacPath, audioInfo.sampleRate, 1, null);
            new File(tempPcm).delete();
            return result;
        } catch (Exception e) {
            e.printStackTrace();
            return -1051;
        }
    }

    public static int m4aToSilk(String m4aPath, String silkPath, SilkCodec codec, int hz) {
        return mp4ToSilk(m4aPath, silkPath, codec, hz);
    }

    public static int aacToSilk(String aacPath, String silkPath, SilkCodec codec, int hz) {
        return mp4ToSilk(aacPath, silkPath, codec, hz);
    }

    public static int m4aToAac(String m4aPath, String aacPath, int hz) {
        return mp4ToAac(m4aPath, aacPath, hz);
    }

    public static int m4aToM4a(String m4aPath, String m4aPathOut, int hz) {
        return mp4ToM4a(m4aPath, m4aPathOut, hz);
    }

    public static int autoToAac(String inputPath, String aacPath, SilkCodec codec, int hz) {
        int fileType = codec.getFileType(inputPath);
        switch (fileType) {
            case 1: return silkToAac(inputPath, aacPath, codec, hz);
            case 2: return mp3ToAac(inputPath, aacPath, hz);
            case 3: return wavToAac(inputPath, aacPath, hz);
            case 7: return m4aToAac(inputPath, aacPath, hz);
            case 8: return mp4ToAac(inputPath, aacPath, hz);
            default: return -2;
        }
    }

    public static int autoToM4a(String inputPath, String m4aPath, SilkCodec codec, int hz) {
        int fileType = codec.getFileType(inputPath);
        switch (fileType) {
            case 1: return silkToM4a(inputPath, m4aPath, codec, hz);
            case 2: return mp3ToM4a(inputPath, m4aPath, hz);
            case 3: return wavToM4a(inputPath, m4aPath, hz);
            case 7: return m4aToM4a(inputPath, m4aPath, hz);
            case 8: return mp4ToM4a(inputPath, m4aPath, hz);
            default: return -2;
        }
    }

    public static int autoAacToSilk(String inputPath, String silkPath, SilkCodec codec, int hz) {
        return m4aToSilk(inputPath, silkPath, codec, hz);
    }

    public static int silkToAac(String silkPath, String aacPath, SilkCodec codec, int hz) {
        try {
            String tempPcm = aacPath + ".temp.pcm";
            int result = codec.silkToPcm(silkPath, tempPcm, hz);
            if (result != 0) return result;
            
            result = encodePcmToAac(tempPcm, aacPath, hz, 1, null);
            new File(tempPcm).delete();
            return result;
        } catch (Exception e) {
            e.printStackTrace();
            return -1001;
        }
    }

    private static int mp3ToAac(String mp3Path, String aacPath, int sampleRate) {
        try {
            String tempPcm = aacPath + ".temp.pcm";
            SilkCodec codec = new SilkCodec();
            int result = codec.mp3ToPcm(mp3Path, tempPcm);
            if (result != 0) return result;
            
            result = encodePcmToAac(tempPcm, aacPath, sampleRate, 1, null);
            new File(tempPcm).delete();
            return result;
        } catch (Exception e) {
            e.printStackTrace();
            return -1011;
        }
    }

    private static int mp3ToM4a(String mp3Path, String m4aPath, int sampleRate) {
        try {
            String tempPcm = m4aPath + ".temp.pcm";
            SilkCodec codec = new SilkCodec();
            int result = codec.mp3ToPcm(mp3Path, tempPcm);
            if (result != 0) return result;
            
            result = encodePcmToM4a(tempPcm, m4aPath, sampleRate, 1, null);
            new File(tempPcm).delete();
            return result;
        } catch (Exception e) {
            e.printStackTrace();
            return -1012;
        }
    }

    private static int wavToAac(String wavPath, String aacPath, int sampleRate) {
        try {
            String tempPcm = aacPath + ".temp.pcm";
            SilkCodec codec = new SilkCodec();
            int result = codec.wavToPcm(wavPath, tempPcm);
            if (result != 0) return result;
            
            result = encodePcmToAac(tempPcm, aacPath, sampleRate, 1, null);
            new File(tempPcm).delete();
            return result;
        } catch (Exception e) {
            e.printStackTrace();
            return -1021;
        }
    }

    private static int wavToM4a(String wavPath, String m4aPath, int sampleRate) {
        try {
            String tempPcm = m4aPath + ".temp.pcm";
            SilkCodec codec = new SilkCodec();
            int result = codec.wavToPcm(wavPath, tempPcm);
            if (result != 0) return result;
            
            result = encodePcmToM4a(tempPcm, m4aPath, sampleRate, 1, null);
            new File(tempPcm).delete();
            return result;
        } catch (Exception e) {
            e.printStackTrace();
            return -1022;
        }
    }

    public static int aacToPcm(String aacPath, String pcmPath) {
        return decodeAacFile(aacPath, pcmPath, null);
    }

    public static int pcmToAac(String pcmPath, String aacPath, int sampleRate, int channels) {
        return encodePcmToAac(pcmPath, aacPath, sampleRate, channels, null);
    }

    public static int pcmToM4a(String pcmPath, String m4aPath, int sampleRate, int channels) {
        return encodePcmToM4a(pcmPath, m4aPath, sampleRate, channels, null);
    }

    public static int m4aToPcm(String m4aPath, String pcmPath) {
        return decodeAacFile(m4aPath, pcmPath, null);
    }

    public static int decodeM4aFile(String m4aPath, String pcmPath, AacCallback callback) {
        return decodeAacFile(m4aPath, pcmPath, callback);
    }

    public static String getErrorMessage(int code) {
        if (code == 0) return "成功";
        if (code >= -801 && code <= -802) return "AAC/M4A 解码错误 (文件读取失败)";
        if (code == -803) return "AAC/M4A 解码错误 (格式不支持)";
        if (code >= -901 && code <= -902) return "AAC 编码错误 (文件操作失败)";
        if (code >= -911 && code <= -912) return "M4A 编码错误 (Muxer 失败)";
        if (code >= -1001 && code <= -1009) return "Silk 转 AAC/M4A 错误";
        if (code >= -1011 && code <= -1012) return "MP3 转 AAC/M4A 错误";
        if (code >= -1021 && code <= -1022) return "WAV 转 AAC/M4A 错误";
        if (code >= -1031 && code <= -1039) return "M4A/AAC 转 Silk 错误";
        if (code >= -1051 && code <= -1059) return "M4A/AAC 转 AAC 错误";
        if (code >= -1061 && code <= -1069) return "M4A/AAC 转 M4A 错误";
        if (code == -2000) return "M4A/AAC 转 Silk 错误 (解码失败)";
        return "错误码: " + code + " → 未知错误";
    }
}
