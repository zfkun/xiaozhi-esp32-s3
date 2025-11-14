# 小智S3 双麦 经典版 LCD

## 自动编译

```
python scripts/release.py zfkun-s3-v2-lcd
```

> 产出的固件中 `*-debug-for-v1` 是兼容初版V1.0板子调试LCD用的, 一般用不到


## 手工编译

0. 修改 `CMakeLists.txt` 文件

增加板子类型
```
...

elseif(CONFIG_BOARD_TYPE_ZFKUN_S3_V2_LCD)
    set(BOARD_TYPE "zfkun-s3-v2-lcd")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)

...
```

1. 修改 `Kconfig.projbuild` 文件

增加板子类型
```
choice BOARD_TYPE
    ...

    config BOARD_TYPE_ZFKUN_S3_V2_LCD
        bool "小智S3 双麦 (经典 LCD)"
        depends on IDF_TARGET_ESP32S3

    ...
```

2. 增加OLED屏幕选择支持

```
choice DISPLAY_OLED_TYPE
    ...

    depends on ... || BOARD_TYPE_ZFKUN_S3_V2

    ...
```

3. 添加LED屏幕选择支持

```
choice DISPLAY_LCD_TYPE
    ...

    depends on  ... || BOARD_TYPE_ZFKUN_S3_V2_LCD
    
    ...
```

4. 增加 AEC 支持

```
config USE_DEVICE_AEC
    ...

    default n
        depends on USE_AUDIO_PROCESSOR && ( ... || BOARD_TYPE_ZFKUN_S3_V2_LCD)

    ...

```

5. 增加专属配置

```
config SD_ENABLE
    bool "Enable SD Card Support"
    default y
        depends on (BOARD_TYPE_ZFKUN_S3_V2 || BOARD_TYPE_ZFKUN_S3_V2_LCD)
    help
        开启SD卡支持功能 (开机自动挂载)

config SERVO_ENABLE
    bool "Enable Servo Support"
    default y
        depends on (BOARD_TYPE_ZFKUN_S3_V2 || BOARD_TYPE_ZFKUN_S3_V2_LCD)
    help
        开启舵机控制支持功能 (SG90)

config LCD_DEBUG_FOR_V1
    bool "Enable Debug For V1 Device"
    default n
        depends on BOARD_TYPE_ZFKUN_S3_V2_LCD
    help
        开启屏幕调试 (仅用于初版V1.0设备)
```