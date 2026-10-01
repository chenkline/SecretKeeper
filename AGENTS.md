# AGENTS.md

本文件为密匣（SecretKeeper）加密应用仓库的代理协作指南。**在修改任何代码前，请先完整阅读本文件与 [docs/README.md](docs/README.md)。**

---

## 1. 项目定位

密匣是一款**完全离线**的个人机密信息加密应用，用于保管网站账号密码、密钥、银行账号等敏感数据。

核心诉求（按优先级）：

1. **安全**：机密信息与主密钥在磁盘上永远不以明文存在。
2. **离线**：不联网，不上报任何数据。
3. **跨平台通用**：五端数据文件格式完全一致，同一份文件可在任一平台读写。

---

## 2. 仓库结构

单仓库多工程（monorepo），每个平台一个**独立原生工程**，不共享 UI 代码。

```
Secret/
├── AGENTS.md
├── docs/                              # 全部文档
│   ├── README.md
│   ├── 机密信息加密应用需求功能设计.txt  # 需求原文（已按技术决策修订）
│   ├── 01-architecture/
│   ├── 02-crypto/
│   ├── 03-data/
│   ├── 04-requirements/
│   ├── 05-development/
│   ├── 06-roadmap/
│   └── 07-testing/
├── test-vectors/                      # 跨平台黄金测试向量（单一真相源）
├── windows/                           # C++/WinUI 3 (C++/WinRT)
├── linux/                             # C / Qt
├── macos/                             # Swift / AppKit
├── android/                           # Kotlin / Jetpack Compose
├── ios/                               # Swift / SwiftUI
└── .github/workflows/                 # GitHub Actions CI
```

---

## 3. 各平台技术栈（已锁定，不得擅自更换）

| 平台 | 语言 | UI 框架 | 备注 |
|---|---|---|---|
| Windows | C++ | WinUI 3 / C++/WinRT | MSVC + Windows SDK 11 |
| Linux | C | Qt | Qt 仅用于 Linux |
| macOS | Swift | AppKit | |
| Android | Kotlin | Jetpack Compose | 必须原生，禁止跨平台方案 |
| iOS | Swift | SwiftUI | 必须原生，禁止跨平台方案 |

**铁律：五个平台的实现彼此独立，不共享业务/UI 代码。** 唯一共享的是 `test-vectors/` 中的测试数据与 `docs/03-data/` 中的格式规范。

---

## 4. 密码学决策（已锁定）

| 项目 | 决策 |
|---|---|
| 密钥派生 | Argon2id，m=10MiB、t=3、p=1，输出 32 字节 |
| 盐 | **每条记录独立 16 字节随机盐，明文随记录保存** |
| 主密钥 | RSA-2048 密钥对 |
| 封装数据密钥 | RSA-OAEP-SHA256（用主密钥**公钥**加密） |
| 解封装数据密钥 | RSA-OAEP-SHA256（用主密钥**私钥**解密） |
| 保护公钥 | KEK + AES-256-GCM（公钥也加密存储，各自独立随机 nonce） |
| 保护私钥 | KEK + AES-256-GCM |
| 机密信息加密 | 每条独立 AES-256 数据密钥 + AES-256-GCM |
| 国密 SM2 | **v0.0.1 不实现**，格式已预留算法标识字段 |

**性能约束**：单次密钥派生 ≤300ms，峰值内存 ≤10MiB，推导完成后立即清零缓冲区。

### 密钥层级

```
用户密码 ──Argon2id(盐)──> KEK ──AES-256-GCM──> 主密钥私钥
                                                      │
                                        RSA-OAEP(公钥) │
                                                      ▼
                                              数据密钥 ──AES-256-GCM──> 机密信息
```

### 关于"相同密码推出相同 KEK"的正确理解

需求原文要求"相同输入密码每次推导出相同的 KEK"。本仓库采用**每条记录随机盐**方案：同一份记录 + 同一密码，在任何设备、任何平台、任何版本上得到相同 KEK（因而可解密互认），但不同记录即使密码相同也得到不同 KEK。

该方案既满足跨平台一致性要求，又避免相同密码导致 KEK 相同、进而一处泄露处处泄露。**不要**把盐做成全局固定值。

---

## 5. 跨平台一致性铁律

数据格式与密码学行为必须五端完全一致。为保证这一点：

1. **唯一真相源**是 [docs/03-data/数据格式与存储设计.md](docs/03-data/数据格式与存储设计.md) 中的字节级规范。任何格式变更必须先改该文档。
2. **黄金测试向量**放在仓库根 `test-vectors/`，五端共用同一份文件。每个向量包含输入、期望输出、期望错误。
3. 任一平台实现若与规范不符，**以规范为准修正实现**，而非反向修改规范去迁就某个平台。
4. 任何新增或修改的密码学参数必须同步更新 `test-vectors/` 并让五端同时通过。
5. 引入第三方密码库时，必须确认其默认参数与本仓库一致（尤其是 RSA-OAEP 的哈希与填充方式、GCM 的 IV 长度）。

---

## 6. 开发工作流

1. **先读文档**：改动数据格式 → 必读 `docs/03-data/`；改动密码学 → 必读 `docs/02-crypto/`；改动交互 → 必读 `docs/04-requirements/`。
2. **先改规范，再改实现**：格式与算法层面的变更顺序不可颠倒。
3. **同步测试向量**：密码学或格式变更必须同时更新 `test-vectors/`。
4. **本地只验证 Windows**：当前开发环境为 Windows 11，其他四端依赖 GitHub Actions 验证。提交后必须确认 CI 全绿。
5. **不提交密钥材料**：任何真实密码、真实密钥、真实导出文件都不得进仓库，测试一律使用固定假数据。

---

## 7. 限额（v0.0.1 基础版）

| 项目 | 上限 |
|---|---|
| 主密钥数量 | 2 |
| 机密信息条数 | 15 |
| 单条机密信息长度 | 150 字符 |

超出上限时，用户须先导出再删除部分记录。以上限额在 v1.0.0 中由 License 控制，**实现时必须把限额读取为可配置常量，不得硬编码散落在各处**。

---

## 8. 代码约定

- 密钥材料一律使用可清零的缓冲区（C++ 用 `explicit_bzero`、Swift 用 `Data.resetBytes`、Java 用 `Arrays.fill`），**不得**用不可清零的 `String` 或 `const char*` 承载密码或密钥。
- 密码输入框禁用自动纠错与自动大写，按平台惯例实现掩码输入。
- 解密失败必须能区分"认证失败"与"数据损坏"，但**对外提示文案按需求文档固定**，不得泄露内部细节。
- 注释与文档使用中文，与现有需求文档保持一致。

---

## 9. 提交与 CI

- 提交信息使用中文或 Conventional Commits，说明"改了什么、为什么"。
- CI 必须覆盖：五端构建 + 五端黄金测试向量校验 + 格式往返测试。
- 合并前要求所有 job 通过。

### 9.1 Windows runner 的三个坑（已踩，勿重犯）

| 坑 | 现象 | 处理 |
|---|---|---|
| 控制台非 UTF-8 | Python 打印中文抛 `UnicodeEncodeError`，C++ `Write-Host` 抛 `NativeCommandFailed`，job 直接变红 | 在脚本**内部**重配（`sys.stdout.reconfigure` / `chcp 65001`），不要依赖 CI 环境变量，这样本地与 CI 行为一致 |
| VS 路径写死 | runner 镜像的 Visual Studio SKU 随镜像更新变化，写死 `Enterprise`/`Community` 路径迟早失效 | 用 `vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64` 查询，失败再递归兜底 |
| SDK 装不上 | Android job 装 SDK 要下数 GB 且需交互式接受许可协议，许可步骤失败就让整个 job 变红，而此时并无东西需要构建 | 先探测工程是否存在，不存在则整段跳过 |

测试输出必须**全 ASCII**。中文注释在源文件里没问题，但 `printf` 出去的中文在
GBK 控制台上会炸。

### 9.2 内置第三方源码

| 目录 | 来源 | 为什么内置 |
|---|---|---|
| `vendor/argon2/` | Argon2 官方参考实现 20190702（CC0/Apache-2.0） | Windows CNG 不提供 Argon2id，且五端必须编译同一份实现才能保证跨端一致 |
| `vendor/sqlite/` | SQLite amalgamation 3.45.0（public-domain dedication） | 五端编译同一份 `sqlite3.c`，避免各端 SQLite 版本差异造成行为分叉 |

两者都**必须入库**（`.gitignore` 已加例外）。本机缺少对应开发工具，且项目约束
不安装新工具。改前先读各自的 `README.md`。

### 9.3 本机与 CI 的环境差异

- VS BuildTools 在**非默认路径** `D:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`（不是 `Program Files (x86)`）
- 本机**无 CMake**，Windows 构建走 `windows/scripts/build.ps1`；Linux/macOS 走 CMake/SwiftPM
- Python 位于 `D:\veighna_studio\`，已装 `argon2-cffi` 与 `cryptography`
- 系统有本地代理 `127.0.0.1:7899`，git 与 GitHub API 需走它；直连 github.com:443 会间歇超时
- 推送认证用 `.local/context.md` 里的 token，经 `http.https://github.com/.extraheader` 传入；该文件已被 gitignore，**严禁提交**

### 9.4 CNG 缺少非对称算法（已知环境限制）

Windows 11 build 22631 与 GitHub `windows-2022` runner 的 CNG **均不提供**
RSA/ECDH/ECDSA/DH/DSA：

- 一律返回 `STATUS_NOT_SUPPORTED (0xC00000BB)`
- `BCryptGenerateKeyPair` 返回成功，但后续 `BCryptExportKey` / `BCryptEncrypt`
  返回 `STATUS_INVALID_HANDLE (0xC0000008)`
- AES / SHA256 / ChaCha20-Poly1305 正常

因此**本机与 CI 都无法执行 RSA 测试**。`cng_has_asymmetric_support()` 会主动探测
并让测试显式跳过。**完整 RSA 覆盖需要其他环境**，不要误以为测试全绿就代表
RSA 路径正确。

---

## 10. 文档索引

| 文档 | 内容 |
|---|---|
| [docs/README.md](docs/README.md) | 文档总索引与阅读顺序 |
| [总体技术设计](docs/01-architecture/总体技术设计.md) | 系统分层、技术选型、模块划分 |
| [仓库结构与工程组织](docs/01-architecture/仓库结构与工程组织.md) | monorepo 布局与各平台工程 |
| [密码学与密钥派生设计](docs/02-crypto/密码学与密钥派生设计.md) | 密钥层级、Argon2id、内存安全 |
| [数据格式与存储设计](docs/03-data/数据格式与存储设计.md) | **字节级格式规范（唯一真相源）** |
| [机密信息与主密钥功能设计](docs/04-requirements/机密信息与主密钥功能设计.md) | v0.0.1 功能规格与提示文案 |
| [开发环境与CI方案](docs/05-development/开发环境与CI方案.md) | 环境安装清单与 CI 设计 |
| [版本规划与扩展预留](docs/06-roadmap/版本规划与扩展预留.md) | v1.0.0 二维码/License 预留 |
| [测试与验收方案](docs/07-testing/测试与验收方案.md) | 测试向量与验收标准 |
