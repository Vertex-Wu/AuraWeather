# AuraWeather

![C++](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Platform](https://img.shields.io/badge/Platform-Windows%2010%2F11-lightgrey.svg)
![Runtime](https://img.shields.io/badge/Runtime%20Dependencies-Zero-red.svg)
![License](https://img.shields.io/badge/License-MIT-green.svg)

AuraWeather 是一款基于纯 C++ (Win32 API) 打造的 Windows 原生桌面天气终端。拒绝第三方 UI 框架与臃肿的 Web 套壳，采用 GDI/GDI+ 原生渲染，为您提供极致轻量、极客感拉满的天气体验。

## 程序图片
<img width="1439" height="855" alt="image" src="https://github.com/user-attachments/assets/64ba3b8a-f3f9-4d5f-9d5a-36909337688a" />
<img width="1439" height="861" alt="image" src="https://github.com/user-attachments/assets/b70fb8b4-eab8-4ab6-b974-12a4eec83d9c" />


## 核心特性 (v2.0.5)

- **极简视觉重构**：概览页移除繁杂元素，主天气图标超大尺寸居中，视觉冲击力拉满。
- **独立天文模块**：新增“日出”与“月相”独立 Tab，查看太阳轨迹与月相演变互不干扰。
- **本地赛博预警**：无需依赖第三方付费接口，内置强对流、极端高低温、狂风、暴雨雪、雾霾等极端天气本地阈值判定与红色横幅警告。
- **智能 3 小时建议**：精准分析未来 3 小时数据，动态生成带伞、防晒、穿衣、运动等生活提示。
- **多源容错更新**：内置 GitHub API 与国内高速镜像（`ghproxy.net`、`ghfast.top`），一键检测更新。
- **纯净无污染**：标准安装包自带“智能覆盖升级”，所有配置与缓存强制锁定在安装目录，绝不写入系统 `AppData`。

## 用户安装指南

1. 前往本仓库右侧的 **Releases** 页面。
2. 下载最新的 `AuraWeather_Setup_vX.X.X.exe` 安装包。
3. 双击运行，选择安装路径即可。若旧版本正在后台运行，安装程序会智能提示并自动关闭它，实现无感平滑升级。

---

## 开发者编译与打包指南

本项目涉及原生 Windows API 与 OpenSSL 静态库，需配置以下编译环境。

### 1. 编译环境依赖
- **操作系统**：Windows 10 / 11
- **编译工具链**：[MSYS2](https://www.msys2.org/) (推荐 UCRT64 环境) 或 MinGW-w64。确保 `g++` 和 `windres` 已加入系统 `PATH`。
- **第三方库**：OpenSSL 静态库（用于 HTTPS 网络请求）。

在 MSYS2 终端中安装 OpenSSL：
```bash
pacman -S mingw-w64-ucrt-x86_64-openssl
