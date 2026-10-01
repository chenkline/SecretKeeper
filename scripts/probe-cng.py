# 机密心 - CNG 能力探测

# 用途：确认目标环境是否提供 CNG 非对称算法（RSA / ECC / DH / DSA）。
# 某些精简版或容器化 Windows 镜像只带对称算法提供者，此时 RSA 相关的
# BCryptGenerateKeyPair / BCryptExportKey / BCryptEncrypt 会一律返回
# STATUS_INVALID_HANDLE(0xC0000008) 或 STATUS_NOT_SUPPORTED(0xC00000BB)，
# 且 BCryptGetProperty(BCRYPT_OBJECT_LENGTH) 直接返回 NOT_SUPPORTED。
# 本程序用于在 CI 上一次性确认，避免把环境问题误判为实现缺陷。

import ctypes, sys
from ctypes import wintypes

bc = ctypes.WinDLL("bcrypt.dll")
NTSTATUS = ctypes.c_long

bc.BCryptOpenAlgorithmProvider.argtypes = [ctypes.POINTER(ctypes.c_void_p),
                                          ctypes.c_wchar_p, ctypes.c_wchar_p,
                                          ctypes.c_ulong]
bc.BCryptOpenAlgorithmProvider.restype = NTSTATUS
bc.BCryptCloseAlgorithmProvider.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
bc.BCryptGetProperty.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_void_p,
                                 ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong),
                                 ctypes.c_ulong]
bc.BCryptGetProperty.restype = NTSTATUS

ALGS = ["RSA", "AES", "SHA256", "ECDH", "ECDSA_P256", "CHACHA20_POLY1305", "DH", "DSA"]

def probe(name):
    h = ctypes.c_void_p()
    st = bc.BCryptOpenAlgorithmProvider(ctypes.byref(h), name, None, 0)
    if st < 0:
        return st, None
    ol = ctypes.c_ulong(0)
    n = ctypes.c_ulong(0)
    st2 = bc.BCryptGetProperty(h, "ObjectLength", ctypes.byref(ol), ctypes.sizeof(ol),
                               ctypes.byref(n), 0)
    bc.BCryptCloseAlgorithmProvider(h, 0)
    return st, st2

print("CNG capability probe")
print("-" * 52)
asym_ok = True
for a in ALGS:
    st, st2 = probe(a)
    obj = "n/a" if st2 is None else ("0x%08X" % (st2 & 0xFFFFFFFF))
    print(f"  {a:20s} open={'OK' if st >= 0 else 'FAIL'}  ObjectLength={obj}")
    if a in ("RSA", "ECDH", "ECDSA_P256", "DH", "DSA") and st2 is not None and st2 < 0:
        asym_ok = False
print("-" * 52)
if asym_ok:
    print("RESULT: asymmetric CNG available")
    sys.exit(0)
print("RESULT: asymmetric CNG NOT available on this host")
sys.exit(2)
