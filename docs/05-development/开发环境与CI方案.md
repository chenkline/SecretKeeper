# 开发环境与 CI 方案

## 1. 问题陈述

开发主机为 **Windows 11**，但项目需构建五个平台的产物：

| 平台 | 可在 Windows 上构建？ |
|---|---|
| Windows | 可以 |
| Linux | 不可（无原生 Linux 构建链） |
| macOS | **绝对不可**（需 Xcode，仅限 macOS） |
| Android | 部分（Gradle + JDK 可交叉构建 APK） |
| iOS | **绝对不可**（需 macOS + Xcode + 签名） |

**结论：必须依靠 GitHub Actions 的托管 runner 来构建与测试非 Windows 平台。** 本地只负责 Windows 端与文档编辑。

## 2. 本地环境（Windows 11）

### 2.1 必装

| 软件 | 版本 | 用途 |
|---|---|---|
| Visual Studio 2022 | 17.11+ | C++ 编译器与 MSBuild（装 **"使用 C++ 的桌面开发"** 工作负载） |
| Windows SDK | 10.0.22621+ | 平台熵 `BCryptGenRandom` |
| CMake | 3.28+ | 桌面三端统一构建脚本 |
| Git | 2.43+ | 版本控制 |
| Python | 3.11+ | 校验脚本、生成测试向量 |

框架（FLTK）与密码学库（mbedTLS / Argon2 / SQLite）全部为 `vendor/` 内置源码，
**无需任何包管理器安装**。

### 2.2 可选

| 软件 | 用途 |
|---|---|
| SQLite 命令行工具 | 手工检查索引库 |
| winget / Scoop | 包管理 |
| GnuPG | 若未来 License 模块需要签名 |
| FLTK | **不需安装**，`vendor/fltk` 内置源码 |
| mbedTLS / Argon2 / SQLite | **不需安装**，全部为 `vendor/` 内置源码 |

### 2.3 不需要在本地安装

- Xcode（iOS / macOS）→ 用 GitHub Actions
- Linux 的 X11 开发包 → 用 GitHub Actions
- Android SDK → 可本地装，但**优先用 CI**，避免版本漂移
- Linux 交叉编译工具链 → 用 GitHub Actions
- mbedTLS / Argon2 / SQLite 的开发包 → **不需要**，三者均 vendor 在仓库内并由构建脚本直接编译

### 2.4 建议的 winget 安装命令

```powershell
winget install --id Microsoft.VisualStudio.2022.Community
winget install Kitware.CMake
winget install Git.Git
winget install Python.Python.3.12
```

安装 Visual Studio 时务必勾选 **"使用 C++ 的桌面开发"**，否则缺少 MSVC 工具链。

### 2.5 PowerShell 的 UTF-8 环境

Windows 控制台默认代码页不是 UTF-8，直接跑构建脚本时 Python 与 C++ 的中文输出会乱码，
进而让脚本以非零码退出。仓库提供 `scripts/ps-profile.ps1`，在**每个新开的 PowerShell 会话
开始时**执行一次即可：

```powershell
. .\scripts\ps-profile.ps1
```

它会设置 `chcp 65001`、把控制台输入输出编码与 `$OutputEncoding` 统一为无 BOM UTF-8，
并设置 `PYTHONUTF8=1` / `PYTHONIOENCODING=utf-8`。CI 脚本内部自行完成同样的设置，
不依赖该 profile。

## 3. GitHub Actions 方案

### 3.1 总原则

- **每个平台一个独立 job**，并行执行。
- 所有 job 必须跑 **黄金测试向量**（见[测试与验收方案](../07-testing/测试与验收方案.md)）。
- 密码学与格式相关的测试任一失败即整体失败。
- 拉取请求必须全绿才能合并。

### 3.2 Job 设计

| Job | Runner | 工具链 | 产物 |
|---|---|---|---|
| **windows** | `windows-2022` | VS 2022 + CMake（+ `build.ps1` 直编自检） | 单文件 exe |
| **windows-arm64** | `windows-2022` | VS 2022 ARM64 交叉工具链（+ `build.ps1 -Architecture arm64 -CompileOnly`） | 仅编译产物，不出包 |
| **linux** | `ubuntu-24.04` | X11 开发包 + CMake + Ninja + mbedTLS（vendor 同源） | tar.gz |
| **macos** | `macos-15` | Xcode 16 命令行工具 + CMake + mbedTLS（vendor 同源） | tar.gz |
| **android** | `ubuntu-24.04` | JDK 17 + Android SDK 34 + Gradle | .apk |
| **ios** | `macos-15` | Xcode 16 + SwiftPM | .xcarchive（未签名） |
| **vectors** | `ubuntu-24.04` | Python 校验脚本 | 校验报告 |

**关于 windows-arm64**：本机 VS BuildTools 未安装 `Microsoft.VisualStudio.Component.VC.Tools.ARM64`，
而项目约定不得在开发机上补装工具，因此 ARM64 交叉编译完全交给 runner。该 job **只编译不运行**
（x64 宿主无法执行 ARM64 二进制），功能测试仍由 x64 job 承担。由于 runner 镜像是否预装 ARM64
交叉工具链并无可靠保证，job 先用 `vswhere -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64`
探测，缺失再调 `vs_installer.exe modify` 幂等补装，不假设预装。

**关于 iOS 签名**：CI 只产出**未签名**构建（`CODE_SIGNING_ALLOWED=NO`）。正式分发需在 macOS 本机用开发者证书签名，签名流程不放入公开 CI，避免证书泄露。

### 3.3 触发时机

- 推送到主干分支
- 发起 Pull Request
- 手动触发
- **每日定时全量构建**（用于捕捉上游依赖的破坏性变更，如 Gradle 发布新版本）

### 3.4 缓存策略

| 缓存项 | 缓存键依据 |
|---|---|
| Gradle | `gradle/libs` |
| SwiftPM | `spm/<Package.resolved 哈希>` |
| CMake 构建目录 | `cmake/<job>-<分支>` |

缓存键必须包含 **lock 文件的哈希**，依赖变更时自动失效。

### 3.5 依赖版本锁定

**这是跨平台一致性的最大风险来源。** 各端密码库大版本升级可能改变默认参数。

要求：
- 五个平台的依赖版本在仓库中显式锁定（`pubspec.lock`、`gradle/libs.versions.toml`、`Package.resolved`、CMake `FetchContent` 的 `GIT_TAG` 等）。
- **密码学库例外**：Windows 与 Linux 使用 vendor 的 mbedTLS 3.6.7，版本随仓库代码走，不通过包管理器解析，因此不存在「宿主升级导致行为变化」的风险。裁剪配置在 `mbedtls_config.h` 末尾的 override 块中，随代码一起 review。
- **密码学相关依赖的升级必须单独提交**，并附五端黄金向量全绿证明。
- 定时构建用于发现上游被动变更。

## 4. 本地与 CI 的职责划分

| 工作 | 本地（Windows） | CI |
|---|---|---|
| 文档编写与评审 | 可以 | — |
| Windows 端开发与调试 | 可以 | 验证 |
| Linux / macOS / iOS 端开发 | 只写代码 | **编译 + 测试** |
| Android 端开发 | 可选 | **编译 + 测试** |
| 黄金测试向量 | 可跑本机子集快速回归 | **五端全量** |
| 性能基准 | 仅供参考（不代表移动端） | 各 runner 实测 |
| 发布打包 | 仅 Windows | 其余平台 |

### 4.1 工作循环

1. 在 Windows 本地开发，先跑本机向量子集做秒级回归。
2. 提交并推送，等待 CI 全绿。
3. 若某个非 Windows 端失败，**修复代码后重新提交**（CI 无法在 PR 内调试）。
4. 合并前确认所有 job 绿色。

### 4.2 本地快速回归

本仓库现成的轻量回归入口有两个：

- `python scripts/verify-vectors.py` —— 校验黄金向量（91 项），秒级
- `windows/scripts/build.ps1 -Test` —— 编译并运行 Windows 端四层自检

全量仍以 CI 为准。

## 5. 仓库设置

需在 GitHub 仓库设置中开启：

- Actions 权限（若从模板创建，需确认 Actions 未被禁用）
- 分支保护：主干分支要求 CI 必须通过才能合并
- 建议开启 Dependabot 监控依赖更新，但**密码学依赖不自动合并**，需人工评审

## 6. 密钥与凭据

- **应用仓库不需要任何密钥**。应用完全离线，构建过程不涉及签名凭据（iOS 分发签名除外）。
- 若将来引入 License 签名服务，其密钥**绝不**进入仓库，使用 GitHub Secrets 管理。
- 黄金测试向量中的密码与密钥**全部是固定的假数据**，可公开。
