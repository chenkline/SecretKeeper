SQLite amalgamation (public-domain dedication by the SQLite authors).

Source: https://sqlite.org/2024/sqlite-amalgamation-3450000.zip
Version: 3.45.0 (2024-02-13)

Vendored for the same reason as vendor/argon2: the local machine has no SQLite
development headers or import library, and the project must not install new
tooling. Only sqlite3.c / sqlite3.h / sqlite3ext.h are kept; shell.c is a
command-line front-end and is not needed.
