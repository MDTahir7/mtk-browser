import io
import os
import base64
from pathlib import Path
from PIL import Image, ImageFilter
import numpy as np

repo_root = Path(__file__).resolve().parent.parent

# User-uploaded master logo (No-text Globe + Shield + Orbital Cyber Ring)
user_upload_path = Path(r"C:\Users\LENOVO\.gemini\antigravity-ide\brain\0e677374-3182-48fe-893f-a73d29508224\.user_uploaded\media_1792210131041.png")
master_save_path = repo_root / "assets" / "logo" / "master_globe_shield_logo.png"

master_save_path.parent.mkdir(parents=True, exist_ok=True)
if user_upload_path.exists():
    import shutil
    shutil.copyfile(user_upload_path, master_save_path)
    print(f"Copied user upload to {master_save_path}")

src_path = master_save_path if master_save_path.exists() else user_upload_path
if not src_path.exists():
    raise FileNotFoundError(f"Source logo not found at {src_path}")

print(f"Opening: {src_path}")
src_img = Image.open(src_path).convert("RGBA")
w, h = src_img.size
print(f"Loaded size: {w}x{h}")

# Step 1: Accurate superellipse / squircle mask (order n=5.0) to make the outer canvas corners transparent
scale = 4
sw, sh = w * scale, h * scale
cx = 330.0 * scale
cy = 330.5 * scale
rx = 316.5 * scale
ry = 316.5 * scale
n = 5.0

Y, X = np.ogrid[:sh, :sw]
dist = ((np.abs(X - cx) / rx) ** n) + ((np.abs(Y - cy) / ry) ** n)
mask_hi = np.where(dist <= 1.0, 255, 0).astype(np.uint8)
mask_img = Image.fromarray(mask_hi).resize((w, h), Image.Resampling.LANCZOS)

res = src_img.copy()
res.putalpha(mask_img)

# Step 2: Crop tightly to the squircle bounding box (no wasted canvas)
bbox = (13, 14, 647, 647)
cropped_squircle = res.crop(bbox)
print(f"Tight squircle size: {cropped_squircle.size}")

# Step 3: Resize to master 512x512
final_512 = cropped_squircle.resize((512, 512), Image.Resampling.LANCZOS)

# Target directories
native_res = repo_root / "native" / "resources"
dist_dir = repo_root / "dist" / "MTK Browser"
assets_logo = repo_root / "assets" / "logo"
assets_icons = repo_root / "assets" / "icons"
ui_assets = repo_root / "src" / "ui" / "assets"
ui_dir = repo_root / "src" / "ui"

for p in [native_res, dist_dir, assets_logo, assets_icons, ui_assets, ui_dir]:
    p.mkdir(parents=True, exist_ok=True)

# 1. Master PNG
for out_dir in [native_res, dist_dir, assets_logo, ui_assets]:
    final_512.save(out_dir / "mtk_browser_logo.png", format="PNG", optimize=True)
print("Saved master PNGs (512x512)")

# 2. Multi-resolution ICO (256, 128, 64, 48, 32, 24, 16)
ico_sizes = [256, 128, 64, 48, 32, 24, 16]
ico_images = []
for s in ico_sizes:
    img_s = final_512.resize((s, s), Image.Resampling.LANCZOS)
    if s <= 48:
        # Subtle unsharp mask so small desktop/taskbar icons are ultra-crisp
        img_s = img_s.filter(ImageFilter.UnsharpMask(radius=1.0, percent=130, threshold=2))
    ico_images.append(img_s)

for out_dir in [native_res, dist_dir, assets_logo]:
    ico_images[0].save(out_dir / "mtk_browser.ico", format="ICO", sizes=[(s, s) for s in ico_sizes])

# Favicon
fav_images = [final_512.resize((s, s), Image.Resampling.LANCZOS) for s in [32, 16]]
fav_images[0].save(ui_dir / "favicon.ico", format="ICO", sizes=[(32, 32), (16, 16)])
print("Saved multi-res ICOs")

# 3. WebP
for out_dir in [native_res, dist_dir, assets_logo, ui_assets]:
    final_512.save(out_dir / "mtk_browser_logo.webp", format="WEBP", quality=95)
print("Saved WebPs")

# 4. Standard icon resolutions
for r in [16, 24, 32, 48, 64, 128, 192, 256, 512]:
    resized = final_512.resize((r, r), Image.Resampling.LANCZOS)
    if r <= 48:
        resized = resized.filter(ImageFilter.UnsharpMask(radius=1.0, percent=130, threshold=2))
    resized.save(assets_icons / f"icon-{r}.png", format="PNG", optimize=True)
    if r in [16, 32, 48, 64, 192]:
        resized.save(ui_assets / f"icon-{r}.png", format="PNG", optimize=True)
print("Saved icon sizes")

# 5. Embedded SVG
buf = io.BytesIO()
final_512.save(buf, format="PNG")
b64_png = base64.b64encode(buf.getvalue()).decode("utf-8")
svg_content = f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" width="512" height="512">
  <image width="512" height="512" href="data:image/png;base64,{b64_png}" />
</svg>
"""
(assets_logo / "mtk_browser_logo.svg").write_text(svg_content, encoding="utf-8")
(ui_assets / "mtk_browser_logo.svg").write_text(svg_content, encoding="utf-8")
print("Saved SVG")
print("SUCCESS: Master clean no-text globe logo exported in all resolutions!")
