package me.yun.silk;

public class SilkCodec {

    static {
        System.loadLibrary("silk");
    }

    /**
     * 获取文件实际类型（通过文件头检测）
     *
     * @param filePath 文件路径
     * @return 文件类型常量 0 = 未知类型 1 = Silk 2 = MP3 3 = WAV 4 = FLAC 5 = OGG 6 = PCM 7 = M4A 8 = AAC
     */
    public native int getFileType(String filePath);

    // ==================== 转 Silk ====================

    /**
     * MP3 转 Silk
     *
     * @param mp3Path 输入 MP3 文件路径
     * @param silkPath 输出 Silk 文件路径
     * @param hz Silk 编码内部采样率 (8000/12000/16000/24000/32000/44100/48000)
     * @return 0=成功, 负数=错误码
     */
    public native int mp3ToSilk(String mp3Path, String silkPath, int hz);

    /**
     * WAV 转 Silk
     *
     * @param wavPath 输入 WAV 文件路径
     * @param silkPath 输出 Silk 文件路径
     * @param hz Silk 编码内部采样率
     * @return 0=成功, 负数=错误码
     */
    public native int wavToSilk(String wavPath, String silkPath, int hz);

    /**
     * FLAC 转 Silk
     *
     * @param flacPath 输入 FLAC 文件路径
     * @param silkPath 输出 Silk 文件路径
     * @param hz Silk 编码内部采样率
     * @return 0=成功, 负数=错误码
     */
    public native int flacToSilk(String flacPath, String silkPath, int hz);

    /**
     * OGG 转 Silk
     *
     * @param oggPath 输入 OGG 文件路径
     * @param silkPath 输出 Silk 文件路径
     * @param hz Silk 编码内部采样率
     * @return 0=成功, 负数=错误码
     */
    public native int oggToSilk(String oggPath, String silkPath, int hz);

    /**
     * PCM 转 Silk
     *
     * @param pcmPath 输入 PCM 文件路径
     * @param silkPath 输出 Silk 文件路径
     * @param hz Silk 编码内部采样率 (8000/12000/16000/24000/32000/44100/48000)
     * @param pcmHz 输入 PCM 文件采样率
     * @param channels 输入 PCM 文件声道数 (1=单声道, 2=立体声)
     * @return 0=成功, 负数=错误码
     */
    public native int pcmToSilk(String pcmPath, String silkPath, int hz, int pcmHz, int channels);

    /**
     * 自动识别音频格式并转 Silk 支持格式: MP3, WAV, FLAC, OGG, M4A, AAC, AMR
     *
     * @param audioPath 输入音频文件路径
     * @param silkPath 输出 Silk 文件路径
     * @param hz Silk 编码内部采样率
     * @return 0=成功, 负数=错误码
     */
    public native int autoToSilk(String audioPath, String silkPath, int hz);

    // ==================== Silk 转 MP3 ====================

    /**
     * Silk 转 MP3
     *
     * @param silkPath 输入 Silk 文件路径
     * @param mp3Path 输出 MP3 文件路径
     * @param hz 输出 MP3 采样率
     * @return 0=成功, 负数=错误码
     */
    public native int silkToMp3(String silkPath, String mp3Path, int hz);

    // ==================== 转 PCM ====================

    /**
     * MP3 转 PCM
     *
     * @param mp3Path 输入 MP3 文件路径
     * @param pcmPath 输出 PCM 文件路径
     * @return 0=成功, 负数=错误码
     */
    public native int mp3ToPcm(String mp3Path, String pcmPath);

    /**
     * WAV 转 PCM
     *
     * @param wavPath 输入 WAV 文件路径
     * @param pcmPath 输出 PCM 文件路径
     * @return 0=成功, 负数=错误码
     */
    public native int wavToPcm(String wavPath, String pcmPath);

    /**
     * FLAC 转 PCM
     *
     * @param flacPath 输入 FLAC 文件路径
     * @param pcmPath 输出 PCM 文件路径
     * @return 0=成功, 负数=错误码
     */
    public native int flacToPcm(String flacPath, String pcmPath);

    /**
     * OGG 转 PCM
     *
     * @param oggPath 输入 OGG 文件路径
     * @param pcmPath 输出 PCM 文件路径
     * @return 0=成功, 负数=错误码
     */
    public native int oggToPcm(String oggPath, String pcmPath);

    /**
     * 自动识别音频格式并转 PCM 支持格式: MP3, WAV, FLAC, OGG, M4A, AAC, AMR
     *
     * @param audioPath 输入音频文件路径
     * @param pcmPath 输出 PCM 文件路径
     * @return 0=成功, 负数=错误码
     */
    public native int autoToPcm(String audioPath, String pcmPath);

    /**
     * 获取音频时长（毫秒）
     *
     * @param filePath 音频文件路径
     * @return 时长，单位毫秒 (例如 2秒返回 2000)
     */
    public native long getDuration(String filePath);

    /**
     * 获取限制后的音频时长（毫秒） 如果时长超过 60 秒，则强制返回 60000 毫秒
     *
     * @param filePath 音频文件路径
     * @return 时长（毫秒），最高 60000
     */
    public long getDurations(String filePath) {
        long duration = getDuration(filePath);
        // 60秒 = 60 * 1000 毫秒
        if (duration > 60000) {
            return 60000;
        }
        return duration;
    }
}
