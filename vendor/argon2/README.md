# 内置第三方源码：Argon2 参考实现

## 为什么内置

跨五端一致性是本项目的硬约束（见 `AGENTS.md`）。Argon2id 有多个互不兼容的
实现与参数方言，一旦五端各自选库、各自调参，就可能出现"五端都实现了 Argon2id，
但彼此算出的 KEK 不一样"这种最难排查的问题。

因此 `vendor/argon2/` 内置 Argon2 官方参考实现的源码，**五个平台编译同一份 C 代码**，
再用 `test-vectors/kdf-argon2id.json` 做最终校验。

这也是必须的，而非仅仅是"更好"：**Windows 11 的 CNG 根本不提供 Argon2id**，
`BCryptOpenAlgorithmProvider(L"ARGON2ID")` 返回 `STATUS_NOT_FOUND`。
即便只看 Windows 一端，也没有系统 API 可用。

## 版本

| 项 | 值 |
|---|---|
| 上游 | https://github.com/P-H-C/phc-winner-argon2 |
| 版本 | 20190702（Argon2 v1.3，`ARGON2_VERSION_13 = 0x13`） |
| 许可 | 双许可 CC0-1.0 / Apache-2.0，许可证全文见 `vendor/argon2/LICENSE` |
| 引入文件 | `include/argon2.h`、`src/{argon2,core,encoding,thread}.c`、`src/ref.c` 或 `src/opt.c`、`src/blake2/*` |

`opt.c` 与 `ref.c` **二选一**（上游 Makefile 即如此规定）：两者都定义
`fill_segment`，同时编译会重复定义。`windows/scripts/build.ps1` 负责按 CPU
能力选择；其他平台的构建脚本需做同样处理。

## 修改纪律

**不要修改 `vendor/argon2/` 下的任何文件。** 需要升级版本时整体替换，并：

1. 重新运行 `python scripts/gen-vectors.py` 生成新向量
2. 确认 `test-vectors/kdf-argon2id.json` 的 6 条结果与旧版一致
   （Argon2 v1.3 的输出是标准化的，一致即说明升级未改变行为）
3. 若结果发生变化，必须视为**破坏性变更**：所有已生成的数据文件将无法解密，
   此时需要按 `docs/03-data/数据格式与存储设计.md` 的版本策略处理

## 同步脚本

如需重新拉取上游版本：

```bash
python scripts/vendor-argon2.py --version 20190702
```

该脚本只负责下载、解包与挑选文件，不修改任何 `vendor/argon2/` 下的内容。
