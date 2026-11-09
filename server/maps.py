#!/usr/bin/env python3
"""ภาพแผนที่สำหรับบอร์ด S3 (รันใน GitHub Actions ทุก 10 นาที)

1) th/f0..f3.jpg   เรดาร์ฝนทั้งประเทศไทย (RainViewer = เรดาร์กรมอุตุฯ) 4 ภาพย้อนหลัง 30 นาที
2) storm/f0..f2.jpg ภาพเมฆอินฟราเรดดาวเทียม Himawari (NASA GIBS) + พายุหมุนเขตร้อน (GDACS) ย้อนหลัง 1 ชม.
3) meta.json        เวลาของแต่ละภาพ, ค่าพิกัดของภาพ (ให้บอร์ดปักหมุดตำแหน่งตัวเองได้), รายชื่อพายุในภูมิภาค

ภาพขนาด 320x240 JPEG (บอร์ดแสดงเต็มจอได้ทันที)
"""
import datetime as dt
import io
import json
import math
import os
import re
import sys
import time
import urllib.request

from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = sys.argv[1] if len(sys.argv) > 1 else 'out/maps'
CACHE = os.environ.get('MAPS_CACHE', '')  # โฟลเดอร์แคชแผนที่พื้น (จาก branch market-data)
UA = {'User-Agent': 'esp32-weather-maps/1.0 (+https://github.com/wssnuk/esp32-weather)'}
W, H = 320, 240
FONT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools', 'fonts', 'NotoThai700.ttf')


def get(url, tries=3):
    for i in range(tries):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=30) as r:
                return r.read()
        except Exception as e:  # noqa
            if i == tries - 1:
                raise
            time.sleep(2 + 2 * i)


def world(lat, lon, z):
    n = 256 * 2 ** z
    s = math.sin(math.radians(lat))
    return (lon + 180) / 360 * n, (0.5 - math.log((1 + s) / (1 - s)) / (4 * math.pi)) * n


def mosaic(fmt, z, x0, y0, w, h, tile=256, tz=None):
    """ประกอบไทล์ให้ครอบคลุมพื้นที่ (พิกัดโลกที่ zoom z) — tz = zoom ของไทล์ (ไทล์ 512 px ใช้ z-1)"""
    tz = z if tz is None else tz
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    for ty in range(int(y0 // tile), int((y0 + h - 1) // tile) + 1):
        for tx in range(int(x0 // tile), int((x0 + w - 1) // tile) + 1):
            n = 2 ** tz
            if ty < 0 or ty >= n:
                continue
            try:
                t = Image.open(io.BytesIO(get(fmt.format(z=tz, x=tx % n, y=ty)))).convert('RGBA')
                if t.size != (tile, tile):
                    t = t.resize((tile, tile))
                img.alpha_composite(t, (int(tx * tile - x0), int(ty * tile - y0)))
            except Exception as e:  # noqa
                print('tile failed', fmt[:60], tx, ty, e, file=sys.stderr)
    return img


ESRI = 'https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Dark_Gray_{}/MapServer/tile/{{z}}/{{y}}/{{x}}'


def basemap(name, z, x0, y0, w, h):
    """แผนที่พื้น + ชื่อสถานที่ (เก็บแคชไว้ เพราะไม่เปลี่ยน)"""
    path = os.path.join(CACHE, f'{name}.png') if CACHE else ''
    if path and os.path.exists(path):
        im = Image.open(path).convert('RGBA')
        if im.size == (w, h * 2):
            return im.crop((0, 0, w, h)), im.crop((0, h, w, 2 * h))
    base = mosaic(ESRI.format('Base'), z, x0, y0, w, h)
    lab = mosaic(ESRI.format('Reference'), z, x0, y0, w, h)
    both = Image.new('RGBA', (w, 2 * h))
    both.paste(base, (0, 0))
    both.paste(lab, (0, h))
    os.makedirs(os.path.join(OUT), exist_ok=True)
    both.save(os.path.join(OUT, f'{name}.png'))
    return base, lab


def brighten(im, k=1.25, add=6):
    return im.point(lambda v: min(255, int(v * k + add)))


def save_jpg(im, path):
    im.convert('RGB').save(path, 'JPEG', quality=86, subsampling=0)


# ---------------------------------------------------------------- 1) เรดาร์ฝนทั้งประเทศ
TH_Z, TH_SCALE = 5, 0.75          # สร้างที่ zoom 5 แล้วย่อ 0.75 → ~6.3 กม./พิกเซล ครอบคลุมทั้งประเทศ
TH_LAT, TH_LON = 13.6, 100.9


def thailand_radar(meta):
    cw, ch = int(W / TH_SCALE), int(H / TH_SCALE)
    cx, cy = world(TH_LAT, TH_LON, TH_Z)
    x0, y0 = int(cx - cw / 2), int(cy - ch / 2)
    base, lab = basemap('base_th', TH_Z, x0, y0, cw, ch)
    base = brighten(base.convert('RGB')).convert('RGBA')
    m = json.loads(get('https://api.rainviewer.com/public/weather-maps.json'))
    host, past = m['host'], m['radar']['past'][-4:]
    frames = []
    os.makedirs(os.path.join(OUT, 'th'), exist_ok=True)
    for i, p in enumerate(past):
        rad = mosaic(host + p['path'] + '/512/{z}/{x}/{y}/2/1_0.png', TH_Z, x0, y0, cw, ch, tile=512, tz=TH_Z - 1)
        a = rad.getchannel('A').point(lambda v: min(255, int(v * 1.1)))
        rad.putalpha(a)
        im = base.copy()
        im.alpha_composite(rad)
        im.alpha_composite(lab)
        im = im.resize((W, H), Image.LANCZOS)
        save_jpg(im, os.path.join(OUT, 'th', f'f{i}.jpg'))
        frames.append(p['time'])
    meta['th'] = {'frames': frames, 'z': TH_Z, 'x0': x0, 'y0': y0, 'scale': TH_SCALE}


# ---------------------------------------------------------------- 2) ดาวเทียม + พายุ
ST_Z, ST_SCALE = 4, 0.75          # ~13 กม./พิกเซล: อ่าวเบงกอล – ทะเลจีนใต้ – ฟิลิปปินส์
ST_LAT, ST_LON = 14.0, 104.0
GIBS = 'https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/Himawari_AHI_Band13_Clean_Infrared/default/{t}/GoogleMapsCompatible_Level6/{{z}}/{{y}}/{{x}}.png'


def gibs_latest():
    x = get('https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/wmts.cgi?SERVICE=WMTS&REQUEST=GetCapabilities').decode('utf-8', 'ignore')
    i = x.find('<ows:Identifier>Himawari_AHI_Band13_Clean_Infrared</ows:Identifier>')
    d = re.search(r'<Default>([^<]+)</Default>', x[i:i + 4000])
    return dt.datetime.strptime(d.group(1), '%Y-%m-%dT%H:%M:%SZ').replace(tzinfo=dt.timezone.utc)


def ir_alpha(img):
    """เมฆอุ่น/ต่ำ (สีเทา) ให้โปร่งบางส่วน, ยอดเมฆเย็นที่เป็นพายุ (มีสี) ทึบ"""
    px = img.load()
    w, h = img.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            sat = max(r, g, b) - min(r, g, b)
            if sat > 40:
                px[x, y] = (r, g, b, 255)
            else:
                v = (r + g + b) // 3
                px[x, y] = (v, v, min(255, v + 12), max(0, min(200, (v - 70) * 2)))
    return img


def storms_list():
    out = []
    try:
        j = json.loads(get('https://www.gdacs.org/gdacsapi/api/events/geteventlist/SEARCH?eventlist=TC'))
    except Exception as e:  # noqa
        print('gdacs failed', e, file=sys.stderr)
        return out
    for f in j.get('features', []):
        p = f['properties']
        if str(p.get('iscurrent', 'true')).lower() != 'true':
            continue
        lon, lat = f['geometry']['coordinates'][:2]
        if not (60 <= lon <= 180 and -15 <= lat <= 45):
            continue
        wind = (p.get('severitydata') or {}).get('severity')
        out.append({'name': p.get('eventname', ''), 'lat': lat, 'lon': lon,
                    'wind': round(wind) if wind else None, 'alert': p.get('alertlevel', ''),
                    'id': p.get('eventid'), 'ep': p.get('episodeid')})
    return out


def storm_tracks(st):
    lines = []
    try:
        g = json.loads(get(f"https://www.gdacs.org/gdacsapi/api/polygons/getgeometry?eventtype=TC&eventid={st['id']}&episodeid={st['ep']}"))
        for f in g.get('features', []):
            if f['geometry']['type'] == 'LineString':
                lines.append(f['geometry']['coordinates'])
    except Exception as e:  # noqa
        print('track failed', st['name'], e, file=sys.stderr)
    return lines


def storm_map(meta):
    cw, ch = int(W / ST_SCALE), int(H / ST_SCALE)
    cx, cy = world(ST_LAT, ST_LON, ST_Z)
    x0, y0 = int(cx - cw / 2), int(cy - ch / 2)
    base, lab = basemap('base_storm', ST_Z, x0, y0, cw, ch)
    base = brighten(base.convert('RGB'), 1.35, 10).convert('RGBA')
    latest = gibs_latest()
    storms = storms_list()
    tracks = {s['name']: storm_tracks(s) for s in storms}
    font = ImageFont.truetype(FONT, 12) if os.path.exists(FONT) else ImageFont.load_default()
    frames = []
    os.makedirs(os.path.join(OUT, 'storm'), exist_ok=True)
    for i, back in enumerate([60, 30, 0]):
        t = latest - dt.timedelta(minutes=back)
        ir = mosaic(GIBS.format(t=t.strftime('%Y-%m-%dT%H:%M:%SZ')), ST_Z, x0, y0, cw, ch)
        im = base.copy()
        im.alpha_composite(ir_alpha(ir))
        im.alpha_composite(lab)
        im = im.resize((W, H), Image.LANCZOS)
        d = ImageDraw.Draw(im)
        for s in storms:
            sx, sy = world(s['lat'], s['lon'], ST_Z)
            px, py = (sx - x0) * ST_SCALE, (sy - y0) * ST_SCALE
            for ln in tracks.get(s['name'], []):
                pts = [((world(la, lo, ST_Z)[0] - x0) * ST_SCALE, (world(la, lo, ST_Z)[1] - y0) * ST_SCALE) for lo, la in ln]
                if len(pts) > 1:
                    d.line(pts, fill=(255, 255, 255, 170), width=1)
            if -20 < px < W + 20 and -20 < py < H + 20:
                c = (255, 70, 70) if (s['wind'] or 0) >= 118 else (255, 170, 40) if (s['wind'] or 0) >= 63 else (255, 230, 90)
                for r, a0 in [(9, 0), (9, 180)]:
                    d.arc([px - r, py - r, px + r, py + r], a0, a0 + 120, fill=c, width=3)
                d.ellipse([px - 3, py - 3, px + 3, py + 3], fill=c)
                label = s['name'].split('-')[0]
                d.text((px + 12, py - 8), label, font=font, fill=(255, 255, 255), stroke_width=2, stroke_fill=(0, 0, 0))
        save_jpg(im, os.path.join(OUT, 'storm', f'f{i}.jpg'))
        frames.append(int(t.timestamp()))
    meta['storm'] = {'frames': frames, 'z': ST_Z, 'x0': x0, 'y0': y0, 'scale': ST_SCALE}
    meta['storms'] = [{k: s[k] for k in ('name', 'lat', 'lon', 'wind', 'alert')} for s in storms]


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    meta = {'generated': int(time.time())}
    for name, fn in [('th', thailand_radar), ('storm', storm_map)]:
        try:
            fn(meta)
        except Exception as e:  # noqa
            print(name, 'failed:', e, file=sys.stderr)
    with open(os.path.join(OUT, 'meta.json'), 'w') as f:
        json.dump(meta, f, separators=(',', ':'))
    print(json.dumps(meta)[:600])
