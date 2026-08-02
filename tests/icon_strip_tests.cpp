#include "test_support.h"

#include <windows.h>

#include "icon_strip.h"

// The strip is loaded from the directory holding the running executable, so
// scripts/run_tests.ps1 copies the shipped asset next to the test binary. That
// keeps this test checking the real rendered file rather than a stand-in.
MTK_TEST(ShieldStripLoadsAsSquareFrames) {
  mtk::icons::Strip strip;
  MTK_EXPECT_TRUE(strip.Load(L"mtk_shield_hover.png"));
  MTK_EXPECT_TRUE(strip.valid());
  // One second at 30 fps: swords drawn, impact, then the tick.
  MTK_EXPECT_EQ_INT(strip.frame_count(), 30);
}

MTK_TEST(MissingStripLeavesLoaderInvalid) {
  mtk::icons::Strip strip;
  MTK_EXPECT_FALSE(strip.Load(L"mtk_no_such_icon_strip.png"));
  MTK_EXPECT_FALSE(strip.valid());
  MTK_EXPECT_EQ_INT(strip.frame_count(), 0);
}

MTK_TEST(EmptyFileNameIsRejected) {
  mtk::icons::Strip strip;
  MTK_EXPECT_FALSE(strip.Load(L""));
  MTK_EXPECT_FALSE(strip.valid());
}

MTK_TEST(StripDrawsEveryFrameAndWrapsIndices) {
  mtk::icons::Strip strip;
  if (!strip.Load(L"mtk_shield_hover.png")) {
    MTK_EXPECT_TRUE(false);
    return;
  }

  // A 32-bit memory surface stands in for the toolbar button.
  HDC screen = GetDC(nullptr);
  HDC target = CreateCompatibleDC(screen);
  HBITMAP surface = CreateCompatibleBitmap(screen, 64, 64);
  HBITMAP previous = static_cast<HBITMAP>(SelectObject(target, surface));

  const RECT box{0, 0, 28, 28};
  for (int frame = 0; frame < strip.frame_count(); ++frame) {
    MTK_EXPECT_TRUE(strip.Draw(target, box, frame));
  }
  // Out-of-range indices wrap instead of failing, so a running animation cannot
  // step off the end of the strip.
  MTK_EXPECT_TRUE(strip.Draw(target, box, strip.frame_count() + 3));
  MTK_EXPECT_TRUE(strip.Draw(target, box, -1));
  // A collapsed target is refused rather than drawn.
  const RECT empty{4, 4, 4, 4};
  MTK_EXPECT_FALSE(strip.Draw(target, empty, 0));

  SelectObject(target, previous);
  DeleteObject(surface);
  DeleteDC(target);
  ReleaseDC(nullptr, screen);
}

MTK_TEST(StripScalesToSeveralDpiSizes) {
  mtk::icons::Strip strip;
  if (!strip.Load(L"mtk_shield_hover.png")) {
    MTK_EXPECT_TRUE(false);
    return;
  }

  HDC screen = GetDC(nullptr);
  HDC target = CreateCompatibleDC(screen);
  HBITMAP surface = CreateCompatibleBitmap(screen, 128, 128);
  HBITMAP previous = static_cast<HBITMAP>(SelectObject(target, surface));

  // 100%, 125%, 150% and 200% of a 24 px icon.
  for (const int size : {24, 30, 36, 48}) {
    const RECT box{0, 0, size, size};
    MTK_EXPECT_TRUE(strip.Draw(target, box, 0));
    // Drawing the same size twice exercises the cached scaled bitmap.
    MTK_EXPECT_TRUE(strip.Draw(target, box, 1));
  }

  SelectObject(target, previous);
  DeleteObject(surface);
  DeleteDC(target);
  ReleaseDC(nullptr, screen);
}
