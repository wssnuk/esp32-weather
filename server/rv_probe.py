"""ตรวจว่า RainViewer มีข้อมูลเรดาร์บริเวณไทยไหม (ใช้ตอนพัฒนา)"""
import io, json, math, urllib.request
from PIL import Image
UA = {'User-Agent': 'esp32-weather-radar/1.0 (+https://github.com/wssnuk/esp32-weather)'}
def get(u):
    with urllib.request.urlopen(urllib.request.Request(u, headers=UA), timeout=30) as r:
        return r.status, r.headers.get('Content-Type'), r.read()
def tile(lat, lon, z):
    n = 2 ** z
    x = (lon + 180) / 360 * n
    y = (1 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2 * n
    return int(x), int(y)
st, ct, b = get('https://api.rainviewer.com/public/weather-maps.json')
meta = json.loads(b)
print('meta keys', list(meta.keys()), 'radar keys', list(meta['radar'].keys()))
print('host', meta['host'], 'past', [p['time'] for p in meta['radar']['past']][-3:], 'nowcast', len(meta['radar'].get('nowcast', [])))
for name, lat, lon in [('Bangkok', 13.75, 100.5), ('ChiangMai', 18.79, 98.98), ('Phuket', 7.88, 98.39), ('Berlin', 52.5, 13.4)]:
    for z in (5, 6, 7, 8):
        x, y = tile(lat, lon, z)
        for kind, url in [('cov', f"{meta['host']}/v2/coverage/0/256/{z}/{x}/{y}/0/0_0.png"),
                          ('radar', f"{meta['host']}{meta['radar']['past'][-1]['path']}/256/{z}/{x}/{y}/2/1_1.png")]:
            try:
                st, ct, b = get(url)
                im = Image.open(io.BytesIO(b)).convert('RGBA')
                a = im.getchannel('A')
                nz = sum(1 for v in a.getdata() if v > 0)
                print(name, z, kind, st, ct, len(b), 'alpha>0 px', nz, 'center', im.getpixel((128, 128)))
            except Exception as e:
                print(name, z, kind, 'ERR', e)
