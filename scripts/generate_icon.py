import binascii, struct, zlib
from pathlib import Path

SIZE = 256
pixels = bytearray()

def inside_rounded(x, y, r=48):
    cx = min(max(x, r), SIZE - 1 - r)
    cy = min(max(y, r), SIZE - 1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r

def in_poly(x, y, points):
    hit = False
    j = len(points) - 1
    for i, (xi, yi) in enumerate(points):
        xj, yj = points[j]
        if ((yi > y) != (yj > y)) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            hit = not hit
        j = i
    return hit

def segment_distance(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return ((px - ax - t * dx) ** 2 + (py - ay - t * dy) ** 2) ** 0.5

shield = [(128, 39), (203, 72), (191, 164), (128, 220), (65, 164), (53, 72)]
for y in range(SIZE):
    pixels.append(0)
    for x in range(SIZE):
        if not inside_rounded(x, y):
            pixels.extend((0, 0, 0, 0)); continue
        t = (x + y) / (2 * (SIZE - 1))
        bg = (int(89 + 25*t), int(65 + 52*t), int(210 + 35*t), 255)
        if in_poly(x, y, shield): bg = (17, 24, 43, 255)
        edge = min(segment_distance(x,y,*shield[i],*shield[(i+1)%6]) for i in range(6))
        if edge < 4: bg = (241, 245, 255, 255)
        m = min(segment_distance(x,y,88,151,88,94), segment_distance(x,y,88,94,128,137),
                segment_distance(x,y,128,137,168,94), segment_distance(x,y,168,94,168,151))
        if m < 8: bg = (74, 222, 128, 255)
        pixels.extend(bg)

raw = bytes(pixels)
def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', binascii.crc32(kind + data) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', SIZE,SIZE,8,6,0,0,0)) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
ico = struct.pack('<HHHBBBBHHII', 0,1,1,0,0,0,0,1,32,len(png),22) + png
out = Path(__file__).resolve().parents[1] / 'native' / 'resources'
out.mkdir(parents=True, exist_ok=True)
(out / 'mtk_browser.ico').write_bytes(ico)
print(out / 'mtk_browser.ico')