#!/usr/bin/env python3
"""เรดาร์ฝนสำหรับบอร์ด ESP32-S3 — รันโดย GitHub Actions ทุก 10 นาที
- ดึงภาพเรดาร์ย้อนหลังจาก RainViewer (ฟรี) รอบตำแหน่งที่ตั้ง แล้ววางบนแผนที่พื้นหลังสีเข้ม
- ถ้าบริเวณนั้นไม่มีเรดาร์ครอบคลุม ใช้แผนที่ฝนจากแบบจำลอง Open-Meteo แทน
- วิเคราะห์ระยะ/ทิศของกลุ่มฝนที่ใกล้ที่สุด ความเร็ว และเวลาที่น่าจะถึง
ผลลัพธ์: out/radar/f0..f3.jpg (224x204) + out/radar/radar.json
แผนที่พื้นหลัง: Esri World Dark Gray Base (เก็บแคชไว้ใน radar/base_v2.png ของ branch market-data)
"""
import io, json, math, os, sys, time, urllib.request
from PIL import Image, ImageDraw, ImageFilter

LAT = float(os.environ.get('RADAR_LAT', '13.7563'))
LON = float(os.environ.get('RADAR_LON', '100.5018'))
OUT = sys.argv[1] if len(sys.argv) > 1 else 'out/radar'
W, H = 224, 204
KM_PER_PX = 0.45            # มาตราส่วนภาพที่ส่งให้บอร์ด (≈ 50 x 46 กม.)
UA = {'User-Agent': 'esp32-weather-radar/1.0 (+https://github.com/wssnuk/esp32-weather)'}
os.makedirs(OUT, exist_ok=True)


def get(url, timeout=20):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=timeout) as r:
        return r.read()


def tile_xy(lat, lon, z):
    n = 2 ** z
    x = (lon + 180) / 360 * n
    y = (1 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2 * n
    return x, y


def km_per_px_at(z, lat):
    return 40075.016686 * math.cos(math.radians(lat)) / (256 * 2 ** z)


def mosaic(url_fmt, z, mode='RGBA'):
    """ต่อภาพ tile 3x3 รอบตำแหน่ง แล้วตัดให้ตรงกลาง คืนภาพ + ตำแหน่งจุดศูนย์กลาง"""
    fx, fy = tile_xy(LAT, LON, z)
    tx, ty = int(fx), int(fy)
    big = Image.new(mode, (256 * 3, 256 * 3), (0, 0, 0, 0) if mode == 'RGBA' else (0, 0, 0))
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            try:
                t = Image.open(io.BytesIO(get(url_fmt.format(z=z, x=tx + dx, y=ty + dy)))).convert(mode)
                big.paste(t, ((dx + 1) * 256, (dy + 1) * 256))
            except Exception as e:  # noqa
                print('tile error', e, file=sys.stderr)
    cx, cy = (fx - tx + 1) * 256, (fy - ty + 1) * 256
    return big, cx, cy


def crop_scaled(big, cx, cy, z):
    """ตัดภาพรอบจุดศูนย์กลางให้ได้มาตราส่วน KM_PER_PX แล้วย่อ/ขยายเป็น W x H"""
    src_kmpp = km_per_px_at(z, LAT)
    s = KM_PER_PX / src_kmpp          # จำนวนพิกเซลต้นทางต่อ 1 พิกเซลปลายทาง
    w, h = W * s, H * s
    box = (int(cx - w / 2), int(cy - h / 2), int(cx + w / 2), int(cy + h / 2))
    return big.crop(box).resize((W, H), Image.LANCZOS)


def basemap(cache_path):
    """แผนที่พื้นหลังโทนเข้ม (Esri Dark Gray) — สร้างครั้งเดียวแล้วเก็บไว้ใช้ซ้ำ"""
    if cache_path and os.path.exists(cache_path):
        img = Image.open(cache_path).convert('RGB')
        if img.size == (W, H):
            return img
    z = 9
    img, cx, cy = mosaic('https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Dark_Gray_Base/MapServer/tile/{z}/{y}/{x}', z, 'RGB')
    m = crop_scaled(img, cx, cy, z)
    # โทนน้ำเงินเข้มให้เข้ากับธีมบนบอร์ด และสว่างขึ้นเล็กน้อย
    r, g, b = m.split()
    m = Image.merge('RGB', (Image.eval(r, lambda v: int(v * 0.9)), Image.eval(g, lambda v: int(v * 1.0 + 4)),
                            Image.eval(b, lambda v: min(255, int(v * 1.25 + 22)))))
    if cache_path:
        m.save(cache_path)
    return m


def rainviewer_frames():
    meta = json.loads(get('https://api.rainviewer.com/public/weather-maps.json'))
    host = meta['host']
    past = meta['radar']['past'][-4:]
    frames = []
    for z in (8, 7):
        frames = []
        for p in past:
            url = host + p['path'] + '/256/{z}/{x}/{y}/4/1_1.png'
            img, cx, cy = mosaic(url, z)
            frames.append((p['time'], crop_scaled(img, cx, cy, z)))
        if frames:
            break
    # ตรวจว่าบริเวณนี้มีเรดาร์ครอบคลุมไหม
    covered = True
    try:
        cimg, cx, cy = mosaic(host + '/v2/coverage/0/256/{z}/{x}/{y}/0/0_0.png', 7)
        c = crop_scaled(cimg, cx, cy, 7)
        a = c.getchannel('A')
        covered = a.getpixel((W // 2, H // 2)) > 0
    except Exception as e:  # noqa
        print('coverage check failed', e, file=sys.stderr)
    return frames, covered


def model_frames():
    """แผนที่ฝนจากแบบจำลอง Open-Meteo (ตาราง 9x9 จุด) — ใช้เมื่อไม่มีเรดาร์"""
    n = 15
    span_km = 70
    lats, lons = [], []
    for j in range(n):
        for i in range(n):
            dy = (j - (n - 1) / 2) * span_km / (n - 1)
            dx = (i - (n - 1) / 2) * span_km / (n - 1)
            lats.append(round(LAT - dy / 111.0, 4))
            lons.append(round(LON + dx / (111.0 * math.cos(math.radians(LAT))), 4))
    url = ('https://api.open-meteo.com/v1/forecast?latitude=' + ','.join(map(str, lats)) + '&longitude=' +
           ','.join(map(str, lons)) + '&minutely_15=precipitation&past_minutely_15=3&forecast_minutely_15=1&timezone=UTC')
    data = json.loads(get(url, 40))
    if isinstance(data, dict):
        data = [data]
    frames = []
    steps = len(data[0]['minutely_15']['precipitation'])
    for k in range(steps):
        grid = Image.new('F', (n, n))
        for idx, d in enumerate(data):
            v = d['minutely_15']['precipitation'][k] or 0
            grid.putpixel((idx % n, idx // n), float(v))
        frames.append((int(time.time()) - (steps - 1 - k) * 900, grid))
    out = []
    for t, grid in frames[-4:]:
        big = grid.resize((W, H), Image.BICUBIC)
        rgba = Image.new('RGBA', (W, H))
        px = big.load()
        dst = rgba.load()
        for y in range(H):
            for x in range(W):
                mm = px[x, y] * 4  # มม./ชม. โดยประมาณ
                dst[x, y] = color_for_mm(mm)
        out.append((t, rgba))
    return out


def color_for_mm(mm):
    if mm < 0.2:
        return (0, 0, 0, 0)
    stops = [(0.2, (60, 200, 90, 150)), (2, (240, 220, 60, 190)), (8, (240, 90, 60, 210)), (25, (220, 40, 180, 230))]
    c = stops[0][1]
    for v, col in stops:
        if mm >= v:
            c = col
    return c


def analyze(frames):
    """หากลุ่มฝนที่ใกล้ที่สุด + ทิศทาง/ความเร็วการเคลื่อนที่"""
    def rain_mask(img):
        return [(x, y) for y in range(0, H, 2) for x in range(0, W, 2) if img.getpixel((x, y))[3] > 60]
    last = rain_mask(frames[-1][1])
    cx, cy = W / 2, H / 2
    res = {'raining_here': False, 'nearest_km': None, 'bearing': None, 'speed_kmh': None, 'eta_min': None, 'approaching': False}
    if not last:
        return res
    d = [(math.hypot(x - cx, y - cy), x, y) for x, y in last]
    dist, nx, ny = min(d)
    res['nearest_km'] = round(dist * KM_PER_PX, 1)
    res['bearing'] = round((math.degrees(math.atan2(nx - cx, cy - ny)) + 360) % 360)
    res['raining_here'] = dist * KM_PER_PX < 2
    if len(frames) >= 2:
        prev = rain_mask(frames[-2][1])
        if prev:
            px_, py_ = sum(p[0] for p in prev) / len(prev), sum(p[1] for p in prev) / len(prev)
            lx, ly = sum(p[0] for p in last) / len(last), sum(p[1] for p in last) / len(last)
            dt_h = max(1, frames[-1][0] - frames[-2][0]) / 3600
            vx, vy = (lx - px_) * KM_PER_PX / dt_h, (ly - py_) * KM_PER_PX / dt_h
            speed = math.hypot(vx, vy)
            if 3 < speed < 120:
                res['speed_kmh'] = round(speed)
                # เข้าหาเราไหม: ความเร็วตามแนวจากกลุ่มฝนมาหาเรา
                ux, uy = cx - nx, cy - ny
                ul = math.hypot(ux, uy) or 1
                closing = (vx * ux + vy * uy) / ul
                if closing > 3 and not res['raining_here']:
                    res['approaching'] = True
                    res['eta_min'] = round(res['nearest_km'] / closing * 60)
    return res


def main():
    base = basemap(os.environ.get('RADAR_BASEMAP_CACHE'))
    source = 'rainviewer'
    try:
        frames, covered = rainviewer_frames()
        if not covered or not frames:
            raise RuntimeError('no radar coverage here')
    except Exception as e:  # noqa
        print('radar unavailable, using model:', e, file=sys.stderr)
        frames = model_frames()
        source = 'model'
    for i, (t, ov) in enumerate(frames):
        img = base.convert('RGBA')
        ov = ov.filter(ImageFilter.GaussianBlur(0.8))
        img.alpha_composite(ov)
        d = ImageDraw.Draw(img)
        d.text((4, H - 11), 'Esri' + (' · RainViewer' if source == 'rainviewer' else ' · Open-Meteo model'),
               fill=(150, 160, 180, 200))
        img.convert('RGB').save(os.path.join(OUT, f'f{i}.jpg'), quality=82)
    info = analyze(frames)
    info.update({'ok': True, 'source': source, 'generated': int(time.time()), 'lat': LAT, 'lon': LON,
                 'km_per_px': KM_PER_PX, 'w': W, 'h': H, 'frames': [t for t, _ in frames]})
    with open(os.path.join(OUT, 'radar.json'), 'w') as f:
        json.dump(info, f)
    print(json.dumps(info, indent=1))


if __name__ == '__main__':
    main()
