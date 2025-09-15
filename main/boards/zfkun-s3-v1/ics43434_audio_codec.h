#ifndef _ICS43434_AUDIO_CODEC_H
#define _ICS43434_AUDIO_CODEC_H

#include "codecs/no_audio_codec.h"

#include <driver/gpio.h>
#include <mutex>
#include <vector>
#include <cstdint>

// 音频增益处理算法枚举
enum AudioProcessingAlgorithm {
    ALGORITHM_SIMPLE_AVERAGE,    // 简单平均
    ALGORITHM_BEAMFORMING_BASIC, // 基础波束成形
    ALGORITHM_NOISE_SUPPRESSION  // 噪声抑制
};

class Ics43434AudioCodec : public NoAudioCodec {
public:
    Ics43434AudioCodec(int input_sample_rate, int output_sample_rate,
                      gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout,
                      gpio_num_t mic_sck, gpio_num_t mic_ws, gpio_num_t mic_din,
                      bool input_reference = false, AudioProcessingAlgorithm algorithm = ALGORITHM_NOISE_SUPPRESSION);

    ~Ics43434AudioCodec() override;

    int Read(int16_t* dest, int samples) override;
    int Write(const int16_t *data, int samples) override;

    AudioProcessingAlgorithm GetAlgorithm() const { return current_algorithm_; }
    void SetAlgorithm(AudioProcessingAlgorithm algorithm); // 设置音频增益处理算法
    void SetAudioGain(float gain); // 设置统一增益

private:
    // internal ring buffer for playback ref (optional)
    std::vector<int16_t> ref_ring_;
    size_t ref_write_pos_ = 0;
    size_t ref_read_pos_ = 0;
    std::mutex ref_mutex_;

    // 当前使用的算法
    AudioProcessingAlgorithm current_algorithm_ = ALGORITHM_NOISE_SUPPRESSION; // 默认使用噪声抑制

    // 统一的增益控制
    static float audio_gain_;
    static int32_t max_signal_;

    // 音频处理方法
    int16_t ProcessStereoToMono(int32_t left, int32_t right);
    // 统一的增益应用方法
    int16_t ApplyGain(int32_t signal);

    int16_t ProcessWithSimpleAverage(int32_t left, int32_t right); // 简单平均算法
    int16_t ProcessWithBasicBeamforming(int32_t left, int32_t right); // 基础波束成形算法
    int16_t ProcessWithNoiseSuppression(int32_t left, int32_t right); // 噪声抑制算法
};

#endif // _ICS43434_AUDIO_CODEC_H
