# 第三方与素材说明

- **Unicorn 2.1.4**：固定提交 `8028ec436f2d9376525352dd38ed9ed6b9f6be10`，由 [unicorn-engine/unicorn](https://github.com/unicorn-engine/unicorn/tree/8028ec436f2d9376525352dd38ed9ed6b9f6be10) 获取。包含 GPL 与 LGPL 组件，遵循上游各文件的许可。许可和作者说明保存在 `licenses/`，构建时也随 APK 打包。
- **Noto Sans SC**：字形来自 [Google Fonts 的 Noto Sans SC](https://github.com/google/fonts/tree/main/ofl/notosanssc)，遵循 `licenses/Noto-OFL.txt`。本仓库的艺术指令字形也是该字体的渲染结果。
- **新增配乐和音效**：由 `tools/build_original_game_audio.py` 合成，没有外部歌曲或录音采样，按 `native_hd/assets/audio/LICENSE.txt` 的 CC0-1.0 声明提供。
- **新增战斗界面**：底板由代码绘制，艺术字使用上述 OFL 字体。
- **原作游戏**：《轩辕炼妖录》的原程序和原资源归原权利人所有。原材料记录开发商为北京魔百创娱科技有限公司。本仓库的 GPL 声明只适用于新增运行层和代码，不授予原作程序或素材的权利。完整游戏数据与重绘的游戏纹理在本地使用，不包含在公开 Git 提交中。
