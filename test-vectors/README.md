# 黄金测试向量

本目录是**跨平台一致性的唯一共享资产**。五个平台的实现都必须加载这里的向量并向其对齐。

> **不要用项目自身的实现生成这些向量**，否则五端同时实现错误时无法发现。
> 向量由 `scripts/gen-vectors.py` 使用可信第三方实现（argon2-cffi / cryptography）生成。

## 文件说明

| 文件 | 覆盖内容 | 关键约束 |
|---|---|---|
| `kdf-argon2id.json` | Argon2id 派生 | m=10240KiB, t=3, p=1, out=32, ver=19 |
| `aes-gcm.json` | AES-256-GCM | nonce 12 字节, tag 16 字节 |
| sa-oaep.json` | RSA-2048 OAEP | MGF1 与 OAEP 哈希**均为 SHA-256** |
| `container-roundtrip.json` | 完整容器字节流 | SMK1 / SSC1 往返一致 |
| `char-count.json` | 字符计数 | Unicode **码点**，非 UTF-16 单元 |
| 
egative-cases.json` | 负例 | 每条指定期望错误类别 |

## 使用方式

```bash
# 重新生成（仅在密码学参数变更时执行）
python scripts/gen-vectors.py

# 校验向量自洽性（CI 的 vectors job 使用）
python scripts/verify-vectors.py

# 只校验指定文件
python scripts/verify-vectors.py test-vectors/kdf-argon2id.json
```

各平台实现**不使用**这两个 Python 脚本，而是加载本目录的 JSON 并向其对齐。

## 几个容易踩的坑

### 1. RSA-OAEP 密文不可复现

OAEP 加密含随机种子，因此**同一数据密钥每次封装结果都不同**。向量中的 `wrappedHex` 
只用于验证解封装能还原数据密钥，不能要求重新加密得到相同密文。

各端测试时应验证**往返一致性**，而非密文逐字节相等。

### 2. MGF1 哈希必须显式指定

各密码库对 OAEP 的 MGF1 哈希默认值不一致（常见默认 SHA-1）。必须显式指定为 SHA-256，
并与 OAEP 哈希保持一致。`verify-vectors.py` 中有一条检查专门验证 SHA-1 变体会失败。

### 3. 公私钥 nonce 不得相同

`aes-gcm.json` 中 `aesgcm-pubkey-nonce` 与 `aesgcm-privkey-nonce` 使用同一 KEK 但不同 nonce。
**复用 nonce 会使两密文异或即可恢复两明文异或，加密完全失效。**

### 4. 字符计数用码点

`char-count.json` 中 emoji 与 ZWJ 序列在码点口径和 UTF-16 单元口径下结果不同。
限额 150 必须按**码点**计算，否则五个平台会得到不同的判定。

### 5. 负例必须校验错误类别

负例不能只断言“失败”，必须返回**指定的错误类别**。
egative-cases.json` 中每条都带 
`expectedError` 字段，对应错误语义见该文件的 `errorCategories`。

## 修改向量时的纪律

修改任何密码学参数后，必须：

1. 重新生成向量
2. 同步更新 `docs/02-crypto/密码学与密钥派生设计.md` 的参数汇总表
3. 同步更新 `docs/03-data/数据格式与存储设计.md` 的字段说明
4. 在提交信息中说明参数变更及其影响

参数变更会导致**所有已生成的数据文件无法解密**，属于破坏性变更。
