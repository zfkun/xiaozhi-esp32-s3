#include "ics43434_audio_codec.h"

#include <esp_log.h>
#include <cmath>
#include <cstring>
#include <cstdlib>

#include "driver/i2s_std.h"
#include "hal/i2s_types.h"

static const char* TAG = "Ics43434AudioCodec";

Ics43434AudioCodec::Ics43434AudioCodec(
    int input_sample_rate, int output_sample_rate,
    gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout,
    gpio_num_t mic_sck, gpio_num_t mic_ws, gpio_num_t mic_din,
    bool input_reference, AudioProcessingAlgorithm algorithm) {
    // parent protected members exist in NoAudioCodec/AudioCodec
    duplex_ = false;
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;

    // We expect two mic channels available on the physical line (stereo L/R)
    input_channels_ = 2;   // left = mic0, right = mic1
    // output_channels_ = 1;  // speaker mono
    input_reference_ = input_reference; // whether AFE expects reference channel
    
    // Initialize ref_ring_ if input reference is needed
    // Size the buffer to hold approximately 50ms of audio data
    if (input_reference_) {
        current_algorithm_ = algorithm;
        int buffer_size = input_sample_rate_ * 0.05; // 50ms buffer
        if (buffer_size < 512) buffer_size = 512;    // Minimum size
        if (buffer_size > 4096) buffer_size = 4096;  // Maximum size
        ref_ring_.resize(buffer_size, 0);
    }

    // Create a new channel for speaker
    i2s_chan_config_t chan_cfg = {
        .id = (i2s_port_t)1,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, nullptr));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            #ifdef   I2S_HW_VERSION_2
                .ext_clk_freq_hz = 0,
            #endif
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
            #ifdef   I2S_HW_VERSION_2
                .left_align = true,
                .big_endian = false,
                .bit_order_lsb = false
            #endif
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = spk_bclk,
            .ws = spk_ws,
            .dout = spk_dout,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));

    // Create the primary MIC channel
    chan_cfg.id = (i2s_port_t)0;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, nullptr, &rx_handle_));
    std_cfg.clk_cfg.sample_rate_hz = (uint32_t)input_sample_rate_;
    std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_STEREO;
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    std_cfg.gpio_cfg.bclk = mic_sck;
    std_cfg.gpio_cfg.ws = mic_ws;
    std_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.din = mic_din;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &std_cfg));

    ESP_LOGI(TAG, "Ics43434 codec created: RX stereo (mic0=L mic1=R), sample=%d, algorithm=%d", input_sample_rate_, (int)current_algorithm_);
}

Ics43434AudioCodec::~Ics43434AudioCodec() {
    if (rx_handle_) {
        i2s_channel_disable(rx_handle_);
        i2s_del_channel(rx_handle_);
        rx_handle_ = nullptr;
    }
    if (tx_handle_) {
        i2s_channel_disable(tx_handle_);
        i2s_del_channel(tx_handle_);
        tx_handle_ = nullptr;
    }

    std::lock_guard<std::mutex> lock(ref_mutex_);
    ref_ring_.clear();
}


int Ics43434AudioCodec::Read(int16_t* dest, int samples) {
    // 根据samples参数计算实际需要读取的帧数
    int output_channels = 1; // AFE只需要1个麦克风通道
    if (input_reference_) {
        output_channels++; // 如果需要参考信号，则输出通道数加1
    }
    
    int frames_requested = samples / output_channels;
    
    // 限制最大帧数以避免大内存分配
    const int max_frames = 512;
    if (frames_requested > max_frames) {
        frames_requested = max_frames;
    }
    
    // 使用栈上数组避免动态内存分配
    int32_t bit32_buffer[512 * 2]; // 最多支持立体声输入
    
    size_t bytes_read;
    esp_err_t ret = i2s_channel_read(rx_handle_, bit32_buffer, 
                                    frames_requested * input_channels_ * sizeof(int32_t), 
                                    &bytes_read, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S read failed: %d", ret);
        return 0;
    }

    int samples_read = bytes_read / sizeof(int32_t);
    int frames_got = samples_read / input_channels_;
    
    if (input_reference_) {
        // 需要提供参考信号给AEC
        for (int f = 0; f < frames_got; f++) {
            // 转换并拷贝麦克风数据 (从立体声到单声道)
            int32_t left = bit32_buffer[f * input_channels_] >> 12;
            int32_t right = bit32_buffer[f * input_channels_ + 1] >> 12;
            
            // 使用抽象的音频处理方法
            dest[f * 2] = ProcessStereoToMono(left, right);
            
            // 添加参考信号 (扬声器输出用于AEC)
            std::lock_guard<std::mutex> lock(ref_mutex_);
            if (!ref_ring_.empty()) {
                size_t ref_pos = (ref_read_pos_ + f) % ref_ring_.size();
                dest[f * 2 + 1] = ref_ring_[ref_pos];
            } else {
                dest[f * 2 + 1] = 0;
            }
        }
        
        // 更新ref_read_pos_
        if (!ref_ring_.empty()) {
            std::lock_guard<std::mutex> lock(ref_mutex_);
            ref_read_pos_ = (ref_read_pos_ + frames_got) % ref_ring_.size();
        }
        
        return frames_got * 2;
    } else {
        // 不需要参考信号，只拷贝麦克风数据
        for (int f = 0; f < frames_got; f++) {
            int32_t left = bit32_buffer[f * input_channels_] >> 12;
            int32_t right = bit32_buffer[f * input_channels_ + 1] >> 12;
            
            // 使用抽象的音频处理方法
            dest[f] = ProcessStereoToMono(left, right);
        }
        
        return frames_got;
    }
}

int Ics43434AudioCodec::Write(const int16_t* data, int samples) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    std::vector<int32_t> buffer(samples);

    // 正确的音量控制实现
    // output_volume_: 0-100
    // volume_factor_: 0-65536
    int32_t volume_factor = (int32_t)(pow((double)output_volume_ / 100.0, 2) * 65536);
    
    for (int i = 0; i < samples; i++) {
        // 先应用增益处理
        int16_t processed = ApplyGain(data[i]);
        
        // 再应用音量控制
        int64_t temp = (int64_t)processed * volume_factor;
        if (temp > INT32_MAX) {
            buffer[i] = INT32_MAX;
        } else if (temp < INT32_MIN) {
            buffer[i] = INT32_MIN;
        } else {
            buffer[i] = static_cast<int32_t>(temp);
        }
    }

    size_t bytes_written;
    esp_err_t ret = i2s_channel_write(tx_handle_, buffer.data(), samples * sizeof(int32_t), &bytes_written, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S write failed: %d", ret);
        return 0;
    }
    
    int samples_written = bytes_written / sizeof(int32_t);
    
    // 保存到 ref_ring_ 用于AEC参考 (仅在需要时)
    if (input_reference_) {
        std::lock_guard<std::mutex> ref_lock(ref_mutex_);

        if (ref_ring_.empty()) {
            ref_ring_.resize(1024, 0); // 1024样本的环形缓冲区
        }
        
        for (int i = 0; i < samples_written; i++) {
            // 参考信号只需要增益处理，不需要音量控制
            ref_ring_[ref_write_pos_] = ApplyGain(data[i]);
            ref_write_pos_ = (ref_write_pos_ + 1) % ref_ring_.size();
        }
    }
    
    return samples_written;
}


float Ics43434AudioCodec::audio_gain_ = 4.0f; // 默认增益4.0倍
int32_t Ics43434AudioCodec::max_signal_ = 1; // 用于自动增益控制的最大信号值

void Ics43434AudioCodec::SetAlgorithm(AudioProcessingAlgorithm algorithm) {
    current_algorithm_ = algorithm;
}

void Ics43434AudioCodec::SetAudioGain(float gain) {
    audio_gain_ = gain;
}

int16_t Ics43434AudioCodec::ProcessStereoToMono(int32_t left, int32_t right) {
    switch (current_algorithm_) {
        case ALGORITHM_SIMPLE_AVERAGE:
            return ProcessWithSimpleAverage(left, right);
        case ALGORITHM_BEAMFORMING_BASIC:
            return ProcessWithBasicBeamforming(left, right);
        case ALGORITHM_NOISE_SUPPRESSION:
            return ProcessWithNoiseSuppression(left, right);
        default:
            return ProcessWithBasicBeamforming(left, right);
    }
}

int16_t Ics43434AudioCodec::ProcessWithSimpleAverage(int32_t left, int32_t right) {
    int32_t mono = (left + right) / 2;
    return ApplyGain(mono);
}

int16_t Ics43434AudioCodec::ProcessWithBasicBeamforming(int32_t left, int32_t right) {
    // 简单的差分波束成形算法 - 增强前方声音，抑制后方噪声
    int32_t beamformed = (left + right) / 2;
    
    // 噪声抑制 - 如果两个麦克风信号差异很大，可能是噪声
    int32_t diff = abs(left - right);
    int32_t sum = abs(left) + abs(right);
    if (sum > 0) {
        float noise_ratio = (float)diff / (float)sum;
        if (noise_ratio > 0.7f) { // 噪声比例过高
            beamformed = beamformed * 0.5f; // 降低增益
        }
    }
    
    return ApplyGain(beamformed);
}

int16_t Ics43434AudioCodec::ProcessWithNoiseSuppression(int32_t left, int32_t right) {
    // 更强的噪声抑制算法
    int32_t beamformed = (left + right) / 2;
    
    // 计算信号相关性
    int32_t diff = abs(left - right);
    int32_t sum = abs(left) + abs(right);
    
    if (sum > 0) {
        float correlation = 1.0f - ((float)diff / (float)sum);
        
        // 根据相关性调整增益
        if (correlation < 0.3f) {
            // 信号不相关，很可能是噪声
            beamformed = beamformed * 0.2f;
        } else if (correlation < 0.6f) {
            // 信号部分相关
            beamformed = beamformed * 0.6f;
        } else {
            // 信号高度相关，可能是语音
            beamformed = beamformed * 1.2f;
        }
    }
    
    return ApplyGain(beamformed);
}

int16_t Ics43434AudioCodec::ApplyGain(int32_t signal) {
    // 更新最大信号值（用于自动增益控制）
    if (abs(signal) > max_signal_) {
        max_signal_ = abs(signal);
    }
    
    // 实现简单的自动增益控制
    const int32_t target_level = INT16_MAX / 3; // 目标幅度为最大值的1/3
    if (max_signal_ > 0) {
        audio_gain_ = (float)target_level / (float)max_signal_;
        if (audio_gain_ < 1.0f) audio_gain_ = 1.0f;
        if (audio_gain_ > 8.0f) audio_gain_ = 8.0f;
    }
    
    // 应用增益
    float amplified = (float)signal * audio_gain_;
    
    // 确保不溢出
    if (amplified > INT16_MAX) amplified = INT16_MAX;
    if (amplified < INT16_MIN) amplified = INT16_MIN;
    
    return (int16_t)amplified;
}