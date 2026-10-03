Mbed TLS 3.6.7 (vendored)

Source: https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.7
License: Apache-2.0 (dual-licensed upstream; Apache-2.0 chosen)
Files: library/ and include/ only -- tests, programs/, docs/ and CI config are omitted.

Why vendored
------------
The project's CNG-based RSA path could not be executed on any available host:
Windows 11 build 22631 and the GitHub windows-2022 runner both ship a CNG
without asymmetric algorithms, so RSA keygen / OAEP always failed with
STATUS_NOT_SUPPORTED or STATUS_INVALID_HANDLE. mbedTLS is pure software, so the
same code path runs identically everywhere -- on Windows, on Linux, and inside
the CI runners.

Vendoring (rather than using a system package) also guarantees that Windows and
Linux compile the identical source, which is the only way the five platforms can
be held to one common behaviour.

Why NOT Mbed TLS 4.x
--------------------
4.x is a licence change (this project requires Apache-2.0 or GPL-2.0-with-classpath
and prefers the permissive option) and a breaking API revision. 3.6.7 is the last
3.x release and keeps the stable 3.x API.

Configuration
-------------
include/mbedtls/mbedtls_config.h has an override block appended at the end of the
file. Appending rather than inlining keeps the upstream file byte-identical so a
future version sync produces a reviewable diff. The override disables TLS,
X.509, net and file I/O, and keeps only RSA, AES-GCM, SHA-256, BIGNUM,
ASN.1/PKCS#1/PKCS#8 and CTR-DRBG over platform entropy.

Modifying the vendored copy
---------------------------
Do not hand-edit library/ or include/ except for the config override block at the
bottom of mbedtls_config.h. Record any other change here with its reason.
