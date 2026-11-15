# AuraWeather

![C++](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Platform](https://img.shields.io/badge/Platform-Windows%2010%2F11-lightgrey.svg)
![Runtime](https://img.shields.io/badge/Runtime%20Dependencies-Zero-red.svg)
![License](https://img.shields.io/badge/License-MIT-green.svg)

AuraWeather 是一款基于纯 C++ (Win32 API) 开发的 Windows 原生桌面天气软件。不使用第三方 UI 框架，最终交付为静态编译的独立 `.exe` 文件。

## 图片
<img width="1439" height="855" alt="image" src="https://github.com/user-attachments/assets/64ba3b8a-f3f9-4d5f-9d5a-36909337688a" />
<img width="1439" height="861" alt="image" src="https://github.com/user-attachments/assets/b70fb8b4-eab8-4ab6-b974-12a4eec83d9c" />

## 软件介绍

AuraWeather 采用原生 GDI/GDI+ 渲染，具有较低的内存占用，并适配了系统的暗黑/亮色主题与高 DPI 缩放。软件提供标准的 Windows 安装与卸载体验，自带多重网络容错更新检测，并强制将数据保存在安装目录，确保系统盘不被污染。

### 完整功能模块

- **概览**：显示当前温度、体感温度、降水、云量及紫外线指数，右侧展示大尺寸天气图标。
- **逐小时**：展示未来 12 小时及 48 小时的详细温度与降水趋势。
- **十日预报**：展示未来 10 天的天气状况与温度区间。
- **日出与月相**：独立标签页展示日出日落轨迹弧线，以及当前月相状态（白天同样生效，表示当天的月相）。
- **本地赛博预警**：无需第三方接口。根据风速、降水、温度、能见度等数据本地计算阈值，若触发雷暴、狂风、暴雨雪、极寒高温、重度污染等极端天气，将在界面顶部显示红色预警横幅。
- **智能生活建议**：严格基于未来 3 小时数据，分析生成带伞、防晒、穿衣、运动、洗车等针对性出行建议。
- **多源更新检测**：内置基于 GitHub API 的检查更新功能，并提供 `ghproxy.net` 与 `ghfast.top` 镜像备用，防止国内网络屏蔽。

## 如何使用（普通用户）

1. 前往本仓库右侧的 **Releases** 页面。
2. 下载最新的 `AuraWeather_Setup_vX.X.X.exe` 安装包。
3. 双击运行，选择你希望安装的目录，完成安装。
4. 若旧版本正在后台运行，安装程序会提示并自动关闭旧进程，实现平滑覆盖升级，原有设置与缓存不会丢失。

## 开发者编译与打包指南

### 1. 编译环境依赖
- 操作系统：Windows 10 / 11
- 编译工具链：[MSYS2](https://www.msys2.org/) (UCRT64 环境) 或 MinGW-w64。确保 `g++` 和 `windres` 已加入系统 `PATH`。
- 第三方库：OpenSSL 静态库（用于 HTTPS 网络请求）。

在 MSYS2 终端中安装 OpenSSL 依赖：
```bash
pacman -S mingw-w64-ucrt-x86_64-openssl
```

### 2. 编译主程序
1. 克隆本仓库到本地：
   ```bash
   git clone https://github.com/Vertex-Wu/AuraWeather.git
   ```
2. 进入项目根目录。
3. 双击运行根目录下的 `build.bat`（或在终端执行 `./build.bat`）。
4. 编译完成后，根目录下会生成 `AuraWeather.exe`。

### 3. 打包安装程序（可选）
如果你希望自行生成 `.exe` 安装包，请先下载并安装 [Inno Setup](https://jrsoftware.org/isinfo.php)。
安装完成后，在项目根目录双击运行 `installer.iss`，点击顶部的编译按钮（或按 F9），即可生成安装包。

> **注意事项**：
> - 编译脚本强制使用了 `-static` 参数以确保生成单文件。
> - 执行 `build.bat` 时如果出现中文乱码或闪退，请检查文件的文本编码是否为 `ANSI` 或 `GBK`。

## 数据来源与隐私保护

- 天气数据：MET Norway (挪威气象局) 逐小时预报
- 城市搜索：Open-Meteo Geocoding API
- 隐私保护：所有数据均来自公开服务，应用仅在本机缓存天气，绝不上传任何个人数据。

## 关于安全警告的声明

1. **UAC / SmartScreen 拦截**：本软件为个人开源项目，未购买微软代码签名证书。双击安装包时若提示“未知发布者”，请点击“更多信息”并选择“仍要运行”。
2. **杀毒软件误报**：由于本软件包含网络请求、注册表写入（开机自启功能）且为无签名单文件，部分杀毒软件（如卡巴斯基、Windows Defender）可能报毒（如 `HEUR:Trojan.Win64.Agent.pef`）。代码已完全开源，请放心添加信任白名单。
3. **高级调试参数**：为便于自动化测试，程序内部保留了以 `--debug` 为前缀的命令行参数，普通用户正常双击运行不会触发。

## 开源协议

本项目基于 [MIT License](LICENSE) 开源。
```
