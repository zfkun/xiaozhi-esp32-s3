#include "soc/gpio_num.h"
#include "wifi_board.h"
#include "codecs/box_audio_codec.h"
#include "display/oled_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "led/single_led.h"
#include "assets/lang_config.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

#ifdef SH1106
#include <esp_lcd_panel_sh1106.h>
#endif

#if CONFIG_SD_ENABLE
#include <driver/sdmmc_host.h>
#include <driver/sdmmc_defs.h>
#include <sdmmc_cmd.h>
#include <esp_vfs_fat.h>
#include <sys/stat.h>
#include <dirent.h>
#endif

#define TAG "ZfkunS3V2Board"

class CustomPa {
private:
    gpio_num_t en_pin_;
public:
    CustomPa(gpio_num_t en_pin) : en_pin_(en_pin) {
        if(en_pin_ != GPIO_NUM_NC) {
            gpio_config_t io_conf = {};
            io_conf.intr_type = GPIO_INTR_DISABLE;
            io_conf.mode = GPIO_MODE_OUTPUT;
            io_conf.pin_bit_mask = (1ULL << AUDIO_PA_EN_GPIO);
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            gpio_config(&io_conf);

            ESP_LOGI(TAG, "PA EN pin initialized");
        }
    }

    void SetOutputState(uint8_t level) {
        if (en_pin_ != GPIO_NUM_NC) {
            gpio_set_level(en_pin_, level);
            ESP_LOGI(TAG, "PA output level: %d", level);
        }
    }
};

class CustomAudioCodec : public BoxAudioCodec {
private:
    CustomPa* pa_;

public:
    CustomAudioCodec(i2c_master_bus_handle_t i2c_bus, CustomPa* pa) 
        : BoxAudioCodec(i2c_bus, 
                       AUDIO_INPUT_SAMPLE_RATE, 
                       AUDIO_OUTPUT_SAMPLE_RATE,
                       AUDIO_I2S_GPIO_MCLK, 
                       AUDIO_I2S_GPIO_BCLK, 
                       AUDIO_I2S_GPIO_WS, 
                       AUDIO_I2S_GPIO_DOUT, 
                       AUDIO_I2S_GPIO_DIN,
                       GPIO_NUM_NC, 
                       AUDIO_CODEC_ES8311_ADDR, 
                       AUDIO_CODEC_ES7210_ADDR, 
                       AUDIO_INPUT_REFERENCE),
          pa_(pa) {
    }

    virtual void EnableOutput(bool enable) override {
        BoxAudioCodec::EnableOutput(enable);
        if (enable) {
            pa_->SetOutputState(1);
        } else {
            pa_->SetOutputState(0);
        }
    }
};

class ZfkunS3V2Board : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    // i2c_master_dev_handle_t pca9557_handle_;
    Button boot_button_;
    Button user_button_;
    Display* display_ = nullptr;
    CustomPa* pa_;
    // Esp32Camera* camera_;
#if CONFIG_SD_ENABLE
    sdmmc_card_t* sdcard_ = nullptr;
    bool sdcard_mounted_ = false;
    bool sdcard_present_ = false;
    uint32_t last_sdcard_check_ = 0;
#endif

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));

        // Initialize CustomPa
        pa_ = new CustomPa(AUDIO_PA_EN_GPIO);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });

#if CONFIG_USE_DEVICE_AEC
        boot_button_.OnLongPress([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateIdle) {
                app.SetAecMode(app.GetAecMode() == kAecOff ? kAecOnDeviceSide : kAecOff);
            }
        });
#endif

        user_button_.OnClick([this]() {
            ESP_LOGI(TAG, "User button clicked");
        });
    }

    void InitializeSsd1306Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        // SSD1306 config
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400 * 1000,
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(i2c_bus_, &io_config, &panel_io));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

#ifdef SH1106
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io, &panel_config, &panel_));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io, &panel_config, &panel));
#endif
        ESP_LOGI(TAG, "SSD1306 driver installed");

        // Reset the display
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        if (esp_lcd_panel_init(panel) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, false));

        // Set the display to on
        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

        display_ = new OledDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

#if CONFIG_SD_ENABLE
    void InitializeSdcard() {
        ESP_LOGI(TAG, "Initializing SD card");

        // 如果有SD卡检测引脚，配置为输入
        if (SD_CARD_DETECT_GPIO != GPIO_NUM_NC) {
            gpio_config_t io_conf = {};
            io_conf.intr_type = GPIO_INTR_DISABLE;
            io_conf.mode = GPIO_MODE_INPUT;
            io_conf.pin_bit_mask = (1ULL << SD_CARD_DETECT_GPIO);
            io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            gpio_config(&io_conf);
        }
        
        // 初始化SD卡状态
        sdcard_present_ = IsSdcardPresent();
        
        // 如果SD卡已插入，挂载它
        if (sdcard_present_) {
            MountSdcard();
        }
        
        // // 注册到Application的周期性更新中
        // auto& app = Application::GetInstance();
        // app.Schedule([this]() {
        //     // 每秒检查一次SD卡状态
        //     static int counter = 0;
        //     counter++;
        //     if (counter % 1 == 0) { // 每秒检查一次
        //         HandleSdcardHotplug();
        //     }
        // });
    }

    bool IsSdcardPresent() {
        // 如果没有检测引脚，假设SD卡始终存在
        if (SD_CARD_DETECT_GPIO == GPIO_NUM_NC) {
            return true;
        }
        
        // 检查SD卡检测引脚状态
        // 通常低电平表示卡存在，但这也取决于硬件设计
        return gpio_get_level(SD_CARD_DETECT_GPIO) == 0;
    }

    void HandleSdcardHotplug() {
        bool present = IsSdcardPresent();
        
        // 检查SD卡状态是否发生变化
        if (present != sdcard_present_) {
            sdcard_present_ = present;
            
            if (present) {
                ESP_LOGI(TAG, "SD card inserted");
                MountSdcard();
            } else {
                ESP_LOGI(TAG, "SD card removed");
                UnmountSdcard();
            }
        }
    }

    void MountSdcard() {
        if (sdcard_mounted_) {
            ESP_LOGW(TAG, "SD card already mounted");
            return;
        }

        // SDMMC主机配置
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.flags = SDMMC_HOST_FLAG_1BIT; // 1线模式
        // host.slot = SDMMC_HOST_SLOT_0; // 使用slot 0
        
        // SDMMC slot配置 - 手动指定所有引脚
        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        slot_config.width = 1;
        slot_config.clk = SD_CARD_CLK_GPIO;
        slot_config.cmd = SD_CARD_CMD_GPIO;
        slot_config.d0 = SD_CARD_D0_GPIO;
        slot_config.d1 = GPIO_NUM_NC;
        slot_config.d2 = GPIO_NUM_NC;
        slot_config.d3 = GPIO_NUM_NC;
        slot_config.d4 = GPIO_NUM_NC;
        slot_config.d5 = GPIO_NUM_NC;
        slot_config.d6 = GPIO_NUM_NC;
        slot_config.d7 = GPIO_NUM_NC;

        // 挂载配置
        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 16 * 1024
        };
        
        // 挂载文件系统
        esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_CARD_BASE_PATH, &host, &slot_config, &mount_config, &sdcard_);
        
        if (ret != ESP_OK) {
            if (ret == ESP_FAIL) {
                ESP_LOGE(TAG, "Failed to mount filesystem. "
                    "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
            } else {
                ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                    "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
            }
            sdcard_ = nullptr;
            return;
        }
        
        // 已初始化，打印SD卡信息
        sdmmc_card_print_info(stdout, sdcard_);
        ESP_LOGI(TAG, "SD card mounted successfully");
        sdcard_mounted_ = true;
    }

    void UnmountSdcard() {
        if (!sdcard_mounted_) {
            ESP_LOGW(TAG, "SD card not mounted");
            return;
        }
        
        ESP_LOGI(TAG, "Unmounting SD card");
        esp_err_t ret = esp_vfs_fat_sdcard_unmount(SD_CARD_BASE_PATH, sdcard_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to unmount SD card (%s)", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "SD card unmounted successfully");
            sdcard_ = nullptr;
            sdcard_mounted_ = false;
        }
    }
#endif

public:
    ZfkunS3V2Board(): boot_button_(BOOT_BUTTON_GPIO), user_button_(USER_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        // InitializeCamera();
        InitializeSdcard();
    }

    ~ZfkunS3V2Board() {
        UnmountSdcard();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static CustomAudioCodec audio_codec(i2c_bus_, pa_);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(ZfkunS3V2Board);
