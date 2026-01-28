#include "soc/gpio_num.h"
#include "wifi_board.h"
#include "codecs/box_audio_codec.h"
#include "display/lcd_display.h"
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
#include <driver/spi_common.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

#if CONFIG_SD_ENABLE
#include <driver/sdmmc_host.h>
#include <driver/sdmmc_defs.h>
#include <sdmmc_cmd.h>
#include <esp_vfs_fat.h>
#include <sys/stat.h>
#include <dirent.h>
#endif

#if CONFIG_SERVO_ENABLE
#include "iot_servo.h"
#endif

#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0,
    (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C,
                0x04, 0x12, 0x14, 0x1f},
    14, 0},
    {0xf1,
    (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D,
                0x0C, 0x1A, 0x14, 0x1E},
    14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};
#endif

#define TAG "ZfkunS3V2LcdBoard"

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

class ZfkunS3V2LcdBoard : public WifiBoard {
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

#if CONFIG_SERVO_ENABLE
    // 舵机移动回调函数类型
    typedef std::function<void(int channel, int angle)> ServoMoveCallback;

    // 带回调的舵机控制函数
    void MoveServoWithCallback(int channel, int angle, ServoMoveCallback callback = nullptr) {
        // 执行舵机移动
        iot_servo_write_angle(LEDC_LOW_SPEED_MODE, channel, angle);
        
        // 等待舵机移动完成（基于经验设定延时）
        // SG90舵机通常需要几百毫秒完成60度以上的转动
        int delay_ms = abs(angle - GetCurrentServoAngle(channel)) * 5; // 简单估算，每度5ms
        if (delay_ms < 100) delay_ms = 100;  // 最小延时
        if (delay_ms > 1000) delay_ms = 1000; // 最大延时
        
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        
        // 调用回调函数
        if (callback) {
            callback(channel, angle);
        }
    }

    // 获取当前舵机角度的辅助函数
    int GetCurrentServoAngle(int channel) {
        float angle;
        esp_err_t err = iot_servo_read_angle(LEDC_LOW_SPEED_MODE, channel, &angle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "读取舵机角度失败: channel=%d, err=%s", channel, esp_err_to_name(err));
            return -1;
        }
        return static_cast<int>(angle);
    }
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

#if CONFIG_LCD_DEBUG_FOR_V1
        ESP_LOGW(TAG, "I2C bus initialized for v1.0 hardware. scl=%d, sda=%d", AUDIO_CODEC_I2C_SCL_PIN, AUDIO_CODEC_I2C_SDA_PIN);
#endif

        // Initialize CustomPa
        pa_ = new CustomPa(AUDIO_PA_EN_GPIO);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));

#if CONFIG_LCD_DEBUG_FOR_V1
        ESP_LOGW(TAG, "SPI bus initialized for v1.0 hardware. mosi=%d, sclk=%d", DISPLAY_MOSI_PIN, DISPLAY_CLK_PIN);
#endif
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
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

    void InitializeSt7789Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };        
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif
        
        esp_lcd_panel_reset(panel);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#ifdef  LCD_TYPE_GC9A01_SERIAL
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);

#if CONFIG_LCD_DEBUG_FOR_V1
        ESP_LOGW(TAG, "LCD initialized for v1.0 hardware. width=%d, height=%d, host=%d, mode=%d, scl=%d, sda=%d, sdo=%d, rst=%d, dc=%d, cs=%d, csf=%d, bl=%d",
          DISPLAY_WIDTH, DISPLAY_HEIGHT, SPI3_HOST, DISPLAY_SPI_MODE, DISPLAY_CLK_PIN, DISPLAY_MOSI_PIN, DISPLAY_MISO_PIN, DISPLAY_RST_PIN, DISPLAY_DC_PIN, DISPLAY_CS_PIN, DISPLAY_CSF_PIN, DISPLAY_BACKLIGHT_PIN);
#endif
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

#if CONFIG_SERVO_ENABLE
    void InitializeServo() {
        ESP_LOGI(TAG, "Initializing Servo");

        // 初始化舵机
        servo_config_t servo_cfg = {
            .max_angle = SERVO_MAX_ANGLE,
            .min_width_us = SERVO_MIN_WIDTH_US,
            .max_width_us = SERVO_MAX_WIDTH_US,
            .freq = 50,
            .timer_number = LEDC_TIMER_0,
            .channels = {
                .servo_pin = {
                    SERVO_0_GPIO,
                    SERVO_1_GPIO,
                    SERVO_2_GPIO,
                    SERVO_3_GPIO,
                },
                .ch = {
                    LEDC_CHANNEL_0,
                    LEDC_CHANNEL_1,
                    LEDC_CHANNEL_2,
                    LEDC_CHANNEL_3,
                },
            },
            .channel_number = 4,
        };
        ESP_ERROR_CHECK(iot_servo_init(LEDC_LOW_SPEED_MODE, &servo_cfg));


        // 复位初始角度
        esp_err_t err;
        for (int i = 0; i < 4; i++) {
          err = iot_servo_write_angle(LEDC_LOW_SPEED_MODE,
                                      servo_cfg.channels.ch[i],
                                      SERVO_DEFAULT_ANGLE);
          if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reset servo angle: channel=%d, angle=%d, err=%d", i, SERVO_DEFAULT_ANGLE, err);
          }
        }
    }

    void AddServoTools() { 
        auto& mcp_server = McpServer::GetInstance();
        ESP_LOGI(TAG, "开始注册舵机MCP工具...");

        // 设置单个舵机角度
        mcp_server.AddTool(
            "self.servo.set_angle",
            "设置SG90舵机到指定角度。"
            "channel: 目标舵机编号(0-3)"
            "angle: 目标角度(0-180度)",
            PropertyList({
              Property("channel", kPropertyTypeInteger, 0, 0, 3),
              Property("angle", kPropertyTypeInteger, 90, 0, 180)
            }),
            [this](const PropertyList &properties) -> ReturnValue {
              int channel = properties["channel"].value<int>();
              int angle = properties["angle"].value<int>();
              // iot_servo_write_angle(LEDC_LOW_SPEED_MODE, channel, angle);
              MoveServoWithCallback(channel, angle, [this](int ch, int ang) {
                  ESP_LOGI(TAG, "舵机 %d 完成移动到 %d 度", ch, ang);
                  // 可以在这里添加更多回调逻辑
              });

              return std::to_string(channel) + "号舵机设置到 " + std::to_string(angle) + " 度";
            });

        // 设置所有舵机角度
        mcp_server.AddTool(
            "self.servo.set_angle_all",
            "同时设置所有SG90舵机到指定角度。"
            "angle: 目标角度(0-180度)",
            PropertyList({
              Property("angle", kPropertyTypeInteger, 90, 0, 180)
            }),
            [this](const PropertyList &properties) -> ReturnValue {
              int angle = properties["angle"].value<int>();
              // iot_servo_write_angle(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, angle);
              // iot_servo_write_angle(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, angle);
              // iot_servo_write_angle(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, angle);
              // iot_servo_write_angle(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, angle);

              // 为每个舵机执行移动并添加回调
              for (int i = 0; i < 4; i++) {
                  MoveServoWithCallback(i, angle, [this](int ch, int ang) {
                      ESP_LOGI(TAG, "舵机 %d 完成移动到 %d 度", ch, ang);
                  });
              }

              return "所有舵机设置到 " + std::to_string(angle) + " 度";
            });

        // 获取舵机状态
        mcp_server.AddTool("self.servo.get_status_all",
            "获取所有SG90舵机当前状态",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                float angles[4] = {0, 0, 0, 0};
                std::string status = "{\"angles\":[";

                esp_err_t err = ESP_OK;
                for (int i = 0; i <= 3; i++) {
                  err = iot_servo_read_angle(LEDC_LOW_SPEED_MODE, i, &angles[i]);
                  if (err != ESP_OK) {
                    ESP_LOGE(TAG, "%d 号舵机角度读取失败: %s", i, esp_err_to_name(err));
                  }

                  status += (i > 0 ? "," : "") + std::to_string(angles[i]);
                }
                
                status += "]}";

                ESP_LOGI(TAG, "舵机状态: %s", status.c_str());
                return status;
          });
    }
#endif

    void InitializeTools() {
#if CONFIG_SERVO_ENABLE
        AddServoTools();
#endif
    }

public:
    ZfkunS3V2LcdBoard(): boot_button_(BOOT_BUTTON_GPIO), user_button_(USER_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeSt7789Display();
        InitializeButtons();
        // InitializeCamera();
#if CONFIG_SD_ENABLE
        InitializeSdcard();
#endif
#if CONFIG_SERVO_ENABLE
        InitializeServo();
#endif
        InitializeTools();
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
    }

    ~ZfkunS3V2LcdBoard() {
#if CONFIG_SD_ENABLE
        UnmountSdcard();
#endif
#if CONFIG_SERVO_ENABLE
        iot_servo_deinit(LEDC_LOW_SPEED_MODE);
#endif
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

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(ZfkunS3V2LcdBoard);
