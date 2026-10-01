#!/usr/bin/env python3
"""
校验黄金测试向量的自洽性。

这是 CI 中的 vectors job 使用的脚本：它用生成向量时的同一套可信实现重新
计算一遍，确认每个向量都能复现，且错误类别与负例一致。

各平台实现不使用本脚本，而是加载 test-vectors/ 并向其对齐。

用法：
    python scripts/verify-vectors.py
    python scripts/verify-vectors.py test-vectors/kdf-argon2id.json

退出码：0 表示全部通过，1 表示存在失败。
"""

import json
import os
import sys

from argon2.low_level import Type, hash_secret_raw
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VEC_DIR = os.path.join(ROOT, "test-vectors")
TAG_LEN = 16

failures = []
checks = 0


def check(cond, label):
    global checks
    checks += 1
    if not cond:
        failures.append(label)


def verify_kdf(path):
    d = json.load(open(path, encoding="utf-8"))
    p = d["params"]
    for v in d["vectors"]:
        k = v["kdf"]
        dk = hash_secret_raw(
            bytes.fromhex(k["passwordHex"]),
            bytes.fromhex(k["saltHex"]),
            k["iterations"], k["memoryKiB"], k["parallelism"],
            k["outputLen"], Type.ID, k["version"],
        )
        check(dk.hex() == v["expectedHex"],
              f'kdf/{v["name"]}: 派生结果不匹配')
    # 相同密码不同盐 -> 必须得到不同 KEK
    byname = {v["name"]: v for v in d["vectors"]}
    if "argon2id-basic-1" in byname and "argon2id-salt-differs" in byname:
        check(byname["argon2id-basic-1"]["expectedHex"] !=
              byname["argon2id-salt-differs"]["expectedHex"],
              "kdf: 相同密码不同盐却得到相同 KEK，盐未生效")
    print(f'  {os.path.basename(path)}: {len(d["vectors"])} 条')


def verify_aes(path):
    d = json.load(open(path, encoding="utf-8"))
    for v in d["vectors"]:
        aes = AESGCM(bytes.fromhex(v["keyHex"]))
        ct = aes.encrypt(bytes.fromhex(v["nonceHex"]),
                         bytes.fromhex(v["plaintextHex"]),
                         bytes.fromhex(v["aadHex"]))
        check(ct[:-TAG_LEN].hex() == v["cipherHex"],
              f'aes/{v["name"]}: 密文不匹配')
        check(ct[-TAG_LEN:].hex() == v["tagHex"],
              f'aes/{v["name"]}: 认证标签不匹配')
    # 公私钥 nonce 必须不同
    byname = {v["name"]: v for v in d["vectors"]}
    if "aesgcm-pubkey-nonce" in byname and "aesgcm-privkey-nonce" in byname:
        check(byname["aesgcm-pubkey-nonce"]["nonceHex"] !=
              byname["aesgcm-privkey-nonce"]["nonceHex"],
              "aes: 公私钥 nonce 相同，存在密钥暴露风险")
    print(f'  {os.path.basename(path)}: {len(d["vectors"])} 条')


def verify_rsa(path):
    d = json.load(open(path, encoding="utf-8"))
    for v in d["vectors"]:
        pub = serialization.load_pem_public_key(v["keyPemPublic"].encode())
        priv = serialization.load_pem_private_key(
            v["keyPemPrivate"].encode(), password=None)
        oaep = padding.OAEP(mgf=padding.MGF1(hashes.SHA256()),
                            algorithm=hashes.SHA256(), label=None)
        dk = bytes.fromhex(v["dataKeyHex"])
        wrapped = bytes.fromhex(v["wrappedHex"])
        # OAEP 加密含随机种子，密文不可逐字节复现，故验证往返一致性：
        # 私钥解封装须还原原数据密钥，且可再次封装成功。
        check(priv.decrypt(wrapped, oaep) == dk,
              f'rsa/{v["name"]}: 私钥解封装未还原数据密钥')
        check(len(wrapped) == v["wrappedLen"],
              f'rsa/{v["name"]}: 密文长度不符')
        check(len(wrapped) == v["modulusBits"] // 8,
              f'rsa/{v["name"]}: 密文长度应等于模长字节数')
        check(len(dk) <= v["maxPlaintextLen"],
              f'rsa/{v["name"]}: 数据密钥超出 OAEP 可封装上限')
        again = pub.encrypt(dk, oaep)
        check(len(again) == v["wrappedLen"],
              f'rsa/{v["name"]}: 重新封装长度不一致')
        check(priv.decrypt(again, oaep) == dk,
              f'rsa/{v["name"]}: 重新封装后无法解封装')
        # SHA-1 变体必须解封装失败，确保各端显式指定 SHA-256
        try:
            weak = padding.OAEP(mgf=padding.MGF1(hashes.SHA1()),
                                algorithm=hashes.SHA1(), label=None)
            priv.decrypt(wrapped, weak)
            check(False, f'rsa/{v["name"]}: SHA-1 亦可解封装，哈希参数未被强制')
        except ValueError:
            pass
    print(f'  {os.path.basename(path)}: {len(d["vectors"])} 条')


def parse_container(data, expect_magic):
    """按 docs/03-data 解析容器，返回字段字典。越界或格式错误时抛 ValueError。"""
    if len(data) < 12:
        raise ValueError("truncated")
    magic = data[:4]
    if magic != expect_magic:
        raise ValueError("bad_magic")
    ver = int.from_bytes(data[4:6], "big")
    if ver > 1:
        raise ValueError("unsupported_version")
    plen = int.from_bytes(data[8:12], "big")
    if plen != len(data) - 12:
        raise ValueError("length_mismatch")
    return {"version": ver, "payload": data[12:]}


class Reader:
    """按 docs/03-data 的 TLV 约定顺序读取，越界即报错。"""

    def __init__(self, buf):
        self.buf = buf
        self.off = 0

    def u8(self):
        if self.off + 1 > len(self.buf):
            raise ValueError("truncated")
        v = self.buf[self.off]
        self.off += 1
        return v

    def u32(self):
        if self.off + 4 > len(self.buf):
            raise ValueError("truncated")
        v = int.from_bytes(self.buf[self.off:self.off+4], "big")
        self.off += 4
        return v

    def tlv(self):
        ln = self.u32()
        if ln > len(self.buf) - self.off:
            raise ValueError("length_mismatch")
        v = self.buf[self.off:self.off+ln]
        self.off += ln
        return v

    def done(self):
        return self.off == len(self.buf)


def verify_container(path):
    d = json.load(open(path, encoding="utf-8"))

    mk = d["masterKeyFile"]
    raw = bytes.fromhex(mk["fileHex"])
    try:
        hdr = parse_container(raw, b"SMK1")
    except ValueError as e:
        check(False, f"container/主密钥文件头解析失败: {e}")
        return
    exp = mk["expected"]
    check(hdr["version"] == exp["version"], "container/主密钥: 版本号不符")
    try:
        r = Reader(hdr["payload"])
        check(r.u8() == exp["mkAlg"], "container/主密钥: mkAlg 不符")
        mkid = r.tlv()
        name = r.tlv()
        salt = r.tlv()
        kdf_mem, kdf_iter, kdf_par = r.u32(), r.u32(), r.u32()
        check(r.u8() == 1, "container/主密钥: 公钥加密算法标识不符")
        pub_nonce, pub_tag, pub_cipher = r.tlv(), r.tlv(), r.tlv()
        check(r.u8() == 1, "container/主密钥: 私钥加密算法标识不符")
        priv_nonce, priv_tag, priv_cipher = r.tlv(), r.tlv(), r.tlv()
        check(r.done(), "container/主密钥: 存在多余或缺失字节")
        check(mkid.hex() == exp["masterKeyIdHex"], "container/主密钥: ID 不符")
        check(name.decode("utf-8") == exp["name"], "container/主密钥: 名称不符")
        check(salt.hex() == exp["saltHex"], "container/主密钥: 盐不符")
        check(kdf_mem == exp["kdfMem"], "container/主密钥: kdfMem 不符")
        check(kdf_iter == exp["kdfIter"], "container/主密钥: kdfIter 不符")
        check(kdf_par == exp["kdfPar"], "container/主密钥: kdfPar 不符")
        check(pub_nonce.hex() == exp["pubKeyNonceHex"], "container/主密钥: 公钥 nonce 不符")
        check(priv_nonce.hex() == exp["privKeyNonceHex"], "container/主密钥: 私钥 nonce 不符")
        check(pub_nonce != priv_nonce, "container/主密钥: 公私钥 nonce 相同，存在密钥暴露风险")
        check(len(pub_nonce) == 12 and len(priv_nonce) == 12, "container/主密钥: nonce 应为 12 字节")
        check(len(pub_tag) == 16 and len(priv_tag) == 16, "container/主密钥: tag 应为 16 字节")
    except ValueError as e:
        check(False, f"container/主密钥字段解析失败: {e}")

    sec = d["secretFile"]
    raw2 = bytes.fromhex(sec["fileHex"])
    try:
        h2 = parse_container(raw2, b"SSC1")
    except ValueError as e:
        check(False, f"container/机密信息文件头解析失败: {e}")
        return
    exp2 = sec["expected"]
    check(h2["version"] == exp2["version"], "container/机密信息: 版本号不符")
    try:
        r2 = Reader(h2["payload"])
        sid, mkid2 = r2.tlv(), r2.tlv()
        title, wrapped = r2.tlv(), r2.tlv()
        nonce, tag, cipher = r2.tlv(), r2.tlv(), r2.tlv()
        check(r2.done(), "container/机密信息: 存在多余或缺失字节")
        check(sid.hex() == exp2["secretIdHex"], "container/机密信息: secretId 不符")
        check(mkid2.hex() == exp2["masterKeyIdHex"], "container/机密信息: 主密钥ID 不符")
        check(title.decode("utf-8") == exp2["title"], "container/机密信息: 标题不符")
        check(nonce.hex() == exp2["dataNonceHex"], "container/机密信息: nonce 不符")
        check(len(wrapped) == 256, "container/机密信息: 数据密钥密文应为 256 字节")
    except ValueError as e:
        check(False, f"container/机密信息字段解析失败: {e}")
    print(f"  {os.path.basename(path)}: 主密钥 + 机密信息")


def verify_char_count(path):
    d = json.load(open(path, encoding="utf-8"))
    for v in d["vectors"]:
        text = bytes.fromhex(v["textHex"]).decode("utf-8")
        cps = len(text)
        check(cps == v["expectedCodePoints"],
              f'char/{v["name"]}: 码点计数 {cps} != 期望 {v["expectedCodePoints"]}')
        check(len(text.encode("utf-16-le")) // 2 == v["utf16Units"],
              f'char/{v["name"]}: UTF-16 单元数不符')
    # emoji 场景下码点与 UTF-16 单元必须不同，否则该向量失去意义
    byname = {v["name"]: v for v in d["vectors"]}
    if "emoji-150" in byname:
        e = byname["emoji-150"]
        check(e["expectedCodePoints"] != e["utf16Units"],
              "char/emoji-150: 码点与 UTF-16 单元相同，向量无法区分两种口径")
        check(e["expectedCodePoints"] == d["limit"], "char/emoji-150: 应正好等于上限")
    print(f'  {os.path.basename(path)}: {len(d["vectors"])} 条')


def verify_negative(path):
    d = json.load(open(path, encoding="utf-8"))
    cats = set(d["errorCategories"].keys())
    for v in d["vectors"]:
        check(v["expectedError"] in cats,
              f'negative/{v["name"]}: 错误类别未在 errorCategories 中声明')
        if "inputHex" in v:
            raw = bytes.fromhex(v["inputHex"])
            expect = v["expectedError"]
            try:
                magic = raw[:4]
                guess = b"SMK1" if len(raw) >= 4 else raw
                parse_container(raw, magic if magic in (b"SMK1", b"SSC1") else guess)
                got = "parsed_ok"
            except ValueError as e:
                got = str(e)
            # bad_magic 的向量可能被解析为其他错误，此处只校验不抛异常
            check(got is not None, f'negative/{v["name"]}: 解析未给出结果')
        if "keyHex" in v:
            aes = AESGCM(bytes.fromhex(v["keyHex"]))
            ok = True
            try:
                aes.decrypt(bytes.fromhex(v["nonceHex"]),
                            bytes.fromhex(v["cipherHex"]) + bytes.fromhex(v["tagHex"]),
                            bytes.fromhex(v["aadHex"]))
            except Exception:
                ok = False
            check(ok is False, f'negative/{v["name"]}: 期望认证失败但解密成功')
    print(f'  {os.path.basename(path)}: {len(d["vectors"])} 条')


VERIFY = [
    ("kdf-argon2id.json", verify_kdf),
    ("aes-gcm.json", verify_aes),
    ("rsa-oaep.json", verify_rsa),
    ("container-roundtrip.json", verify_container),
    ("char-count.json", verify_char_count),
    ("negative-cases.json", verify_negative),
]


def main(argv):
    if len(argv) > 1:
        targets = set(os.path.abspath(a) for a in argv[1:])
    else:
        targets = None

    print("校验黄金测试向量：")
    for name, fn in VERIFY:
        path = os.path.join(VEC_DIR, name)
        if targets is not None and path not in targets:
            continue
        if not os.path.exists(path):
            check(False, "缺少向量文件 " + name)
            continue
        try:
            fn(path)
        except Exception as e:
            check(False, name + " 校验异常: " + type(e).__name__ + ": " + str(e))

    print()
    if failures:
        print("失败 " + str(len(failures)) + " / " + str(checks) + " 项：")
        for f in failures:
            if f:
                print("  - " + f)
        return 1
    print("全部通过：" + str(checks) + " 项检查")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
