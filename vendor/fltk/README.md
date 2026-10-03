# vendor/fltk — FLTK 1.4.5

## 来源与版本

| 项目 | 值 |
|---|---|
| 版本 | 1.4.5（`fltk_version.dat` 内容为 `1.4.5`） |
| 官方地址 | https://www.fltk.org/ |
| 源码包 | `vendor/fltk-1.4.5-source.tar.gz`（上游 `fltk-1.4.5-source.tar.gz`） |
| 许可 | LGPL-2.1（见 `COPYING`） |

## 为什么必须入库

桌面三端（Windows / Linux / macOS）**静态链接同一份 FLTK**，界面行为才能保证一致。
若依赖各平台包管理器安装，版本会随时漂移，与密码学一致性纪律（见 `vendor/mbedtls/README.md`）
是同一类风险：一旦某个平台拿到不同版本，界面差异会以难以复现的方式出现。

LGPL-2.1 允许静态链接。本项目静态链接后不修改 FLTK 源码，也不单独分发 FLTK 库文件，
产物为单一可执行文件，因此不触发 LGPL 的源码开放义务。

## 保留了什么

| 路径 | 用途 | 说明 |
|---|---|---|
| `src/` | FLTK 实现 | 核心源码，编译 `fltk.lib` |
| `FL/` | 公开头文件 | 应用侧 `#include <FL/...>` |
| `CMake/`、`CMakeLists.txt` | 构建脚本 | 由根 `CMakeLists.txt` 以 `add_subdirectory` 引入 |
| `png/`、`jpeg/`、`zlib/`、`nanosvg/` | 内置图像与压缩库 | 避免依赖系统库 |
| `libdecor/` | Linux 窗口装饰 | Linux 端可选 |
| `fltk-options/` | 运行时选项 | 由 `FLTK_BUILD_FLTK_OPTIONS` 控制，默认关闭 |
| `*.cmake.in`、`fltk-config.in` | 构建期模板 | CMake `configure_file` 必需，**不可删除** |

## 已裁剪的内容

以下目录已删除，构建不需要：

- `documentation/`、`examples/`、`test/`、`fluid/`、`cairo/`
- autotools 相关：`configure`、`configure.ac`、`autogen.sh`、`config.guess`、
  `config.sub`、`install-sh`、`Makefile`、`makeinclude.in`
- 对应的非 CMake 模板：`configh.in`、`fl_config.in`、`fltk.spec.in`、`fltk.list.in`

**注意**：`configh.cmake.in`、`fl_config.cmake.in`、`fltk-config.in` 是 CMake 构建的
必需模板，删掉会导致 `configure_file` 失败。这三个文件与已被删除的同名 `.in`
容易混淆，裁剪时不要一起删。

## 构建选项

根 `CMakeLists.txt` 中固定关闭以下选项以缩短构建时间：

```cmake
set(FLTK_BUILD_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_TEST         OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FLUID        OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FORMS        OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_HTML_DOCS    OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FLUID_DOCS   OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FLTK_OPTIONS OFF CACHE BOOL "" FORCE)
set(FLTK_OPTION_STD         ON  CACHE BOOL "" FORCE)
```

`FLTK_OPTION_STD=ON` 允许 FLTK 使用部分 C++ 标准库特性，这是 Windows 与 macOS
构建所必需的（默认 `OFF` 会走 C++98 兼容路径）。

## 裁剪纪律

**不要修改 `src/` 与 `FL/` 中的上游文件。** 若需裁剪功能或行为，必须以追加 override 的方式
在根 `CMakeLists.txt` 中通过编译定义或选项完成，使上游文件保持字节一致——这样将来升级
FLTK 版本时 diff 可读。参见 `AGENTS.md` 第 9.2 节对 `vendor/mbedtls` 的同类要求。
