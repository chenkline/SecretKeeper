# 密匣（SecretKeeper）

一款**完全离线**的个人机密信息加密应用，用于保管网站账号密码、访问密钥、银行账号等敏感数据。

> 应用不联网、不上报任何数据。你的机密只留在你自己的设备上。

## 特性

- **主密钥体系**：RSA-2048 密钥对，私钥由 Argon2id 派生的 KEK 加密保护；支持多主密钥与默认主密钥切换。
- **逐条独立加密**：每条机密信息使用独立的 AES-256 数据密钥，主密钥公钥仅用于封装数据密钥。
- **密钥不落明文**：主密钥的公钥与私钥均以 KEK 加密后存储，磁盘上不存在明文密钥材料。
- **跨平台通用**：Windows / Linux / macOS / Android / iOS 五端原生实现，**数据文件格式完全一致**，同一份文件可在任意平台读写。
- **安全加固**：默认主密钥 KEK 内存缓存轮换与诱饵槽位、闲置自动锁定、剪贴板定时清除、密码错误指数退避。
- **纯软件密码学**：桌面三端共用 vendor 的 mbedTLS，不依赖宿主系统是否提供非对称算法，任何环境下 RSA 行为一致。

## 当前状态

**v0.0.1（基础功能版本）** — 文档设计阶段。

五端技术栈已锁定：

| 平台 | 语言 | UI |
|---|---|---|
| Windows | C++17 | FLTK 1.4.5 |
| Linux | C++17 | FLTK 1.4.5 |
| macOS | C++17 | FLTK 1.4.5 |
| Android | Kotlin | Jetpack Compose |
| iOS | Swift | SwiftUI |

桌面三端（Windows / Linux / macOS）**共享同一套 C++ 业务与界面代码**，一致性由代码共享
直接保证；Android 与 iOS 保持各自的原生实现，靠统一的数据格式规范与黄金测试向量对齐。

密码学方面，桌面三端固定使用 **mbedTLS 3.6.7**（源码 vendor 在 `vendor/mbedtls/`，
Apache-2.0），Argon2id 来自 `vendor/argon2`；移动两端不强制，但必须逐字节通过同一份黄金向量。

界面框架统一为 **FLTK 1.4.5**（LGPL-2.1，源码 vendor 在 `vendor/fltk/`），静态链接，
产物是不依赖任何第三方 DLL 的单文件可执行程序。

core 分四层单向依赖（crypto / serialize / store / service），UI 只经 service 门面访问。
Windows 端五目标自检均已通过，**RSA 路径真实执行、0 跳过**：

| 层 | 自检项数 |
|---|---|
| crypto（密码学原语） | 43 |
| serialize（字节级格式） | 43 |
| store（索引与数据文件） | 84 |
| service（业务门面） | 85 |
| UI 纯逻辑 | 29 |
| 黄金向量（`scripts/verify-vectors.py`） | 91 |

合计 **284 项自检 + 91 项向量 + 57 项一致性探针**。

`scripts/check-layering.py` 静态断言分层方向不被回潮；
`scripts/compare-conformance.py` 逐行比对各平台跑同一份密码学代码产出的
transcript，证明相同输入在任何平台都得到相同字节 —— 这是向量校验做不到的，
因为向量校验用的是另一套独立实现。

## 编译指南

三端共用根目录的 `CMakeLists.txt`。产物目录统一为：

```
build/{platform}/{arch}/{Debug,Release}          库与可执行程序
build/{platform}/{arch}/{Debug,Release}/vendor/{module}   内置第三方各自的产物
```

例如 `build/windows/x64/Release/` 与 `build/windows/x64/Release/vendor/mbedtls/`。

两种生成器的区别：**MSVC 是多配置生成器**，CMake 会在输出目录后
自动追加 `Debug`/`Release`，配置名通过 `--config` 指定；**Ninja 是单配置生成器**，
配置名必须写在目录里（`-DCMAKE_BUILD_TYPE=`），否则两种配置的产物会互相覆盖。

### Windows（MSVC）

需要 Visual Studio 2022 的 C++ 工具链与 CMake 3.20+。首先进入开发者命令提示符（
`x64 Native Tools Command Prompt`，或自行运行 `VsDevCmd.bat`）：

```
cmake -S . -B build/windows/x64 -A x64
cmake --build build/windows/x64 --config Debug
cmake --build build/windows/x64 --config Release
ctest --test-dir build/windows/x64 -C Debug
ctest --test-dir build/windows/x64 -C Release
```

PowerShell 中若中文显示为乱码，先执行 `. .\scripts\ps-profile.ps1`。

面向 ARM64 交叉编译（需要 `Microsoft.VisualStudio.Component.VC.Tools.ARM64`）：

```
cmake -S . -B build/windows/arm64 -A ARM64
cmake --build build/windows/arm64 --config Release
```

交叉编译凭据不足时，mbedTLS 的 AES 加速会自动切到 Armv8 实现（不需要任何手工配置）。

### Linux

需要 CMake 3.20+ 与 Ninja，以及 FLTK 的 X11 依赖：

```
sudo apt-get install -y cmake ninja-build \
  libx11-dev libxext-dev libxft-dev libxinerama-dev \
  libxcursor-dev libxrender-dev libfontconfig1-dev libfreetype6-dev
```

配置与构建（把 `Release` 换成 `Debug` 即可得到调试版）：

```
cmake -S . -B build/linux/x64 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux/x64 --parallel
ctest --test-dir build/linux/x64 --output-on-failure
./build/linux/x64/Release/SecretKeeper
```

ARM64 目标（需要交叉编译器）：

```
sudo apt-get install -y g++-aarch64-linux-gnu
cmake -S . -B build/linux/aarch64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
  -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++
cmake --build build/linux/aarch64 --parallel
```

只验证核心四层（不需要界面，因此不需要 X11 开发库）：加 `-DSK_BUILD_APP=OFF`。

### macOS

需要 Xcode Command Line Tools 与 CMake 3.20+：

```
xcode-select --install
cmake -S . -B build/macos/arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/macos/arm64 --parallel
ctest --test-dir build/macos/arm64 --output-on-failure
./build/macos/arm64/Release/SecretKeeper
```

Apple Silicon 上架构为 `arm64`；Intel Mac 改为 `build/macos/x64` 即可。

### 产物检查

Windows 下可确认产物不依赖任何第三方 DLL：

```
dumpbin /dependents build/windows/x64/Release/SecretKeeper.exe
```

出现 `fltk` / `msvcp` / `vcruntime` 等非系统 DLL 则说明静态链接失败。

### 运行自检

五个分层测试目标由 CTest 驱动，与生产代码的依赖方向一致（每层只测本层及之下）：

| 目标 | 覆盖层 |
|---|---|
| `sk_crypto_test` | 密码学原语 + 黄金向量 |
| `sk_serialize_test` | 字节级格式往返与负例 |
| `sk_store_test` | SQLite 索引、原子写入、路径推导 |
| `sk_service_test` | 需求 1/2/3 的全部业务用例 |
| `sk_ui_test` | UI 层纯逻辑（不启动 FLTK 事件循环） |

释放 UI 界面可用 `-DSK_BUILD_APP=OFF`，仅构建前四个目标。

## 文档

全部文档位于 [docs/](docs/README.md)，建议从 [总体技术设计](docs/01-architecture/总体技术设计.md) 开始阅读。

| 文档 | 内容 |
|---|---|
| [需求原文](docs/机密信息加密应用需求功能设计.txt) | 产品需求 |
| [总体技术设计](docs/01-architecture/总体技术设计.md) | 系统分层、技术选型、模块划分 |
| [仓库结构与工程组织](docs/01-architecture/仓库结构与工程组织.md) | monorepo 布局与各平台工程 |
| [密码学与密钥派生设计](docs/02-crypto/密码学与密钥派生设计.md) | 密钥层级、Argon2id、内存安全 |
| [数据格式与存储设计](docs/03-data/数据格式与存储设计.md) | **字节级格式规范（唯一真相源）** |
| [机密信息与主密钥功能设计](docs/04-requirements/机密信息与主密钥功能设计.md) | v0.0.1 功能规格与提示文案 |
| [开发环境与CI方案](docs/05-development/开发环境与CI方案.md) | 环境安装清单与 CI 设计 |
| [测试与验收方案](docs/07-testing/测试与验收方案.md) | 测试向量与验收标准 |
| [版本规划与扩展预留](docs/06-roadmap/版本规划与扩展预留.md) | v1.0.0 二维码/License 预留 |

## 参与开发

**请先阅读 [AGENTS.md](AGENTS.md)** —— 其中约定了跨平台一致性铁律、密码学决策与提交要求。

由于 macOS / iOS / Android / Linux 的构建环境无法在单台 Windows 11 机器上齐备，仓库通过 **GitHub Actions** 完成五端构建与测试。本地仅验证 Windows 端。

## 安全声明

- 本软件按"完全离线"原则设计，不包含任何网络通信代码。
- 请务必妥善保管主密钥密码：**密码遗失则数据永久无法恢复**，开发者无法找回。
- 建议定期导出主密钥与机密信息并离线备份。
