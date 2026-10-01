#!/usr/bin/env python3
"""
生成跨平台黄金测试向量。

向量由可信第三方实现（argon2-cffi / cryptography）生成，五端实现向其对齐，
而非用项目自身实现自证——这样五端同时实现错误时也能被发现。

用法：
    python scripts/gen-vectors.py

注意：修改任何密码学参数后必须重新生成向量，并同步更新 docs/02-crypto 与
docs/03-data 中的参数表。参数变更会影响所有已生成的数据文件。
"""

import json
import os
import sys

# Windows runner 上 Python 的 stdout 默认是 cp1252，打印中文会抛
# UnicodeEncodeError 让整个 job 变红。必须在任何输出之前完成重配。
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

from argon2.low_level import Type, hash_secret_raw
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

# ---- 与 docs/02-crypto 保持一致的固定参数 ----
KDF_MEM = 10240   # KiB，约 10 MiB
KDF_ITER = 3
KDF_PAR = 1
KDF_VER = 19      # 0x13，Argon2 v1.3
KDF_OUTLEN = 32
SALT_LEN = 16
DATA_KEY_LEN = 32
NONCE_LEN = 12
TAG_LEN = 16

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "test-vectors")


def hx(b):
    """bytes -> 小写十六进制字符串"""
    return b.hex()


def unhx(s):
    """十六进制字符串 -> bytes"""
    return bytes.fromhex(s)


def write(name, obj):
    path = os.path.join(OUT_DIR, name)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(obj, f, ensure_ascii=False, indent=2, sort_keys=False)
        f.write("\n")
    print(f"  写入 {name}")


# ---------------------------------------------------------------- Argon2id
def gen_kdf():
    """Argon2id 派生向量。密码与盐均以十六进制字节表示，避免编码歧义。"""
    cases = [
        # (用例名, 密码, 盐)
        ("argon2id-basic-1", b"test-password-1", bytes(range(16))),
        ("argon2id-basic-2", b"", bytes(range(16))),
        ("argon2id-basic-3", b"a", bytes([0xFF] * 16)),
        # 含非 ASCII 字符的密码（UTF-8 编码）
        ("argon2id-utf8-password", "密码密码".encode("utf-8"), bytes(range(16))),
        # 多字节 / 长密码
        ("argon2id-long-password", b"P" * 128, bytes(range(16))),
        # 不同盐 -> 不同 KEK，验证盐确实生效
        ("argon2id-salt-differs", b"test-password-1", bytes([0x10] * 16)),
    ]
    vectors = []
    for name, pwd, salt in cases:
        dk = hash_secret_raw(
            pwd, salt, KDF_ITER, KDF_MEM, KDF_PAR, KDF_OUTLEN, Type.ID, KDF_VER
        )
        vectors.append({
            "name": name,
            "kdf": {
                "alg": "argon2id",
                "version": KDF_VER,
                "passwordHex": hx(pwd),
                "saltHex": hx(salt),
                "memoryKiB": KDF_MEM,
                "iterations": KDF_ITER,
                "parallelism": KDF_PAR,
                "outputLen": KDF_OUTLEN,
            },
            "expectedHex": hx(dk),
        })
    write("kdf-argon2id.json", {
        "description": "Argon2id 密钥派生黄金向量。KEK = Argon2id(password, salt)",
        "params": {
            "memoryKiB": KDF_MEM, "iterations": KDF_ITER,
            "parallelism": KDF_PAR, "outputLen": KDF_OUTLEN, "version": KDF_VER,
        },
        "vectors": vectors,
    })


# --------------------------------------------------------------- AES-GCM
def gen_aes_gcm():
    """AES-256-GCM 向量，含公钥/私钥 nonce 不复用的正例。"""
    key = bytes(range(32))
    cases = [
        # (用例名, key, nonce, aad, plaintext)
        ("aesgcm-basic", key, bytes(range(12)), b"master-key-id-16byte", "机密信息内容".encode("utf-8")),
        ("aesgcm-empty-plaintext", key, bytes(range(12)), b"secret-id-16bytes", b""),
        ("aesgcm-aad-differs", key, bytes(range(12)), b"other-aad-value!", "机密信息内容".encode("utf-8")),
        # 同一 KEK、不同 nonce：公钥与私钥加密必须走这个模式
        ("aesgcm-pubkey-nonce", key, bytes([0xA0] * 12), b"master-key-id-16byte", b"RSA-PUBLIC-KEY-DER-BYTES"),
        ("aesgcm-privkey-nonce", key, bytes([0xB0] * 12), b"master-key-id-16byte", b"RSA-PRIVATE-KEY-DER-BYTES"),
    ]
    vectors = []
    for name, k, nonce, aad, pt in cases:
        aes = AESGCM(k)
        ct = aes.encrypt(nonce, pt, aad)
        # cryptography 返回 ciphertext||tag，此处显式拆分
        cipher, tag = ct[:-TAG_LEN], ct[-TAG_LEN:]
        vectors.append({
            "name": name,
            "alg": "aes-256-gcm",
            "keyHex": hx(k),
            "nonceHex": hx(nonce),
            "aadHex": hx(aad),
            "plaintextHex": hx(pt),
            "cipherHex": hx(cipher),
            "tagHex": hx(tag),
        })
    write("aes-gcm.json", {
        "description": "AES-256-GCM 黄金向量。nonce 12 字节，tag 16 字节",
        "note": "pubKeyNonce 与 privKeyNonce 必须不同：同 KEK 复用 nonce 会使两密文",
        "note2": "异或即可恢复两明文异或，加密完全失效。",
        "vectors": vectors,
    })


# -------------------------------------------------------------- RSA-OAEP
FIXED_RSA_PRIVATE_PEM = """-----BEGIN PRIVATE KEY-----
MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQDLcpVnAy/Y+ui2
ucOleiKIanFx7X/xjAcjQCPx+ICF6+qipNzSQV5ksAyryc+Un43hL5HytWHCW3Sx
XDfMj5xBEeibaKbSgZ2JC3VY7karasXGsUlnNAKUpq4PKPfymPNR+g9vIJ2E2BQG
7vea6jSzuER7JEQ1Kmc6/WVwItgSKY9h0/VIn4l+Dbvxot9qZuZRNNDeMh1A6aJu
1Lz4WJ2Lzko0Un+FLdGYl3ShymiDRrQSYcwdqYGzLGCF67eU37R2GR5BSlfsRIje
j8K+TsorLuXz2sdwun9DRyRSZfkBLbQ+nn77UMOrMOBhfNK+Sj8lfSWc7Uatwd9S
6tjWM0RtAgMBAAECggEAHOd/oG4MezhSnbtQt47dnyH4UiZa+hdZ4EE5miQjJmO5
JzhjAyA0Z/u3IST/7+6nOqoGg0QPyowxzQz8BH0WxwvjT0kUAm5V0mWlW2gm9C2s
Qaloc1gpUbNIoXDBoV7nmur7DF/G4VvNpJqI5SKHME1qtVAWvLL0jaX4pnDc3990
e8+NrixZL2Dz3QMtXohcHLilLOnpEzbI4Yq/TSUUO2kU5SoleI4F4X/+pxehFTui
aqhOORsp1POPMbiLy1bWCRhDd6ws1BcdPEs7x64M8/HQWeL1dfABdukurjbHg/qX
BSw6Pnm02BQqtG2eK6YCvslNforktKdi62t7ft0koQKBgQD+XSS7U6PY38nM8yjE
+dtLVkgTTCeo8/xM2B/s4qEUiabnwT/UF32jZ2rkD7mtdcI1+3N/T6nLCdie26m4
nKqUiUK4fm3P/9qp69QDFUcjrujB/iCfnwUQzLfDnFKuKdbBno6N5o1GVyMVFwMB
sA2NkEqikdHMBfqVKMbxrKrHFQKBgQDMwZjjMTj3Ce3zuVri/dGdpRgTEASEDSTe
zde8J4XXrw6oosZF2FND1+A1bKNFU2N0Ngjc5C0bwUqf1txc1qWmGXrohq+leP5p
+ddu93nYMgSkCW6edc9K6ksl49A2qr2qrdSD06F/bH7+jTIfUyIKcz2OkVoFn9R/
rIjyc/Fd+QKBgGLH81rYSXoQtoLkL4IHcjVYpZbw7Tn8vo9tI0DQZjPenTXFY69P
DBioMeetf7MwVyK1qw3W9x6FjV1r+wZZuRkEESgq2SR8vQNeNTCZU840v+mOckbq
74GtrvPFQoqmbuM0WzIaReT6NAKdtCo0n57JWWK29F2iOMUyV8hFAPFdAoGBAMJk
ms43kSmY17yooU0YRMYyU2TltCPahsKxEryjmUJoBLa9sxfkhUjHzylpdqj+vlE6
a7erOdg77qlJx/SsJKBJWJMiK+ghswjSFmJXueozsow6GDeBCcjKH4ZdfvQnreI5
HqX5aj4bt4Xcb1WmC494UVzlSl5Em+6K04m74c+hAoGBAIsxA0sXd6ub/d3FeHdG
4ohwhVm6wC1j2u2GuifYv/Yx9F6VqrHKsjNjRrBkY6FrwC0mIv+tqySFW9VdWIBx
rV2ifvzfZZVagXQUfh80/tnMxKqQTnRKnJTxzdPzfwkTpPJW64/uBypn5/MuQjS1
yuMz/JWUnJiJFwwDrmYgO34S
-----END PRIVATE KEY-----
"""


def gen_rsa_oaep():
    """RSA-2048 OAEP-SHA256 封装向量。使用固定密钥对，保证跨端可比对。"""
    private_key = serialization.load_pem_private_key(
        FIXED_RSA_PRIVATE_PEM.encode("utf-8"), password=None
    )
    public_key = private_key.public_key()

    # MGF1 与 OAEP 哈希均显式指定为 SHA-256；不可依赖各库默认值
    oaep = padding.OAEP(
        mgf=padding.MGF1(algorithm=hashes.SHA256()),
        algorithm=hashes.SHA256(),
        label=None,
    )
    data_key = bytes(range(32))
    wrapped = public_key.encrypt(data_key, oaep)

    vectors = [{
        "name": "rsa2048-oaep-sha256",
        "alg": "rsa-oaep",
        "mgfHash": "sha256",
        "oaepHash": "sha256",
        "label": None,
        "keyPemPrivate": FIXED_RSA_PRIVATE_PEM,
        "keyPemPublic": public_key.public_bytes(
            serialization.Encoding.PEM,
            serialization.PublicFormat.SubjectPublicKeyInfo,
        ).decode("utf-8"),
        "dataKeyHex": hx(data_key),
        "wrappedHex": hx(wrapped),
        "modulusBits": private_key.key_size,
        "wrappedLen": len(wrapped),
        "maxPlaintextLen": (private_key.key_size // 8) - 2 * 32 - 2,
    }]
    write("rsa-oaep.json", {
        "description": "RSA-2048 OAEP-SHA256 黄金向量（公钥封装数据密钥）",
        "note": "MGF1 与 OAEP 哈希均固定 SHA-256；部分库默认不一致，必须显式指定。",
        "vectors": vectors,
    })


# ------------------------------------------------------- 容器格式往返
def tlv(data: bytes) -> bytes:
    """按 docs/03-data 的 TLV 约定编码：uint32 大端长度 + 载荷。"""
    return len(data).to_bytes(4, "big") + data


def build_secret_file(secret_id, master_key_id, title, wrapped_dk, nonce, tag, cipher):
    """构造机密信息文件（Magic = SSC1）。字段顺序须与 docs/03-data §5 一致。"""
    payload = b"".join([
        tlv(secret_id),
        tlv(master_key_id),
        tlv(title.encode("utf-8")),
        tlv(wrapped_dk),
        tlv(nonce),
        tlv(tag),
        tlv(cipher),
    ])
    return b"SSC1" + (1).to_bytes(2, "big") + (0).to_bytes(2, "big") + len(payload).to_bytes(4, "big") + payload


def build_master_key_file(mk_alg, mk_id, name, salt, kdf_mem, kdf_iter, kdf_par,
                          pub_nonce, pub_tag, pub_cipher,
                          priv_nonce, priv_tag, priv_cipher):
    """构造主密钥文件（Magic = SMK1）。公私钥各自独立密文，nonce 必须不同。"""
    payload = b"".join([
        bytes([mk_alg]),
        tlv(mk_id),
        tlv(name.encode("utf-8")),
        tlv(salt),
        kdf_mem.to_bytes(4, "big"),
        kdf_iter.to_bytes(4, "big"),
        kdf_par.to_bytes(4, "big"),
        bytes([1]),
        tlv(pub_nonce), tlv(pub_tag), tlv(pub_cipher),
        bytes([1]),
        tlv(priv_nonce), tlv(priv_tag), tlv(priv_cipher),
    ])
    return b"SMK1" + (1).to_bytes(2, "big") + (0).to_bytes(2, "big") + len(payload).to_bytes(4, "big") + payload


def gen_container():
    """完整容器字节流往返向量：给出期望字节，各端解析后须逐字节还原。"""
    private_key = serialization.load_pem_private_key(
        FIXED_RSA_PRIVATE_PEM.encode("utf-8"), password=None
    )
    public_key = private_key.public_key()
    pub_der = public_key.public_bytes(
        serialization.Encoding.DER, serialization.PublicFormat.PKCS1
    )
    priv_der = private_key.private_bytes(
        serialization.Encoding.DER,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption(),
    )

    mk_id = bytes(range(16))
    kek = hash_secret_raw(b"container-password", bytes(range(16)),
                          KDF_ITER, KDF_MEM, KDF_PAR, KDF_OUTLEN, Type.ID, KDF_VER)
    pub_nonce = bytes([0xA0] * 12)
    priv_nonce = bytes([0xB0] * 12)
    assert pub_nonce != priv_nonce, "公私钥 nonce 不得相同"
    aes = AESGCM(kek)
    pub_ct = aes.encrypt(pub_nonce, pub_der, mk_id)
    priv_ct = aes.encrypt(priv_nonce, priv_der, mk_id)

    mk_file = build_master_key_file(
        1, mk_id, "我的主密钥", bytes(range(16)), KDF_MEM, KDF_ITER, KDF_PAR,
        pub_nonce, pub_ct[-TAG_LEN:], pub_ct[:-TAG_LEN],
        priv_nonce, priv_ct[-TAG_LEN:], priv_ct[:-TAG_LEN],
    )

    # 机密信息：使用真实 RSA-OAEP 封装的数据密钥
    oaep = padding.OAEP(mgf=padding.MGF1(hashes.SHA256()),
                        algorithm=hashes.SHA256(), label=None)
    data_key = bytes(range(32))
    wrapped = public_key.encrypt(data_key, oaep)
    secret_id = bytes([0x0F] * 16)
    aes2 = AESGCM(data_key)
    sec_nonce = bytes(range(12))
    sec_ct = aes2.encrypt(sec_nonce, "机密信息内容".encode("utf-8"), secret_id)
    sec_file = build_secret_file(
        secret_id, mk_id, "标题", wrapped,
        sec_nonce, sec_ct[-TAG_LEN:], sec_ct[:-TAG_LEN],
    )

    write("container-roundtrip.json", {
        "description": "完整容器字节流。各端解析后须逐字段还原，且可重新序列化得到相同字节。",
        "masterKeyFile": {
            "fileHex": hx(mk_file),
            "expected": {
                "magic": "SMK1", "version": 1, "flags": 0, "mkAlg": 1,
                "masterKeyIdHex": hx(mk_id), "name": "我的主密钥",
                "saltHex": hx(bytes(range(16))),
                "kdfMem": KDF_MEM, "kdfIter": KDF_ITER, "kdfPar": KDF_PAR,
                "pubKeyNonceHex": hx(pub_nonce),
                "privKeyNonceHex": hx(priv_nonce),
            },
        },
        "secretFile": {
            "fileHex": hx(sec_file),
            "expected": {
                "magic": "SSC1", "version": 1, "flags": 0,
                "secretIdHex": hx(secret_id),
                "masterKeyIdHex": hx(mk_id), "title": "标题",
                "dataNonceHex": hx(sec_nonce),
            },
        },
    })


# ------------------------------------------------------------ 字符计数
def gen_char_count():
    """机密信息长度按 Unicode 码点计数（非 UTF-16 单元、非字节数）。

    五端必须对同一字符串得出相同计数，尤其含 emoji、生僻字、组合字符时。
    """
    cases = [
        ("ascii-150", "a" * 150, 150),
        ("ascii-151", "a" * 151, 151),
        ("cjk-150", "密" * 150, 150),
        ("emoji-150", "\U0001F512" * 150, 150),
        ("emoji-mixed", "a\U0001F512b\U0001F510", 4),
        ("combining", "e\u0301\u0301", 3),
        ("zwj-family", "\U0001F468\u200D\U0001F469\u200D\U0001F467", 5),
        ("empty", "", 0),
    ]
    vectors = []
    for name, text, expect in cases:
        vectors.append({
            "name": name,
            "textHex": hx(text.encode("utf-8")),
            "expectedCodePoints": expect,
            "utf16Units": len(text.encode("utf-16-le")) // 2,
            "utf8Bytes": len(text.encode("utf-8")),
        })
    write("char-count.json", {
        "description": "机密信息字符计数向量。计数规则：Unicode 码点。",
        "note": "utf16Units 与 utf8Bytes 仅供对照，不得用作计数依据。",
        "limit": 150,
        "vectors": vectors,
    })


# ------------------------------------------------------------ 负例向量
def gen_negative():
    """负例向量：为每种畸形输入指定期望错误类别。"""
    g = build_secret_file(
        bytes([0x0F] * 16), bytes(range(16)), "标题",
        bytes(range(256)), bytes(range(12)), bytes(16), b"ciphertext-bytes",
    )
    hdr = lambda ver, plen, magic=b"SSC1": magic + ver.to_bytes(2, "big") + (0).to_bytes(2, "big") + plen.to_bytes(4, "big")
    vectors = [
        {"name": "bad-magic-wrong", "inputHex": hx(g[:4] + b"XXXX" + g[8:]),
         "expectedError": "bad_magic", "desc": "Magic 被篡改"},
        {"name": "bad-magic-type-confusion", "inputHex": hx(b"SSC1" + g[4:]),
         "expectedError": "bad_magic", "desc": "机密信息文件被当作主密钥文件"},
        {"name": "unsupported-version", "inputHex": hx(hdr(99, len(g) - 12) + g[12:]),
         "expectedError": "unsupported_version", "desc": "版本号高于支持范围"},
        {"name": "truncated-file", "inputHex": hx(g[:20]),
         "expectedError": "truncated", "desc": "文件被截断"},
        {"name": "empty-file", "inputHex": "",
         "expectedError": "truncated", "desc": "空文件"},
        {"name": "one-byte-file", "inputHex": hx(b"S"),
         "expectedError": "truncated", "desc": "仅 1 字节"},
        {"name": "length-mismatch-large", "inputHex": hx(hdr(1, 9999) + g[12:]),
         "expectedError": "length_mismatch", "desc": "PayloadLen 与实际长度不符"},
        {"name": "tlv-length-overflow", "inputHex": hx(hdr(1, 12) + (0xFFFFFFFF).to_bytes(4, "big")),
         "expectedError": "length_mismatch", "desc": "TLV 长度 0xFFFFFFFF，禁止越界分配"},
        {"name": "trailing-garbage", "inputHex": hx(g + b"\x00\x01\x02"),
         "expectedError": "length_mismatch", "desc": "文件尾部有多余字节"},
        # SM2 是主密钥文件（SMK1）的 mkAlg 字段值，机密信息文件没有算法字段。
        # 这里必须用 SMK1，否则测的就不是「未知算法标识」而是长度问题。
        # 长度只覆盖到 mkAlg 一个字节，够解析器读出算法标识并拒绝即可。
        {"name": "unknown-algorithm-sm2",
         "inputHex": hx(hdr(1, 1, magic=b"SMK1") + b"\x02"),
         "expectedError": "unknown_algorithm",
         "desc": "主密钥文件使用 SM2 算法标识（mkAlg=2），v0.0.1 必须拒绝"},
    ]

    # name 含非法 UTF-8。先构造一份完全合法的主密钥文件（name = "X"），
    # 再把 name 的那一个字节换成 0xC3。0xC3 是双字节序列的首字节，
    # 单独出现属于截断的 UTF-8 序列，必然校验失败。
    # 长度前缀保持不变，因此测的确实是 UTF-8 校验而非长度校验。
    _mk_valid = build_master_key_file(
        1, bytes(range(16)), "X", bytes(range(16)),
        KDF_MEM, KDF_ITER, KDF_PAR,
        bytes([0xA0] * 12), bytes(16), b"x",
        bytes([0xB0] * 12), bytes(16), b"y",
    )
    _bad_name_offset = _mk_valid.index(b"X")
    _mk_bad = bytearray(_mk_valid)
    _mk_bad[_bad_name_offset] = 0xC3
    vectors.append({
        "name": "invalid-utf8-name",
        "inputHex": hx(bytes(_mk_bad)),
        "expectedError": "invalid_utf8",
        "desc": "name 含非法 UTF-8（0xC3 是双字节序列首字节，单独出现即非法）",
    })


    # 密文篡改与错误密码：均走 GCM 认证失败
    kek = hash_secret_raw(b"container-password", bytes(range(16)),
                          KDF_ITER, KDF_MEM, KDF_PAR, KDF_OUTLEN, Type.ID, KDF_VER)
    nonce = bytes([0xA0] * 12)
    aad = bytes(range(16))
    good = AESGCM(kek).encrypt(nonce, b"ORIGINAL-PLAINTEXT", aad)
    bad = bytearray(good)
    bad[0] ^= 0x01
    wrong = hash_secret_raw(b"WRONG-password", bytes(range(16)),
                            KDF_ITER, KDF_MEM, KDF_PAR, KDF_OUTLEN, Type.ID, KDF_VER)
    vectors.append({
        "name": "tampered-ciphertext", "keyHex": hx(kek), "nonceHex": hx(nonce),
        "aadHex": hx(aad), "cipherHex": hx(bytes(bad[:-TAG_LEN])),
        "tagHex": hx(bytes(bad[-TAG_LEN:])), "expectedError": "wrong_password",
        "desc": "密文被篡改 1 字节，认证失败",
    })
    vectors.append({
        "name": "wrong-password", "keyHex": hx(wrong), "nonceHex": hx(nonce),
        "aadHex": hx(aad), "cipherHex": hx(good[:-TAG_LEN]),
        "tagHex": hx(good[-TAG_LEN:]), "expectedError": "wrong_password",
        "desc": "使用错误 KEK 解密，认证失败",
    })

    write("negative-cases.json", {
        "description": "负例向量：畸形输入必须返回指定错误类别，且不得崩溃或越界。",
        "errorCategories": {
            "wrong_password": "GCM 认证失败（含密文篡改），对外统一提示主密钥密码错误",
            "bad_magic": "文件类型不匹配",
            "unsupported_version": "版本号高于支持范围",
            "truncated": "数据截断",
            "length_mismatch": "长度字段与实际不符",
            "unknown_algorithm": "未知算法标识（如 v0.0.1 遇到 SM2）",
            "invalid_utf8": "name / title 不是合法 UTF-8",
        },
        "vectors": vectors,
    })


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    print("生成黄金测试向量：")
    gen_kdf()
    gen_aes_gcm()
    gen_rsa_oaep()
    gen_container()
    gen_char_count()
    gen_negative()
    print("完成。")


if __name__ == "__main__":
    main()
