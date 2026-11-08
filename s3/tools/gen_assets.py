#!/usr/bin/env python3
"""สร้างกราฟิกทั้งหมดของธีม Aurora Gold สำหรับบอร์ด ESP32-S3 2.8"
ผลลัพธ์: s3/esp32-weather-s3/assets.h (ฝังในเฟิร์มแวร์) + s3/tools/out/*.png (ไว้ดูตัวอย่าง)

- พื้นหลังแต่ละหน้า (JPEG): แสงออโรร่า การ์ดกระจก เส้นทอง ป้ายข้อความไทยที่ไม่เปลี่ยน
- ตัวเลขนาฬิกาสีทองเรืองแสง / ตัวเลขวินาที (PNG โปร่งใส)
- ไอคอนอากาศ 3 ขนาด (PNG โปร่งใส)
- ฟอนต์ VLW (ลบขอบเรียบ) สำหรับตัวเลข/อังกฤษ
- ข้อความไทยที่เปลี่ยนตามข้อมูล (alpha 4 บิต) — จอแสดงสระ/วรรณยุกต์ไทยซ้อนเองไม่ได้ จึงเรนเดอร์ไว้ก่อน
"""
import io, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gfx_helpers import *  # noqa
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'out')
SKETCH = os.path.join(HERE, '..', 'esp32-weather-s3')
os.makedirs(OUT, exist_ok=True)

GOLD = (255, 214, 120, 255)
MUT = (200, 214, 240, 255)
CYAN = (0, 230, 255, 255)
WHITE = (255, 255, 255, 255)


# ---------------------------------------------------------------- พื้นหลัง
def aurora_base():
    im = grad(W, H, (10, 14, 40), (26, 14, 52)).convert('RGBA')
    for (cx, cy, rx, ry, c) in [(60, 10, 180, 60, (0, 220, 255, 140)), (270, 20, 160, 60, (200, 80, 255, 130)),
                                (300, 240, 170, 60, (255, 90, 170, 70)), (20, 240, 140, 50, (0, 160, 255, 60))]:
        l = layer(im)
        ImageDraw.Draw(l).ellipse([(cx - rx) * K, (cy - ry) * K, (cx + rx) * K, (cy + ry) * K], fill=c)
        im = Image.alpha_composite(im, l.filter(ImageFilter.GaussianBlur(28 * K)))
    # ตารางเส้นบาง ๆ แบบ HUD
    l = layer(im)
    d = ImageDraw.Draw(l)
    for x in range(0, 321, 20):
        d.line([(x * K, 0), (x * K, 240 * K)], fill=(120, 200, 255, 12), width=1)
    for y in range(0, 241, 20):
        d.line([(0, y * K), (320 * K, y * K)], fill=(120, 200, 255, 12), width=1)
    return Image.alpha_composite(im, l)


def brackets(im, box, col=(0, 230, 255, 220), ln=8):
    # มุมแบบ HUD
    x0, y0, x1, y1 = box
    l = layer(im)
    d = ImageDraw.Draw(l)
    for (x, y, dx, dy) in [(x0, y0, 1, 1), (x1, y0, -1, 1), (x0, y1, 1, -1), (x1, y1, -1, -1)]:
        d.line([(x * K, y * K), ((x + dx * ln) * K, y * K)], fill=col, width=int(1.6 * K))
        d.line([(x * K, y * K), (x * K, (y + dy * ln) * K)], fill=col, width=int(1.6 * K))
    return Image.alpha_composite(Image.alpha_composite(im, l.filter(ImageFilter.GaussianBlur(2 * K))), l)


def gcard(im, box, r=12, accent=(0, 230, 255, 220)):
    im = glass(im, box, r=r, fill=(255, 255, 255, 26), line=(150, 200, 255, 90))
    return brackets(im, box, accent)


def hline(im, x0, x1, y, col=(0, 230, 255, 110)):
    l = layer(im)
    ImageDraw.Draw(l).line([(x0 * K, y * K), (x1 * K, y * K)], fill=col, width=K)
    return Image.alpha_composite(im, l)


def header(im, title):
    im = text(im, (12, 12), title, 11, GOLD, 'lm')
    return hline(im, 12, 308, 23)


def bg_home():
    im = aurora_base()
    l = layer(im)
    ImageDraw.Draw(l).rounded_rectangle([14 * K, 90 * K, 306 * K, 93 * K], 2 * K, fill=(255, 255, 255, 35))
    im = Image.alpha_composite(im, l)
    im = gcard(im, (10, 132, 310, 190))
    for i, lab in enumerate(['รู้สึก', 'ฝน', 'PM2.5', 'UV']):
        x = 150 + i * 40
        im = text(im, (x, 176), lab, 9, MUT, 'mm', b=False)
        if i:
            l = layer(im)
            ImageDraw.Draw(l).line([((x - 20) * K, 142 * K), ((x - 20) * K, 182 * K)], fill=(255, 255, 255, 40), width=K)
            im = Image.alpha_composite(im, l)
    im = glass(im, (10, 198, 310, 226), r=12, fill=(0, 170, 255, 40), line=(0, 200, 255, 140))
    return im


def bg_summary():
    im = header(aurora_base(), 'สรุปวันนี้')
    tiles = [('อุณหภูมิวันนี้', (255, 150, 90, 255)), ('ฝน', (90, 170, 255, 255)), ('ฝุ่น PM2.5', (120, 240, 160, 255)),
             ('รังสี UV', (255, 100, 220, 255)), ('พระอาทิตย์ขึ้น / ตก', (255, 214, 120, 255)), ('ความชื้น · ลม', (0, 230, 255, 255))]
    for i, (lab, c) in enumerate(tiles):
        x = 10 + (i % 2) * 154
        y = 30 + (i // 2) * 56
        im = gcard(im, (x, y, x + 146, y + 50), r=10, accent=c)
        l = layer(im)
        ImageDraw.Draw(l).rounded_rectangle([(x + 4) * K, (y + 10) * K, (x + 7) * K, (y + 40) * K], 2 * K, fill=c)
        im = Image.alpha_composite(im, l)
        im = text(im, (x + 14, y + 11), lab, 9, MUT, 'lm', b=False)
    return im


def bg_radar():
    im = header(aurora_base(), 'แผนที่ฝน')
    im = gcard(im, (238, 28, 312, 232), r=10)
    for i, (c, t) in enumerate([((60, 200, 90), 'เบา'), ((240, 220, 60), 'ปานกลาง'), ((240, 90, 60), 'หนัก'), ((220, 40, 180), 'หนักมาก')]):
        y = 176 + i * 13
        l = layer(im)
        ImageDraw.Draw(l).rounded_rectangle([246 * K, (y - 3) * K, 256 * K, (y + 4) * K], K, fill=c + (255,))
        im = Image.alpha_composite(im, l)
        im = text(im, (260, y), t, 8, MUT, 'lm', b=False)
    return im


def bg_now():
    im = header(aurora_base(), 'อากาศตอนนี้')
    labels = ['ความชื้น', 'ลม กม./ชม.', 'ฝุ่น PM2.5', 'ดัชนี UV']
    for i, n in enumerate(labels):
        x = 12 + (i % 2) * 150
        y = 122 + (i // 2) * 54
        im = gcard(im, (x, y, x + 144, y + 48), r=10)
        l = layer(im)
        ImageDraw.Draw(l).arc([(x + 8) * K, (y + 6) * K, (x + 44) * K, (y + 42) * K], 0, 360, fill=(255, 255, 255, 28), width=4 * K)
        im = Image.alpha_composite(im, l)
        im = text(im, (x + 54, y + 12), n, 9, MUT, 'la', b=False)
    return im


def bg_fc():
    im = header(aurora_base(), 'พยากรณ์อากาศ')
    im = text(im, (304, 100), 'โอกาสฝน %', 8, (120, 190, 255, 255), 'ra', b=False)
    for i in range(4):
        y = 140 + i * 24
        if i:
            im = hline(im, 16, 304, y - 2, (255, 255, 255, 22))
        l = layer(im)
        ImageDraw.Draw(l).rounded_rectangle([196 * K, (y + 7) * K, 270 * K, (y + 11) * K], 2 * K, fill=(255, 255, 255, 25))
        im = Image.alpha_composite(im, l)
        im = text(im, (132, y + 9), 'ฝน', 9, (120, 190, 255, 255), 'lm', b=False)
    return im


def bg_plain():
    return aurora_base()


def to_jpeg(img, name):
    rgb = finish(img)
    rgb.save(os.path.join(OUT, name + '.png'))
    buf = io.BytesIO()
    rgb.save(buf, 'JPEG', quality=88, subsampling=0)
    return buf.getvalue()


# ---------------------------------------------------------------- ตัวเลขทอง / วินาที (PNG)
def glyph_set(chars, px, c1, c2, glow, pad=8, name='gold'):
    """เรนเดอร์ตัวอักษรทีละตัว ความสูงเท่ากันทุกตัว (baseline ตรงกัน)"""
    f = ImageFont.truetype(FBD, px * 4)
    asc, desc = f.getmetrics()
    out = {}
    for ch in chars:
        adv = int(f.getlength(ch))
        w, h = adv + pad * 2 * 4, asc + desc + pad * 2 * 4
        m = Image.new('L', (w, h), 0)
        ImageDraw.Draw(m).text((pad * 4, pad * 4), ch, font=f, fill=255)
        g = grad(w, h, c1, c2).convert('RGBA')
        col = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        col.paste(g, (0, 0), m)
        img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        if glow:
            gl = Image.new('RGBA', (w, h), glow + (0,))
            gl.putalpha(m.filter(ImageFilter.GaussianBlur(5 * 4)).point(lambda a: int(a * 0.75)))
            img = Image.alpha_composite(img, gl)
        img = Image.alpha_composite(img, col)
        img = img.resize((w // 4, h // 4), Image.LANCZOS)
        img.save(os.path.join(OUT, f'{name}_{ord(ch)}.png'))
        buf = io.BytesIO()
        img.save(buf, 'PNG', optimize=True)
        out[ch] = (buf.getvalue(), img.size[0], img.size[1], adv // 4, pad)
    out['__asc'] = asc // 4
    return out


# ---------------------------------------------------------------- ไอคอนอากาศ
ICON_KINDS = ['clear_day', 'clear_night', 'partly_day', 'partly_night', 'cloudy', 'fog', 'rain', 'storm', 'snow']


def moonicon(im, cx, cy, r):
    l = layer(im)
    d = ImageDraw.Draw(l)
    d.ellipse([(cx - r * 1.8) * K, (cy - r * 1.8) * K, (cx + r * 1.8) * K, (cy + r * 1.8) * K], fill=(200, 210, 255, 60))
    im = Image.alpha_composite(im, l.filter(ImageFilter.GaussianBlur(6 * K)))
    m = Image.new('L', im.size, 0)
    md = ImageDraw.Draw(m)
    md.ellipse([(cx - r) * K, (cy - r) * K, (cx + r) * K, (cy + r) * K], fill=255)
    md.ellipse([(cx - r * 0.35) * K, (cy - r * 1.25) * K, (cx + r * 1.45) * K, (cy + r * 0.55) * K], fill=0)
    g = Image.new('RGBA', im.size, (250, 238, 200, 255))
    L = layer(im)
    L.paste(g, (0, 0), m)
    return Image.alpha_composite(im, L)


def draw_icon(kind, box_w, box_h, scale):
    """วาดไอคอนบนผืนโปร่งใสขนาด box (หน่วยพิกเซลจอ)"""
    global W, H
    im = Image.new('RGBA', (box_w * K, box_h * K), (0, 0, 0, 0))
    cx, cy, s = box_w / 2, box_h / 2 + 2 * scale, scale * 0.8
    if kind == 'clear_day':
        im = sunicon(im, cx, cy, 19 * s)
    elif kind == 'clear_night':
        im = moonicon(im, cx, cy, 20 * s)
    elif kind in ('partly_day', 'partly_night'):
        im = sunicon(im, cx - 12 * s, cy - 9 * s, 12 * s) if kind == 'partly_day' else moonicon(im, cx - 12 * s, cy - 9 * s, 12 * s)
        im = cloudicon(im, cx + 6 * s, cy + 4 * s, 0.85 * s)
    elif kind == 'cloudy':
        im = cloudicon(im, cx - 8 * s, cy - 8 * s, 0.7 * s, dark=True)
        im = cloudicon(im, cx + 6 * s, cy + 2 * s, 0.9 * s)
    elif kind == 'fog':
        im = cloudicon(im, cx, cy - 10 * s, 0.8 * s)
        l = layer(im)
        d = ImageDraw.Draw(l)
        for i in range(3):
            y = cy + (10 + i * 7) * s
            d.rounded_rectangle([(cx - 26 * s) * K, (y - 1.5 * s) * K, (cx + 26 * s) * K, (y + 1.5 * s) * K], int(1.5 * s * K), fill=(200, 210, 230, 200))
        im = Image.alpha_composite(im, l)
    elif kind == 'rain':
        im = cloudicon(im, cx, cy - 8 * s, 0.9 * s, dark=True)
        im = rain(im, cx, cy + 10 * s, 0.9 * s, 3)
    elif kind == 'storm':
        im = cloudicon(im, cx, cy - 8 * s, 0.9 * s, dark=True)
        im = rain(im, cx - 6 * s, cy + 10 * s, 0.8 * s, 2)
        im = bolt(im, cx + 6 * s, cy + 2 * s, 0.75 * s)
    elif kind == 'snow':
        im = cloudicon(im, cx, cy - 8 * s, 0.9 * s)
        l = layer(im)
        d = ImageDraw.Draw(l)
        for i in range(3):
            x = cx - 12 * s + i * 12 * s
            y = cy + (14 + (i % 2) * 6) * s
            d.ellipse([(x - 2.5 * s) * K, (y - 2.5 * s) * K, (x + 2.5 * s) * K, (y + 2.5 * s) * K], fill=(240, 248, 255, 255))
        im = Image.alpha_composite(im, l)
    # ค่อย ๆ จางที่ขอบ ไม่ให้แสงเรืองถูกตัดเป็นขอบเหลี่ยม
    fade = Image.new('L', im.size, 0)
    fd = ImageDraw.Draw(fade)
    steps = 6 * K
    for i in range(steps):
        v = int(255 * (i + 1) / steps)
        fd.rounded_rectangle([i, i, im.size[0] - 1 - i, im.size[1] - 1 - i], radius=steps, outline=v)
    fd.rectangle([steps, steps, im.size[0] - 1 - steps, im.size[1] - 1 - steps], fill=255)
    a = im.getchannel('A')
    im.putalpha(ImageChops.multiply(a, fade))
    return im.resize((box_w, box_h), Image.LANCZOS)


# อาศัย helper ที่อ้าง W,H ของจอเต็ม: ไอคอนวาดบนผืนเล็ก จึงต้อง patch layer()/ขนาดให้ใช้ im.size
import gfx_helpers as _gh  # noqa


def icon_png(kind, w, h, s):
    img = draw_icon(kind, w, h, s)
    buf = io.BytesIO()
    img.save(buf, 'PNG', optimize=True)
    return buf.getvalue(), img


# ---------------------------------------------------------------- ฟอนต์ VLW
VLW_CHARS = [chr(c) for c in range(0x20, 0x7F)] + ['°']


def make_vlw(px, path=FBD):
    f = ImageFont.truetype(path, px)
    asc, desc = f.getmetrics()
    glyphs = []
    for ch in sorted(VLW_CHARS, key=ord):
        adv = int(round(f.getlength(ch)))
        bb = f.getbbox(ch, anchor='ls')
        if ch == ' ' or bb[2] <= bb[0]:
            glyphs.append((ord(ch), 0, 0, adv, 0, 0, b''))
            continue
        w, h = bb[2] - bb[0], bb[3] - bb[1]
        m = Image.new('L', (w, h), 0)
        ImageDraw.Draw(m).text((-bb[0], -bb[1]), ch, font=f, fill=255, anchor='ls')
        glyphs.append((ord(ch), h, w, adv, -bb[1], bb[0], m.tobytes()))
    out = bytearray(struct.pack('>6i', len(glyphs), 11, px, 0, asc, desc))
    for (u, h, w, adv, dy, dx, _) in glyphs:
        out += struct.pack('>7i', u, h, w, adv, dy, dx, 0)
    for g in glyphs:
        out += g[6]
    return bytes(out)


# ---------------------------------------------------------------- ข้อความไทย (alpha 4 บิต ลบขอบเรียบ)
def thai_alpha(text_, px, bold=True):
    f = ImageFont.truetype(FBD if bold else FR, px)
    asc, desc = f.getmetrics()
    w = int(f.getlength(text_)) + 2
    m = Image.new('L', (w, asc + desc), 0)
    ImageDraw.Draw(m).text((1, 0), text_, font=f, fill=255)
    return m


WEEKDAYS = ['อาทิตย์', 'จันทร์', 'อังคาร', 'พุธ', 'พฤหัสบดี', 'ศุกร์', 'เสาร์']
MONTHS = ['มกราคม', 'กุมภาพันธ์', 'มีนาคม', 'เมษายน', 'พฤษภาคม', 'มิถุนายน', 'กรกฎาคม', 'สิงหาคม', 'กันยายน',
          'ตุลาคม', 'พฤศจิกายน', 'ธันวาคม']
DESCS = [('CLEAR', 'ท้องฟ้าแจ่มใส'), ('MAINLY_CLEAR', 'มีเมฆเล็กน้อย'), ('PARTLY', 'มีเมฆบางส่วน'), ('OVERCAST', 'เมฆมาก'),
         ('FOG', 'มีหมอก'), ('DRIZZLE', 'ฝนปรอย'), ('FRZ_DRIZZLE', 'ฝนปรอยเยือกแข็ง'), ('RAIN_L', 'ฝนตกเล็กน้อย'),
         ('RAIN_M', 'ฝนตกปานกลาง'), ('RAIN_H', 'ฝนตกหนัก'), ('FRZ_RAIN', 'ฝนเยือกแข็ง'), ('SNOW', 'หิมะตก'),
         ('SHOWER_L', 'ฝนตกเป็นช่วง ๆ'), ('SHOWER_M', 'ฝนตกเป็นช่วง ๆ ปานกลาง'), ('SHOWER_H', 'ฝนตกหนักเป็นช่วง ๆ'),
         ('SNOW_SH', 'หิมะตกเป็นช่วง ๆ'), ('STORM', 'พายุฝนฟ้าคะนอง'), ('STORM_HAIL', 'พายุฝนฟ้าคะนองและลูกเห็บ'),
         ('UNKNOWN', 'ไม่ทราบสภาพอากาศ')]
TEXTS = []
for i, n in enumerate(WEEKDAYS):
    TEXTS.append((f'WDT_{i}', f'วัน{n}ที่', 13, True))
for i, n in enumerate(WEEKDAYS):
    TEXTS.append((f'WD_{i}', n, 11, True))
for i, n in enumerate(MONTHS):
    TEXTS.append((f'MON_{i}', n, 13, True))
for k, t in DESCS:
    TEXTS.append((f'D_{k}', t, 12, False))
TEXTS += [
    ('TODAY', 'วันนี้', 11, True),
    ('TOMORROW', 'พรุ่งนี้', 11, True),
    ('FEELS', 'รู้สึกเหมือน', 12, False),
    ('T_RAIN_NOW', 'ขณะนี้ฝนกำลังตก', 10, True),
    ('T_RAIN_AT', 'ฝนอาจตกช่วง', 10, True),
    ('T_CHANCE', 'โอกาส', 10, False),
    ('T_NO_RAIN', 'ไม่มีฝนใน 12 ชั่วโมงข้างหน้า', 10, True),
    ('T_HOT', 'อากาศร้อนจัด ดื่มน้ำบ่อย ๆ', 10, True),
    ('T_PM', 'ฝุ่นสูง ควรสวมหน้ากาก', 10, True),
    ('T_UPDATING', 'กำลังอัปเดตข้อมูล...', 10, True),
    ('CONNECTING', 'กำลังเชื่อมต่อ Wi-Fi...', 16, True),
    ('WIFI_FAILED', 'เชื่อมต่อ Wi-Fi ไม่สำเร็จ', 16, True),
    ('LOADING', 'กำลังโหลดข้อมูลอากาศ...', 16, True),
    ('SETUP_TITLE', 'ตั้งค่า Wi-Fi', 20, True),
    ('SETUP_A', 'ทางที่ 1: ที่หน้าเว็บติดตั้ง กด Connect to Wi-Fi', 12, False),
    ('SETUP_B', 'ทางที่ 2: ใช้มือถือเชื่อมต่อ Wi-Fi ชื่อ', 12, False),
    ('SETUP_C', 'แล้วเปิด', 12, False),
    ('NO_DATA', 'ยังไม่มีข้อมูล', 14, True),
    ('FW_UPDATING', 'กำลังอัปเดตโปรแกรมอัตโนมัติ', 16, True),
    ('CAL_TITLE', 'ปรับทัชสกรีน (ครั้งเดียว)', 16, True),
    ('CAL_TAP', 'แตะที่จุดสีทองให้ตรงที่สุด', 12, False),
    ('WF_TITLE', 'เลือก Wi-Fi บ้านของคุณ', 14, True),
    ('WF_SCANNING', 'กำลังค้นหา Wi-Fi...', 16, True),
    ('WF_NONE', 'ไม่พบ Wi-Fi แถวนี้ (ต้องเป็น 2.4 GHz)', 12, False),
    ('WF_RESCAN', 'ค้นหาใหม่', 11, True),
    ('WF_PASS', 'ใส่รหัสผ่าน Wi-Fi', 12, True),
    ('WF_CONNECT', 'เชื่อมต่อ', 11, True),
    ('WF_CANCEL', 'ย้อนกลับ', 11, True),
    ('WF_PHONE', 'หรือใช้มือถือต่อ Wi-Fi', 9, False),
    ('WF_OK', 'เชื่อมต่อสำเร็จ!', 18, True),
    ('WF_ERR_NOSSID', 'ไม่พบ Wi-Fi ชื่อนี้ (อยู่ไกลไป หรือเป็น 5 GHz)', 12, True),
    ('WF_ERR_PASS', 'รหัสผ่านไม่ถูกต้อง ลองใหม่อีกครั้ง', 12, True),
    ('WF_ERR_OTHER', 'เชื่อมต่อไม่สำเร็จ ลองใหม่อีกครั้ง', 12, True),
    ('WF_RETRYING', 'กำลังลองเชื่อมต่อ Wi-Fi เดิมอีกครั้ง...', 12, False),
    ('WDB_0', 'วันอาทิตย์', 15, True), ('WDB_1', 'วันจันทร์', 15, True), ('WDB_2', 'วันอังคาร', 15, True),
    ('WDB_3', 'วันพุธ', 15, True), ('WDB_4', 'วันพฤหัสบดี', 15, True), ('WDB_5', 'วันศุกร์', 15, True),
    ('WDB_6', 'วันเสาร์', 15, True),
    ('MONB_0', 'ม.ค.', 15, True), ('MONB_1', 'ก.พ.', 15, True), ('MONB_2', 'มี.ค.', 15, True), ('MONB_3', 'เม.ย.', 15, True),
    ('MONB_4', 'พ.ค.', 15, True), ('MONB_5', 'มิ.ย.', 15, True), ('MONB_6', 'ก.ค.', 15, True), ('MONB_7', 'ส.ค.', 15, True),
    ('MONB_8', 'ก.ย.', 15, True), ('MONB_9', 'ต.ค.', 15, True), ('MONB_10', 'พ.ย.', 15, True), ('MONB_11', 'ธ.ค.', 15, True),
    ('UPD', 'อัปเดต', 9, False), ('MIN_AGO', 'นาทีที่แล้ว', 9, False), ('JUST_NOW', 'อัปเดตเมื่อสักครู่', 9, False),
    ('S_HOTTEST', 'ร้อนสุด', 9, False), ('S_NORAIN', 'ไม่มีฝน', 18, True), ('S_RAINAT', 'ช่วง', 9, False),
    ('S_UVHIGH', 'แดดแรง', 9, False), ('S_UVOK', 'แดดไม่แรง', 9, False), ('S_SET', 'ตก', 9, False),
    ('AQS_0', 'ดีมาก', 10, True), ('AQS_1', 'ดี', 10, True), ('AQS_2', 'ปานกลาง', 10, True),
    ('AQS_3', 'เริ่มมีผล', 10, True), ('AQS_4', 'มีผลต่อสุขภาพ', 10, True),
    ('ADV_UMB', 'พกร่ม', 10, True), ('ADV_SUN', 'ทากันแดด', 10, True), ('ADV_MASK', 'ใส่หน้ากาก', 10, True),
    ('ADV_WATER', 'ดื่มน้ำบ่อย ๆ', 10, True), ('ADV_GOOD', 'อากาศดี ออกไปข้างนอกได้', 10, True),
    ('S_KMH', 'กม./ชม.', 9, False),
    ('R_MODEL', '(แบบจำลอง)', 9, False), ('R_RADAR', '(เรดาร์)', 9, False),
    ('R_STATUS', 'สถานะ', 9, False), ('R_HERE', 'ฝนตก', 13, True), ('R_HERE2', 'บริเวณนี้', 13, True),
    ('R_COMING', 'ฝนกำลังมา', 12, True), ('R_NEAR', 'มีฝนห่าง', 11, True), ('R_NONE', 'ไม่มีฝน', 13, True),
    ('R_NONE2', 'ในรัศมี 40 กม.', 9, False), ('R_KM', 'กม.', 9, False), ('R_MIN', 'นาที', 9, False),
    ('R_NOW', 'ตอนนี้', 8, False), ('R_OUTSIDE', 'แผนที่ฝนครอบคลุมเฉพาะกรุงเทพฯ และปริมณฑล', 9, False), ('R_ETA', 'ถึงในประมาณ', 9, False), ('R_NODATA', 'กำลังโหลดแผนที่...', 12, False),
    ('DIR_0', 'ทิศเหนือ', 9, False), ('DIR_1', 'ทิศ ตอ.เฉียงเหนือ', 9, False), ('DIR_2', 'ทิศตะวันออก', 9, False),
    ('DIR_3', 'ทิศ ตอ.เฉียงใต้', 9, False), ('DIR_4', 'ทิศใต้', 9, False), ('DIR_5', 'ทิศ ตต.เฉียงใต้', 9, False),
    ('DIR_6', 'ทิศตะวันตก', 9, False), ('DIR_7', 'ทิศ ตต.เฉียงเหนือ', 9, False),
    ('T_RADAR_A', 'กลุ่มฝนห่าง', 10, True), ('T_RADAR_B', 'กม. ถึงในประมาณ', 10, True), ('T_RADAR_C', 'นาที', 10, True),
    ('PRESET_Q', 'ภาพสว่างและชัดขึ้นไหม?', 16, True), ('PRESET_TAP', 'แตะจอ 1 ครั้ง = ใช้แบบนี้ต่อ', 12, False),
    ('PRESET_WAIT', 'ถ้าจอผิดปกติ ไม่ต้องทำอะไร ระบบจะกลับเป็นแบบเดิมเอง', 10, False), ('PRESET_NAME', 'โหมดภาพ', 16, True),
    ('SND_TITLE', 'ทดสอบลำโพง (ครั้งเดียว)', 16, True), ('SND_TAP', 'แตะจอทันทีที่ได้ยินเสียง', 12, True),
    ('SND_SET', 'ชุดที่', 12, False), ('SND_SKIP', 'ถ้าไม่ได้ยินเลย ระบบจะข้ามเอง', 10, False),
    ('SND_OK', 'ตั้งค่าเสียงเรียบร้อย', 16, True),
    ('FW_WAIT', 'ห้ามถอดสาย บอร์ดจะรีสตาร์ทเอง', 12, False),
]


def pack4(m):
    w, h = m.size
    px = list(m.tobytes())
    data = bytearray()
    for y in range(h):
        row = px[y * w:(y + 1) * w] + ([0] if w % 2 else [])
        for i in range(0, len(row), 2):
            data.append(((row[i] * 15 + 127) // 255) << 4 | ((row[i + 1] * 15 + 127) // 255))
    return bytes(data)


# ---------------------------------------------------------------- เขียน header
def carray(name, data):
    lines = [f'static const uint8_t {name}[] PROGMEM = {{']
    for i in range(0, len(data), 32):
        lines.append('  ' + ','.join(str(b) for b in data[i:i + 32]) + ',')
    lines.append('};')
    return '\n'.join(lines)


def main():
    parts = ['// สร้างอัตโนมัติโดย s3/tools/gen_assets.py — ห้ามแก้ด้วยมือ', '#pragma once', '#include <Arduino.h>', '',
             'struct ImgAsset { const uint8_t* data; uint32_t len; uint16_t w, h; };',
             'struct GlyphAsset { const uint8_t* data; uint32_t len; uint16_t w, h, adv, pad; };',
             'struct ThaiAsset { uint16_t w, h; const uint8_t* data; };', '']
    total = 0
    # backgrounds
    for name, fn in [('BG_HOME', bg_home), ('BG_SUM', bg_summary), ('BG_RADAR', bg_radar), ('BG_NOW', bg_now),
                     ('BG_FC', bg_fc), ('BG_PLAIN', bg_plain)]:
        j = to_jpeg(fn(), name.lower())
        total += len(j)
        parts.append(carray(name + '_JPG', j))
        parts.append(f'static const ImgAsset {name} = {{{name}_JPG, {len(j)}, 320, 240}};')
    # glyph sets
    sets = [('CLOCK', '0123456789:', 74, (255, 246, 210), (255, 160, 50), (255, 150, 60)),
            ('GOLD', '0123456789:°', 56, (255, 240, 190), (255, 165, 60), (255, 150, 60)),
            ('SEC', '0123456789:', 30, (210, 255, 255), (0, 200, 255), (0, 180, 255))]
    for name, chars, px, c1, c2, glow in sets:
        gs = glyph_set(chars, px, c1, c2, glow, name=name.lower())
        entries = []
        for ch in chars:
            data, w, h, adv, pad = gs[ch]
            total += len(data)
            cname = f'{name}_{ord(ch)}'
            parts.append(carray(cname, data))
            entries.append(f'  {{{cname}, {len(data)}, {w}, {h}, {adv}, {pad}}},')
        parts.append(f'// ลำดับ: "{chars}"')
        parts.append(f'static const GlyphAsset {name}_GLYPHS[] = {{\n' + '\n'.join(entries) + '\n};')
        parts.append(f'#define {name}_ASC {gs["__asc"]}')
    # icons
    sizes = [('L', 120, 96, 1.55), ('M', 56, 44, 0.7), ('S', 30, 24, 0.38)]
    for sz, w, h, s in sizes:
        entries = []
        for kind in ICON_KINDS:
            data, img = icon_png(kind, w, h, s)
            img.save(os.path.join(OUT, f'icon_{sz}_{kind}.png'))
            total += len(data)
            cname = f'ICON_{sz}_{kind.upper()}'
            parts.append(carray(cname, data))
            entries.append(f'  {{{cname}, {len(data)}, {w}, {h}}},')
        parts.append(f'static const ImgAsset ICONS_{sz}[] = {{\n' + '\n'.join(entries) + '\n};')
    parts.append('enum IconKind { ' + ', '.join('IC_' + k.upper() for k in ICON_KINDS) + ' };')
    # fonts
    for px in (9, 11, 14, 18, 24):
        v = make_vlw(px)
        total += len(v)
        parts.append(carray(f'VLW_{px}', v))
    # thai texts
    entries, enum = [], []
    for key, t, px, b in TEXTS:
        m = thai_alpha(t, px, b)
        data = pack4(m)
        total += len(data)
        parts.append(f'// {t}')
        parts.append(carray(f'TH_{key}_DATA', data))
        entries.append(f'  {{{m.size[0]}, {m.size[1]}, TH_{key}_DATA}},')
        enum.append(f'TH_{key}')
    parts.append('enum ThaiId { ' + ', '.join(enum) + ', TH_COUNT };')
    parts.append('static const ThaiAsset THAI[] = {\n' + '\n'.join(entries) + '\n};')
    with open(os.path.join(SKETCH, 'assets.h'), 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(parts) + '\n')
    print(f'assets: {total / 1024:.0f} KB')


if __name__ == '__main__':
    main()
