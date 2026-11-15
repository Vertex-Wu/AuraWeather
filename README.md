# AuraWeather

![C++](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Platform](https://img.shields.io/badge/Platform-Windows%2010%2F11-lightgrey.svg)
![Runtime](https://img.shields.io/badge/Runtime%20Dependencies-Zero-red.svg)
![License](https://img.shields.io/badge/License-MIT-green.svg)

AuraWeather 是一款基于纯 C++ (Win32 API) 开发的 Windows 原生天气软件。不使用第三方 UI 框架，最终交付为一个静态编译的单文件 `.exe`。

## 软件介绍

AuraWeather 致力于在 Windows 平台上提供极致的轻量级天气体验。软件采用原生 GDI/GDI+ 渲染，具有极低的内存占用，并适配了系统的暗黑/亮色主题与高 DPI 缩放。

### 核心特性

- **纯血 C++ 渲染**：完全基于 Win32 原生渲染，拒绝第三方 UI 框架。
- **运行时零依赖**：通过全静态编译（`-static`），普通用户下载解压后双击即可运行，无需安装任何系统运行库。
- **智能出行建议**：内置多维度逻辑分析，根据降雨率、紫外线、温差、空气质量等数据，自动生成生活建议。
- **月相演算**：内置数学算法，实时计算并展示当前月相，配合 Windows 原生极简线条图标。
- **便携模式**：在 exe 同级目录新建 `portable.flag` 文件即可激活便携模式，数据全留本地，不写入系统目录。

## 如何使用（普通用户）

1. 前往本仓库右侧的 **Releases** 页面。
2. 下载最新的 `AuraWeather-vX.X.X.zip` 压缩包。
3. 解压后，双击 `AuraWeather.exe` 即可直接运行。
4. （可选）如果需要放入 U 盘使用，请确保解压目录中保留 `portable.flag` 空文件，软件会将缓存和配置保存在当前目录的 `data` 文件夹下。

## 如何自行编译（开发者）

本项目涉及对原生 Windows API 和 OpenSSL 静态库的调用，自行编译需配置以下环境。

### 环境依赖
- 操作系统：Windows 10 / 11
- 编译工具链：MSYS2 (推荐 UCRT64 环境) 或 MinGW-w64。确保 `g++` 和 `windres` 已加入系统环境变量 `PATH`。
- 第三方库：OpenSSL 静态库（用于 HTTPS 网络请求）。

在 MSYS2 UCRT64 终端中安装 OpenSSL 依赖：
```bash
pacman -S mingw-w64-ucrt-x86_64-openssl
```

### 编译步骤
1. 克隆本仓库到本地：
   ```bash
   git clone https://github.com/Vertex-Wu/AuraWeather.git
   ```
2. 进入项目根目录。
3. 双击运行根目录下的 `build.bat`（或在终端执行 `./build.bat`）。
4. 编译完成后，根目录下会生成 `AuraWeather.exe`。

注意事项：
- 编译脚本强制使用了 `-static` 参数以确保生成单文件绿色版。
- 执行 `build.bat` 时如果出现中文乱码或闪退，请检查文件的文本编码是否为 `ANSI` 或 `GBK`。
- 包含静态链接过程可能耗时较久，请耐心等待终端自动关闭。

## 数据来源与隐私

- 天气数据：MET Norway (挪威气象局) 逐小时预报
- 城市搜索：Open-Meteo Geocoding API
- 卫星云图：NASA GIBS 公开影像服务
- 隐私保护：所有数据均来自公开服务，应用仅在本机缓存天气与云图，绝不上传任何个人数据。

## 杀毒软件误报声明

由于本软件涉及网络请求、注册表写入（用于开机自启），且为无数字签名的静态编译单文件，部分杀毒软件（如卡巴斯基、Windows Defender）可能会因启发式扫描而误报。
本软件代码已全部开源，请放心添加信任白名单。

## 开源协议

本项目基于 [MIT License](LICENSE) 开源。
