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
├── vendor/                            # 内置第三方源码（五端共用同一份，见 9.2）
│   ├── argon2/                        # Argon2id 参考实现
│   ├── mbedtls/                       # mbedTLS 3.6.7
│   └── sqlite/                        # SQLite amalgamation
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
| Windows | C++ | WinUI 3 / C++/WinRT | MSVC + Windows SDK 11，密码学用 mbedTLS |
| Linux | C | Qt | Qt 仅用于 Linux，密码学用 mbedTLS |
| macOS | Swift | AppKit | |
| Android | Kotlin | Jetpack Compose | 必须原生，禁止跨平台方案 |
| iOS | Swift | SwiftUI | 必须原生，禁止跨平台方案 |

**铁律：五个平台的实现彼此独立，不共享业务/UI 代码。** 唯一共享的是 `test-vectors/` 中的测试数据与 `docs/03-data/` 中的格式规范。

---

## 4. 密码学决策（已锁定）

| 项目 | 决策 |
|---|---|
| 密码学库（Windows / Linux） | **mbedTLS 3.6.7**，源码 vendor 在 `vendor/mbedtls/`（Apache-2.0） |
| 其余三端密码学库 | 不强制，只要逐字节对齐黄金向量；建议同样用 mbedTLS 便于交叉比对 |
| 密钥派生 | Argon2id，m=10MiB、t=3、p=1，输出 32 字节（`vendor/argon2`，非 mbedTLS 提供） |
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
6. 任何密码学库必须**锁死版本并 vendor 入库**，不得依赖宿主系统的库版本。Windows 与 Linux 固定 mbedTLS 3.6.7；其余三端即便用系统库（OpenSSL / CryptoKit / java.security），也必须通过同一份黄金向量。

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

### 8.1 改动纪律（最小改动，铁律）

- **不得变更存量文件的换行符格式。** 仓库内换行符并不统一：绝大多数文件是 LF，
  但以下两个文件是 **CRLF**，改它们时必须逐字节保持 CRLF：
  - `windows/src/container/container.cpp`
  - `windows/src/container/container.h`

  弄错方向的后果一样严重：把 CRLF 改回 LF（或反之）会让 git 把整份文件判定为重写，
  既污染 review，也让真实改动被淹没。
- **不得重写整个文件。** 修改一律走"读取原文 → 精确替换锚点 → 原样写回"，
  禁止"解析成结构再序列化"这类会顺手归一化换行符、缩进或末尾空行的做法。
- 写文件固定用 `io.open(p, "w", encoding="utf-8", newline=<原换行符>)`，
  编码固定 UTF-8 **无 BOM**。
- 只改必须改的行。发现顺手的格式问题（过期表述、旧数字、错别字），
  若不在本次任务范围内，只在交付说明里指出，**不要顺手改**。

---

## 9. 提交与 CI

- 提交信息使用中文或 Conventional Commits，说明"改了什么、为什么"。
- CI 必须覆盖：五端构建 + 五端黄金测试向量校验 + 格式往返测试。
- 合并前要求所有 job 通过。

### 9.1 Windows runner 的三个坑（已踩，勿重犯）

| 坑 | 现象 | 处理 |
|---|---|---|
| 控制台非 UTF-8 | Python 打印中文抛 `UnicodeEncodeError`，C++ `Write-Host` 抛 `NativeCommandFailed`，job 直接变红 | 在脚本**内部**重配（`sys.stdout.reconfigure` / `chcp 65001`），不要依赖 CI 环境变量，这样本地与 CI 行为一致 |
| **Python stdout 默认 GBK** | 本机 PowerShell 7.6 已是 UTF-8（chcp 65001、`Console.OutputEncoding=utf-8`），原生命令中文也能正确捕获。此时读 Python 输出仍然乱码，根因是 `sys.stdout.encoding == "gbk"`，**不是控制台代码页** | 本机已设用户级 `PYTHONIOENCODING=utf-8` 与 `PYTHONUTF8=1`；脚本内仍应 `sys.stdout.reconfigure(encoding="utf-8")` 以保证 CI 与他人环境一致。排查乱码时先看 `sys.stdout.encoding`，不要先动 `chcp` |
| VS 路径写死 | runner 镜像的 Visual Studio SKU 随镜像更新变化，写死 `Enterprise`/`Community` 路径迟早失效 | 用 `vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64` 查询，失败再递归兜底 |
| SDK 装不上 | Android job 装 SDK 要下数 GB 且需交互式接受许可协议，许可步骤失败就让整个 job 变红，而此时并无东西需要构建 | 先探测工程是否存在，不存在则整段跳过 |

测试输出必须**全 ASCII**。中文注释在源文件里没问题，但 `printf` 出去的中文在
GBK 控制台上会炸。

### 9.2 内置第三方源码

| 目录 | 来源 | 为什么内置 |
|---|---|---|
| `vendor/argon2/` | Argon2 官方参考实现 20190702（CC0/Apache-2.0） | 五端必须编译同一份实现才能保证跨端一致。mbedTLS 不提供 Argon2id，故该实现独立保留 |
| `vendor/mbedtls/` | mbedTLS 3.6.7，仅 `library/` + `include/`（Apache-2.0） | Windows 与 Linux 两端必须同版本同源码，否则 OAEP / GCM 行为可能在细节上分叉 |
| `vendor/sqlite/` | SQLite amalgamation 3.45.0（public-domain dedication） | 五端编译同一份 `sqlite3.c`，避免各端 SQLite 版本差异造成行为分叉 |

三者都**必须入库**（`.gitignore` 已为 `vendor/argon2`、`vendor/mbedtls`、`vendor/sqlite`
加例外规则）。本机缺少对应开发工具，且项目约束不安装新工具。改前先读各自的 `README.md`。

**mbedTLS 裁剪纪律**：功能裁剪只允许追加在 `vendor/mbedtls/include/mbedtls/mbedtls_config.h`
**文件末尾的 override 块**里，不得内联改写上游原始行。这样上游文件保持字节一致，将来同步
新版时 diff 可读。当前裁剪：关闭 PSA_C、ECP/ECDSA/ECDH/DHM/LMS、CHACHA20/CCM、
AES 以外的分组密码；保留 RSA_C、PKCS1_V15/V21、BIGNUM、MD、SHA256、AES、GCM、
CIPHER_C、ASN1_PARSE、PKCS8、PK_PARSE、PK_WRITE、OID、CTR_DRBG、ENTROPY、PLATFORM_C。

**PSA 保持关闭**：`MBEDTLS_PSA_CRYPTO_C` 一旦打开，3.6 的经典入口仍可编译，但会引入 PSA
属性配置负担，且本项目不需要它。不要"顺手修好"这个 `#undef`。

**Windows 链接库是 `bcrypt.lib`**：`vendor/mbedtls/library/entropy_poll.c` 调用
`BCryptGenRandom` 取平台熵。`advapi32.lib` 是错的。

### 9.3 本机与 CI 的环境差异

- VS BuildTools 在**非默认路径** `D:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`（不是 `Program Files (x86)`）
- 本机**无 CMake**，Windows 构建走 `windows/scripts/build.ps1`；Linux/macOS 走 CMake/SwiftPM
- 本机 **MSVC 缺 ARM64 目标工具与库**（VS 组件 `Microsoft.VisualStudio.Component.VC.Tools.ARM64` 未安装），
  Windows SDK 的 arm64 库却是齐的。因此 ARM64 **只能交叉编译且只能走 CI**：
  `-Architecture arm64 -CompileOnly`（只编译不运行，x64 宿主执行不了 ARM64 二进制）。
  ARM64 目标下 Argon2 被强制切到 `ref.c`，不得带任何 x86 SIMD 宏。
- Python 位于 `D:\veighna_studio\`，已装 `argon2-cffi` 与 `cryptography`
- 系统有本地代理 `127.0.0.1:7899`，git 与 GitHub API 需走它；直连 github.com:443 会间歇超时
- 推送认证用 `.local/context.md` 里的 token，经 `http.https://github.com/.extraheader` 传入；该文件已被 gitignore，**严禁提交**

### 9.4 密码学库从 CNG 换成 mbedTLS（已解决）

**结论：已解决，盲区已消除。** 保留历史事实用于警示。

**当初的问题**：Windows CNG 在本机与 CI 上都无法执行 RSA。Windows 11 build 22631 与
GitHub `windows-2022` runner 的 CNG **均不提供** RSA/ECDH/ECDSA/DH/DSA：

- 一律返回 `STATUS_NOT_SUPPORTED (0xC00000BB)`
- `BCryptGenerateKeyPair` 返回成功，但后续 `BCryptExportKey` / `BCryptEncrypt`
  返回 `STATUS_INVALID_HANDLE (0xC0000008)`
- AES / SHA256 / ChaCha20-Poly1305 正常

即：**本机与 CI 都无法执行 RSA 测试**，只能让测试显式 SKIP。这不是可以忍受的状态，
因为 RSA 是主密钥的核心，SKIP 掉的路径等于没测。

**现在的做法**：密码学层全量换成 vendor 的 mbedTLS 3.6.7（纯软件，RSA 恒可用）。

- `has_asymmetric_support()` **恒返回 true**，改名自 `cng_has_asymmetric_support()`
  （共 8 处引用：`crypto.h`、`crypto.cpp`、`core/master_key_service.cpp`、
  `core/secret_service.cpp`、`tests/crypto_test.cpp` 2 处、`tests/service_test.cpp` 2 处）。
- 保留该函数而不是删掉，是为了让测试能**断言跳过路径永不触发**。它是构建缺陷的探针，
  不是能力开关。
- Windows 侧只需链接 `bcrypt.lib`（`BCryptGenRandom` 提供平台熵），不再依赖 CNG 密钥 API。

**换库后 RSA 路径首次真实执行**，四层测试 0 失败、0 跳过（见 9.6）。

### 9.5 mbedTLS 3.6 API 坑位（已实测踩过，勿重犯）

这 12 条每一条都曾导致**静默错误**——编译通过、测试通过或直接跳过，只有换库前后对照
才暴露出来。换用 mbedTLS 或升级其版本时，逐条对照。

| # | 坑 | 正确写法 |
|---|---|---|
| 1 | 只用**负数**表示错误 | `check()` 必须写 `rc < 0`，写 `rc != 0` 会把正数返回值当失败 |
| 2 | 部分入口成功时返回**写入长度**而非 0 | `mbedtls_pk_write_pubkey_der` / `mbedtls_pk_write_key_der` 属此类，必须用 `rc < 0` 判定 |
| 3 | DER 写入**从缓冲区末尾向前** | 编码起点是 `buf + size - len`，不是 `buf` |
| 4 | `mbedtls_asn1_write_mpi()` **已输出完整 INTEGER** | 含 tag+len+符号位补零+值，**不能再包一层** tag/len |
| 5 | 缓冲区逆向填充 ⇒ **必须先写 E 再写 N** | 否则得到 `SEQUENCE { e, n }` |
| 6 | 3.6 把 PKCS#1 RSAPublicKey 编解码移入 `rsa_internal.h`（非公开 API） | 公钥按 PKCS#1 存取，本地用公开 API（`asn1` / `asn1write` / `rsa_import` / `rsa_export`）自实现 `write_pkcs1_pubkey` / `parse_pkcs1_pubkey`；**不得**把私有头编入产品 |
| 7 | `mbedtls_gcm_crypt_and_tag()` **内部不设密钥** | 加密前必须 `mbedtls_gcm_setkey()`，且第四参单位是**位**不是字节（`key_len * 8`） |
| 8 | `argon2id_hash_raw` 参数顺序 | `(t_cost, m_cost, parallelism, pwd, pwdlen, salt, saltlen, hash, hashlen)`；且本版 vendor **没有** allocator 回调 API（内部直接 `malloc`），不要恢复已删除的 `argon_alloc`/`argon_free` |
| 9 | `mbedtls_pk_setup(ctx, info)` 只分配自己的 rsa 上下文 | 不接受外部 key；`mbedtls_rsa_info()` 已移除，改用 `mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)`；`MBEDTLS_PRIVATE(pk_ctx)` 展开为 `private_pk_ctx`，外部不可赋值。正确做法：`pk_setup` 后用 `mbedtls_pk_rsa()` 取回指针填充 |
| 10 | `mbedtls_rsa_gen_key` 是 **5 参无 seed** | `(ctx, f_rng, p_rng, nbits, exponent)`；生成后 `hash_id` 留 `MBEDTLS_MD_NONE`，**必须**显式 `mbedtls_rsa_set_padding(&rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256)`，否则得到一把无法封装任何东西的密钥 |
| 11 | GCM 错误宏无 `_DATA` 后缀 | 是 `MBEDTLS_ERR_GCM_BAD_INPUT` |
| 12 | OAEP 是 PSA 风格签名 | 封装 `(ctx, f_rng, p_rng, label, label_len, ilen, input, output)`；解封 `(ctx, f_rng, p_rng, label, label_len, olen, input, output, output_max_len)`。空 label（`nullptr, 0`）即标准 OAEP，与 `test-vectors/rsa-oaep.json` 的 `label: null` 一致；MGF1 与 OAEP 哈希均为 SHA-256。解析后校验 `mbedtls_pk_get_type(&pk) == MBEDTLS_PK_RSA` 再显式 `set_padding`，不依赖 DER 里恢复的哈希值 |

**两个被 CNG 掩盖的真实 bug**（不是 mbedTLS 的坑，但同样值得记）：

- **解密缓冲区长度算错**：GCM 是流模式，**密文长度 == 明文长度**。旧代码按密文向量总长
  （含 16 字节 tag 尾）分配输出，导致恢复出的 DER 尾部多出 16 个零字节，RSA 解析器直接
  拒绝。已新增 `plain_size()` 统一口径（`windows/src/core/master_key_service.cpp`，6 处调用）。
  `secret_service.cpp` 本来就减了尾，是对的。
- **测试 SKIP 文案误导**：旧的 SKIP 提示写 "this host CNG"，会让读者以为 CNG 环境限制仍在。
  现已改为 "RSA reports unavailable on this host / mbedTLS is pure software, so this
  indicates a build defect"。文档提到这段时须与之一致。

### 9.6 当前自检基线

| 层 | 位置 | 自检项数 | 失败 | 跳过 |
|---|---|---|---|---|
| 密码学层 | `windows/src/crypto/` | 86 | 0 | 0 |
| 存储层 | `windows/src/store/` | 81 | 0 | 0 |
| 核心层 | `windows/src/core/` | 178 | 0 | 0 |
| 业务层 | `windows/src/core/` | 54 | 0 | **0（RSA 真实执行）** |
| 黄金向量 | `scripts/verify-vectors.py` | 91 | 0 | — |

**"跳过 0" 是硬要求。** 任何一层出现 `skipped != 0` 都是构建缺陷，不是环境限制。

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
