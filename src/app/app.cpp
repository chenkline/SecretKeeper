// SecretKeeper - 程序入口
//
// 界面实现在 main_window.cpp 与 pages.cpp，本文件只负责创建窗口与运行事件循环。
// 三端共享同一份代码；只有 platform.cpp 逐端不同。
//
// 分层约束（见 AGENTS.md）：本层只依赖 core/service.h（唯一对外门面）、
// view_model.h（可测的展示规则）与 platform.h（必须用 OS API 的四项能力），
// 不得包含 crypto / serialize / store 的任何头文件。
//
// 调试出口：`--screenshot=01,02` 按页号逐页渲染并保存 BMP 到
// build/ui-shots/（文件名纯 ASCII），保存后直接退出，不进事件循环。
// 仅用于开发期与设计稿比对，产物不入库。

#include "app.h"

#include <FL/Fl.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/fl_draw.H>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "main_window.h"
#include "platform.h"
#include "theme.h"

namespace {

// 把 fl_read_image 取到的自上而下 RGB 写成 24 位 BMP（自下而上、BGR、4 字节对齐）。
bool write_bmp(const std::string& path, const unsigned char* rgb, int w, int h) {
  const int row_raw = w * 3;
  const int row_pad = (4 - (row_raw % 4)) % 4;
  const int row_size = row_raw + row_pad;
  const std::uint32_t data_size = static_cast<std::uint32_t>(row_size) * h;
  const std::uint32_t file_size = 54 + data_size;

  unsigned char header[54] = {0};
  header[0] = 'B';
  header[1] = 'M';
  std::memcpy(header + 2, &file_size, 4);
  header[10] = 54;  // 像素数据偏移
  header[14] = 40;  // BITMAPINFOHEADER
  std::memcpy(header + 18, &w, 4);
  std::memcpy(header + 22, &h, 4);
  header[26] = 1;   // 平面数
  header[28] = 24;  // 位深
  std::memcpy(header + 34, &data_size, 4);

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(reinterpret_cast<const char*>(header), sizeof(header));
  std::vector<unsigned char> row(static_cast<std::size_t>(row_size), 0);
  for (int y = h - 1; y >= 0; --y) {
    for (int x = 0; x < w; ++x) {
      const unsigned char* px = rgb + (static_cast<std::size_t>(y) * w + x) * 3;
      row[static_cast<std::size_t>(x) * 3 + 0] = px[2];  // B
      row[static_cast<std::size_t>(x) * 3 + 1] = px[1];  // G
      row[static_cast<std::size_t>(x) * 3 + 2] = px[0];  // R
    }
    out.write(reinterpret_cast<const char*>(row.data()), row_size);
  }
  return out.good();
}

// 解析 --screenshot=01,02 之类参数，返回页号列表（1..9）；非法项忽略。
std::vector<int> parse_screenshot_arg(const char* arg) {
  std::vector<int> pages;
  const std::string value(arg);
  std::size_t pos = 0;
  while (pos < value.size()) {
    const std::size_t comma = value.find(',', pos);
    const std::string token =
        value.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
    if (!token.empty()) {
      try {
        const int n = std::stoi(token);
        if (n >= 1 && n <= 9) pages.push_back(n);
      } catch (...) {
        // 非法页号忽略。
      }
    }
    if (comma == std::string::npos) break;
    pos = comma + 1;
  }
  return pages;
}

}  // namespace

int main(int argc, char** argv) {
  // 必须在任何窗口与控件构造之前安装，否则构造期间的崩溃捕不到。
  secretkeeper::ui::install_crash_dumper();

  std::vector<int> shots;
  for (int i = 1; i < argc; ++i) {
    if (std::strncmp(argv[i], "--screenshot=", 13) == 0) {
      shots = parse_screenshot_arg(argv[i] + 13);
    }
  }

  Fl::visual(FL_RGB);
  secretkeeper::ui::MainWindow window;

  if (shots.empty()) {
    return Fl::run();
  }

  // 截屏模式：逐页导航、等绘制完成、抓图、存盘。
  using secretkeeper::ui::Page;
  static const Page kPages[9] = {
      Page::kUnlock,      Page::kMasterKeys,   Page::kCreateKey,
      Page::kImportKey,   Page::kExportKey,    Page::kSecrets,
      Page::kAddSecret,   Page::kImportSecret, Page::kSettings,
  };
  static const char* kNames[9] = {
      "01-unlock", "02-master-keys", "03-create-key", "04-import-key",
      "05-export-key", "06-secrets", "07-add-secret", "08-import-secret",
      "09-settings",
  };
  std::filesystem::create_directories("build/ui-shots");
  for (int n : shots) {
    window.show();
    Fl::wait(0.1);
    window.navigate(kPages[n - 1]);
    Fl::wait(0.05);
    // 离屏渲染：Fl_Double_Window 的 draw() 会拷贝自己的 backing store
    //（真实屏幕绘制的陈旧残留），因此这里绕开它——先铺窗口底色，
    // 再逐个绘制可见子控件，最后用 surface.image() 取像素。
    const int w = window.w();
    const int h = window.h();
    unsigned char* pixels = nullptr;
    {
      Fl_Image_Surface surface(w, h, 1);
      Fl_Surface_Device::push_current(&surface);
      fl_color(secretkeeper::ui::theme::kContentBg);
      fl_rectf(0, 0, w, h);
      const int n = window.children();
      for (int i = 0; i < n; ++i) {
        Fl_Widget* child = window.child(i);
        if (child != nullptr && child->visible()) {
          // 非 window 控件必须以自身坐标作增量传入，否则会被平移到原点。
          if (child->as_window() != nullptr) {
            surface.draw(child);
          } else {
            surface.draw(child, child->x(), child->y());
          }
        }
      }
      Fl_RGB_Image* img = surface.image();
      Fl_Surface_Device::pop_current();
      if (img != nullptr) {
        pixels = new unsigned char[static_cast<std::size_t>(w) * h * 3];
        std::memcpy(pixels, img->data()[0], static_cast<std::size_t>(w) * h * 3);
        delete img;
      }
    }
    if (pixels != nullptr) {
      const std::string path =
          std::string("build/ui-shots/") + kNames[n - 1] + ".bmp";
      if (write_bmp(path, pixels, w, h)) {
        std::printf("saved %s\n", path.c_str());
      } else {
        std::printf("FAILED to write %s\n", path.c_str());
      }
      delete[] pixels;
    } else {
      std::printf("FAILED to capture page %d\n", n);
    }
  }
  return 0;
}
