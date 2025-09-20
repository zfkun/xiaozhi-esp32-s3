# 小智S3 双麦 普通版

## 自动编译

```
python scripts/release.py zfkun-s3-v1
```

## 手工编译

0. 修改 `CMakeLists.txt` 文件

增加板子类型
```
...

elseif(CONFIG_BOARD_TYPE_ZFKUN_S3_V1)
    set(BOARD_TYPE "zfkun-s3-v1")
    set(BUILTIN_TEXT_FONT font_puhui_basic_14_1)
    set(BUILTIN_ICON_FONT font_awesome_14_1)

...
```

1. 修改 `Kconfig.projbuild` 文件

增加板子类型
```
choice BOARD_TYPE
    ...

    config BOARD_TYPE_ZFKUN_S3_V1
        bool "小智S3 双麦 (普通)"
        depends on IDF_TARGET_ESP32S3

    ...
```

2. 增加OLED屏幕选择支持

```
choice DISPLAY_OLED_TYPE
    ...

    depends on ... || BOARD_TYPE_ZFKUN_S3_V1

    ...
```

3. 增加 AEC 支持

```
config USE_DEVICE_AEC
    ...

    default n
        depends on USE_AUDIO_PROCESSOR && ( ... || BOARD_TYPE_ZFKUN_S3_V1)

    ...

```

4. 增加专属配置

```
choice AUDIO_PROCESSING_ALGORITHM
    depends on USE_DEVICE_AEC && (BOARD_TYPE_ZFKUN_S3_V1)
    prompt "Audio Processing Algorithm"
    default AUDIO_PROCESSING_ALGORITHM_2
    help
        音频增益处理算法
    config AUDIO_PROCESSING_ALGORITHM_0
        bool "简单平均"
    config AUDIO_PROCESSING_ALGORITHM_1
        bool "基础波束成形"
    config AUDIO_PROCESSING_ALGORITHM_2
        bool "噪声抑制"
endchoice
```