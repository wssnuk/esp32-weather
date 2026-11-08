"""ตรวจรูปแบบไทล์ RainViewer / Esri (ใช้ตอนพัฒนา)"""
import io, json, math, urllib.request
from PIL import Image
UA = {'User-Agent': 'esp32-weather-radar/1.0 (+https://github.com/wssnuk/esp32-weather)'}
def get(u):
    with urllib.request.urlopen(urllib.request.Request(u, headers=UA), timeout=30) as r:
        return r.status, r.headers.get('Content-Type'), r.read()
def tile(lat, lon, z):
    n = 2 ** z
    return int((lon + 180) / 360 * n), int((1 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2 * n)
meta = json.loads(get('https://api.rainviewer.com/public/weather-maps.json')[2])
path = meta['radar']['past'][-1]['path']
# หาไทล์ z7 แถบไทยที่มีฝนมากสุด
best = None
for lat in range(6, 21, 2):
    for lon in range(97, 106, 2):
        x, y = tile(lat, lon, 7)
        st, ct, b = get(f"{meta['host']}{path}/256/7/{x}/{y}/2/1_1.png")
        nz = sum(1 for v in Image.open(io.BytesIO(b)).convert('RGBA').getchannel('A').getdata() if v)
        if not best or nz > best[0]: best = (nz, x, y)
print('best rain tile z7', best)
nz, x, y = best
for size in (256, 512):
    for scheme in (0, 1, 2, 3, 4, 6, 8):
        try:
            st, ct, b = get(f"{meta['host']}{path}/{size}/7/{x}/{y}/{scheme}/0_0.png")
            im = Image.open(io.BytesIO(b)).convert('RGBA')
            px = [p for p in im.getdata() if p[3]]
            cols = sorted(set(px))[:6]
            print('size', size, 'scheme', scheme, st, len(b), im.size, 'rain px', len(px), 'sample', cols)
        except Exception as e:
            print('size', size, 'scheme', scheme, 'ERR', e)
for name, u in [('dark', 'https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Dark_Gray_Base/MapServer/tile/8/119/200'),
                ('ref', 'https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Dark_Gray_Reference/MapServer/tile/8/119/200')]:
    st, ct, b = get(u)
    print(name, st, ct, len(b), b[:4])
