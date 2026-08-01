#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Frame strips are how the toolbar plays a rendered 3D icon without hosting a
// browser or a video decoder in the chrome: the animation ships as one PNG whose
// square frames sit side by side, and the button blits one frame at a time.
namespace mtk::icons {

class Strip final {
 public:
  Strip() = default;
  ~Strip();

  Strip(const Strip&) = delete;
  Strip& operator=(const Strip&) = delete;

  // Loads `file_name` from the directory holding the executable. A missing,
  // unreadable or wrongly shaped file leaves the strip invalid, and every caller
  // is expected to fall back to its static glyph in that case.
  bool Load(const std::wstring& file_name);

  bool valid() const { return frame_count_ > 0; }
  int frame_count() const { return frame_count_; }

  // Draws one frame centred in `target`, scaled to fit. Frames are premultiplied
  // and drawn with AlphaBlend, so rounded edges stay clean over the gradient
  // toolbar. Scaled frames are cached per size, keeping repaints cheap.
  bool Draw(HDC dc, const RECT& target, int frame);

 private:
  struct Cache {
    int size = 0;
    HBITMAP bitmap = nullptr;
  };

  // Returns a bitmap holding every frame scaled to `size` pixels square.
  HBITMAP ScaledStrip(int size);
  void Release();

  int frame_count_ = 0;
  int source_size_ = 0;
  // Premultiplied BGRA pixels of the original strip, kept so any DPI can be
  // rendered later without touching the disk again.
  std::vector<unsigned char> pixels_;
  std::vector<Cache> cache_;
};

}  // namespace mtk::icons
