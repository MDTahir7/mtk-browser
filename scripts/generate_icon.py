import io, struct
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1] / "native" / "resources"
logo_png = root / "mtk_browser_logo.png"
ico_out = root / "mtk_browser.ico"

img = Image.open(logo_png).convert("RGBA")
sizes = [256, 128, 64, 48, 32, 24, 16]
frames = []
for s in sizes:
    buf = io.BytesIO()
    img.resize((s, s), Image.Resampling.LANCZOS).save(buf, format="PNG", optimize=True)
    frames.append((s, buf.getvalue()))

header = struct.pack("<HHH", 0, 1, len(frames))
offset = 6 + 16 * len(frames)
entries = bytearray()
payloads = bytearray()
for s, data in frames:
    b = 0 if s == 256 else s
    entries.extend(struct.pack("<BBBBHHII", b, b, 0, 0, 1, 32, len(data), offset))
    payloads.extend(data)
    offset += len(data)

ico_out.write_bytes(header + bytes(entries) + bytes(payloads))
print(f"Generated {ico_out} ({ico_out.stat().st_size} bytes, {len(frames)} resolutions)")
