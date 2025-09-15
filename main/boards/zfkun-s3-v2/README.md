# 小智S3 双麦 经典版

## 自动编译

```
python scripts/release.py zfkun-s3-v2
```

## 手工编译

0. 修改 `CMakeLists.txt` 文件

增加板子类型
```
...

elseif(CONFIG_BOARD_TYPE_ZFKUN_S3_V2)
    set(BOARD_TYPE "zfkun-s3-v2")
    set(LVGL_TEXT_FONT ${FONT_PUHUI_BASIC_14_1})
    set(LVGL_ICON_FONT ${FONT_AWESOME_14_1})
    set(DEFAULT_ASSETS ${ASSETS_XIAOZHI_PUHUI_COMMON_14_1})
...
```

1. 修改 `Kconfig.projbuild` 文件

增加板子类型
```
choice BOARD_TYPE
    ...

    config BOARD_TYPE_ZFKUN_S3_V2
        bool "小智S3 双麦 (经典)"
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

3. 增加 AEC 支持

```
config USE_DEVICE_AEC
    ...

    default n
        depends on USE_AUDIO_PROCESSOR && ( ... || BOARD_TYPE_ZFKUN_S3_V2)

    ...

```

4. 增加专属配置

```
config SD_ENABLE
    bool "Enable SD Card Support"
    default y
        depends on (BOARD_TYPE_ZFKUN_S3_V2)
    help
        开启SD卡支持功能 (开机自动挂载)

```