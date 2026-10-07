# 轩辕炼妖录 · 原版高清安卓运行层

[![Build and verify](https://github.com/jisongmuli/XuanyuanLianyaoluHD/actions/workflows/check.yml/badge.svg)](https://github.com/jisongmuli/XuanyuanLianyaoluHD/actions/workflows/check.yml)

为旧版《轩辕炼妖录》提供安卓运行环境、高清绘制、触屏操作和新配乐。原版 ARM 程序通过 Unicorn 执行，安卓运行层对接旧系统接口，沿用原版剧情、地图、战斗与炼妖规则。

当前对应 **v0.7**，最低 Android 9，包含 `arm64-v8a` 和 `x86_64`，高清画布为 1440×1920。程序不申请联网或短信权限。

## 下载与安装

[下载 v0.7 APK 安装包](https://github.com/jisongmuli/XuanyuanLianyaoluHD/releases/download/v0.7/xuanyuan-original-hd-v0.7.apk) · [发布说明](https://github.com/jisongmuli/XuanyuanLianyaoluHD/releases/tag/v0.7)

Release 提供原来保存的 v0.7 APK，保持原签名和文件内容，没有重新打包。已安装同系列原签名版本时可直接覆盖安装以保留存档；若提示签名不匹配，请保留现有应用，不要直接卸载。新构建的测试签名与这个原安装包不同。

## v0.7 功能

- 高清纹理与 918 个中文及数字字形；原游戏输入可通过方向键、确定、返回和完整 0–9 数字键操作。
- 7 首新合成场景配乐、12 种合成音效；声音开关、切后台暂停和音频焦点处理。
- 战斗角色放大 35%，重新排列站位；独立特效居中，最大放大 55%。
- “战、技、物、收、商、破”使用金色艺术字，选中指令更加醒目。
- 原版存档读写、菜单及字幕底板、旧付费界面的本地兼容处理。

## 仓库与素材范围

Git 源码公开安卓 C++／Java 运行层、构建和音频生成脚本、新合成音频与代码绘制的战斗界面。完整 APK 通过 Release 提供；原作程序、剧情数据、地图、高清游戏纹理与字形缓存可从该安装包导入到本地，已加入 `.gitignore`。签名私钥和用户存档不上传。

原作程序与资源的权利归原权利人所有，不因运行层采用 GPL 而改变。运行层与本仓库新增代码使用 **GPL-2.0-or-later**；新合成音频使用 **CC0-1.0**；Noto Sans SC 字形遵循 **SIL OFL 1.1**。详见 [LICENSE](LICENSE) 和 [第三方说明](THIRD_PARTY_NOTICES.md)。

## 从已有 v0.7 APK 恢复本地构建输入

需要 Python 3.10 以上和 Git。保留自己的 v0.7 APK，在仓库根目录执行：

```powershell
python tools/import_local_apk.py "你的路径/轩辕炼妖录_原版高清_修正版_v0.7.apk"
python tools/verify_project.py
python tools/fetch_unicorn.py
```

导入工具只接受校验值与构建记录一致的 v0.7 包，提取原程序、高清素材和字形缓存，不读取手机存档，也不提取签名私钥。发现本地文件与导入文件不同会停止，不覆盖已有修改。

## Windows 构建

安装 Python、Git for Windows 后执行：

```powershell
python tools/fetch_android_tools.py
python tools/fetch_original_android_toolchain.py
python tools/build_original_android.py
```

工具将 Android SDK API 36、Build Tools 36.0.0、NDK r30、CMake 和 JDK 17 放在本地 `.toolchain/` 下。也可以通过 `ANDROID_HOME`、`ANDROID_NDK_HOME`、`JAVA_HOME` 指定已有安装，并让 CMake／Ninja 可在终端使用。输出为 `build/xuanyuan-original-hd-android-test.apk`。

没有原作资源时，可以执行 `python tools/build_original_android.py --compile-only` 仅编译 C++ 和 Java；GitHub Actions 会在两个 CPU 架构上运行此检查。

**签名说明：** 原 v0.7 私有签名密钥已随旧源码目录删除，未能恢复。新构建会生成新的本地测试签名，因此无法直接覆盖安装原 APK。需要保留进度时，请先通过原应用可用的方式备份存档；不要为了安装新构建直接卸载现有游戏。本仓库恢复了运行层源码，不表示恢复了原签名或可以逐字节重建原 APK。

## 重新生成新增素材

```powershell
python -m pip install -r requirements-art.txt
python tools/build_original_game_audio.py
```

战斗底板与艺术指令可以通过 `tools/build_battle_presentation.py` 生成；需将带 OFL 许可证的 Noto Sans SC 字体放到 `remaster/assets/fonts/NotoSansSC.subset.ttf`。仓库已提供 v0.7 合成音频和战斗界面成品，常规构建无需重新生成。

## 源码恢复与验证

原源码目录被删除后，按历史构建记录中的完整成功补丁及源码修改记录恢复了运行层。v0.7 APK 的 SHA-256 为：

```text
9a6df89af423769878c1d8032d297b0930b914d7153483ca643d889cd62fa410
```

记录中 v0.7 曾通过 Android 15 模拟器检查；本次恢复通过资产校验、JNI 对接检查和两个架构的编译验证，不代表重新完成了全部游戏流程测试。未在 iQOO 15T 实机验证，也未验证完整通关。恢复文件校验值见 [恢复记录](docs/recovery-v0.7.json)。
