#!/usr/bin/env python3
"""สร้าง thai_text.h — ข้อความภาษาไทยแบบรูปภาพสำหรับแสดงบนจอ
จอ TFT ไม่มีฟอนต์ไทยและวาดสระ/วรรณยุกต์ซ้อนไม่ได้ จึงเรนเดอร์ไว้ล่วงหน้า (alpha 4 บิต)
ใช้: python3 tools/gen_thai_text.py   (ต้องมี Pillow ที่รองรับ raqm)
แก้ข้อความ: แก้ในรายการ TEXTS ด้านล่างแล้วรันใหม่
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = {"r": os.path.join(HERE, "fonts/NotoThai400.ttf"), "b": os.path.join(HERE, "fonts/NotoThai700.ttf")}

# (ชื่อ, ข้อความ, น้ำหนัก r/b, ขนาด px)
TEXTS = [
    # สภาพอากาศ (รหัส WMO)
    ("CLEAR", "ท้องฟ้าแจ่มใส", "b", 22),
    ("MAINLY_CLEAR", "มีเมฆเล็กน้อย", "b", 22),
    ("PARTLY_CLOUDY", "มีเมฆบางส่วน", "b", 22),
    ("OVERCAST", "เมฆมาก", "b", 22),
    ("FOG", "มีหมอก", "b", 22),
    ("DRIZZLE", "ฝนปรอย", "b", 22),
    ("FREEZING_DRIZZLE", "ฝนปรอยเยือกแข็ง", "b", 22),
    ("RAIN_LIGHT", "ฝนตกเล็กน้อย", "b", 22),
    ("RAIN_MODERATE", "ฝนตกปานกลาง", "b", 22),
    ("RAIN_HEAVY", "ฝนตกหนัก", "b", 22),
    ("FREEZING_RAIN", "ฝนเยือกแข็ง", "b", 22),
    ("SNOW", "หิมะตก", "b", 22),
    ("SHOWERS_LIGHT", "ฝนตกเป็นช่วง ๆ", "b", 22),
    ("SHOWERS_MODERATE", "ฝนตกเป็นช่วง ๆ ปานกลาง", "b", 22),
    ("SHOWERS_HEAVY", "ฝนตกหนักเป็นช่วง ๆ", "b", 22),
    ("SNOW_SHOWERS", "หิมะตกเป็นช่วง ๆ", "b", 22),
    ("THUNDERSTORM", "พายุฝนฟ้าคะนอง", "b", 22),
    ("THUNDERSTORM_HAIL", "พายุฝนฟ้าคะนองและลูกเห็บ", "b", 22),
    ("UNKNOWN", "ไม่ทราบสภาพอากาศ", "b", 22),
    # ป้ายกำกับ
    ("FEELS_LIKE", "รู้สึกเหมือน", "b", 16),
    ("LBL_HUMIDITY", "ความชื้น %", "b", 15),
    ("LBL_WIND", "ลม กม./ชม.", "b", 15),
    ("LBL_RAIN", "โอกาสฝน %", "b", 15),
    ("LBL_HILO", "สูง/ต่ำ °C", "b", 15),
    ("LBL_UV", "ดัชนี UV", "b", 15),
    ("LBL_SUN", "ขึ้น/ตก", "b", 15),
    # สถานะ
    ("BOOT_HINT", "สีจอเพี้ยน? แตะจอค้าง 5 วินาที เพื่อเปลี่ยนโหมดจอ", "b", 14),
    ("CONNECTING", "กำลังเชื่อมต่อ Wi-Fi...", "b", 18),
    ("WIFI_FAILED", "เชื่อมต่อ Wi-Fi ไม่สำเร็จ", "b", 18),
    ("LOADING", "กำลังโหลดข้อมูลอากาศ...", "b", 18),
    ("UPDATING", "กำลังอัปเดต...", "b", 14),
    ("FETCH_FAILED", "ดึงข้อมูลไม่สำเร็จ จะลองใหม่อัตโนมัติ", "b", 14),
    ("TAP_HINT", "แตะจอเพื่อเปลี่ยนหน้า", "b", 14),
    ("DISPLAY_MODE", "โหมดจอ", "b", 22),
    ("DISPLAY_MODE_HINT", "แตะจอค้างอีกครั้งถ้าสียังเพี้ยน", "b", 15),
    # หน้าคุณภาพอากาศ
    ("LBL_PM25", "ฝุ่น PM2.5", "b", 18),
    ("LBL_PM10", "ฝุ่น PM10", "b", 15),
    ("LBL_PM25_TILE", "ฝุ่น PM2.5", "b", 15),
    ("LBL_AQI", "ดัชนี AQI", "b", 15),
    ("UNIT_UGM3", "มคก./ลบ.ม.", "b", 14),
    ("AQ_0", "ดีมาก", "b", 22),
    ("AQ_1", "ดี", "b", 22),
    ("AQ_2", "ปานกลาง", "b", 22),
    ("AQ_3", "เริ่มมีผลต่อสุขภาพ", "b", 18),
    ("AQ_4", "มีผลต่อสุขภาพ", "b", 18),
    ("AQ_TIP_0", "อากาศดีมาก เหมาะกับกิจกรรมกลางแจ้ง", "b", 16),
    ("AQ_TIP_1", "ทำกิจกรรมกลางแจ้งได้ตามปกติ", "b", 16),
    ("AQ_TIP_2", "กลุ่มเสี่ยงควรลดกิจกรรมกลางแจ้ง", "b", 16),
    ("AQ_TIP_3", "ควรสวมหน้ากากเมื่อออกนอกอาคาร", "b", 16),
    ("AQ_TIP_4", "ควรงดกิจกรรมกลางแจ้ง สวมหน้ากาก N95", "b", 16),
    ("AQ_LEVEL", "ระดับคุณภาพอากาศ", "b", 15),
    # หน้าพยากรณ์
    ("DAY_TOMORROW", "พรุ่งนี้", "b", 18),
    ("DAY_0", "อาทิตย์", "b", 18),
    ("DAY_1", "จันทร์", "b", 18),
    ("DAY_2", "อังคาร", "b", 18),
    ("DAY_3", "พุธ", "b", 18),
    ("DAY_4", "พฤหัสบดี", "b", 18),
    ("DAY_5", "ศุกร์", "b", 18),
    ("DAY_6", "เสาร์", "b", 18),
    ("LBL_HILO_SHORT", "สูง/ต่ำ", "b", 15),
    ("LBL_RAIN_SHORT", "โอกาสฝน", "b", 15),
    # เตือนฝน
    ("RAIN_SOON", "ฝนอาจตกภายใน 2 ชั่วโมง", "b", 22),
    # หน้าตั้งค่า
    ("SETUP_TITLE", "ตั้งค่า Wi-Fi ครั้งแรก", "b", 22),
    ("SETUP_1", "1. ใช้มือถือเชื่อมต่อ Wi-Fi ชื่อ", "b", 16),
    ("SETUP_2", "2. หน้าตั้งค่าจะเด้งขึ้นมาเอง หรือเปิด", "b", 16),
    ("SETUP_3", "3. เลือก Wi-Fi บ้าน ใส่รหัส แล้วกดบันทึก", "b", 16),
    ("SETUP_NOTE", "ใช้ได้เฉพาะ Wi-Fi 2.4 GHz", "b", 14),
]

def render_raw(text, weight, size):
    font = ImageFont.truetype(FONTS[weight], size)
    ascent, descent = font.getmetrics()
    im = Image.new("L", (int(font.getlength(text)) + 2, ascent + descent), 0)
    ImageDraw.Draw(im).text((1, 0), text, font=font, fill=255)
        # ขาว/ดำล้วน (ไม่มีขอบจาง) — ขอบจางจะกลายเป็นสีเพี้ยนบนจอบางล็อต
    return im.point(lambda a: 255 if a >= 110 else 0)


def render_all():
    """เรนเดอร์ทุกข้อความ แล้วตัดขอบบน/ล่างเท่ากันในกลุ่มฟอนต์เดียวกัน (baseline ตรงกัน)"""
    raw = {n: render_raw(t, w, sz) for n, t, w, sz in TEXTS}
    groups = {}
    for n, _, w, sz in TEXTS:
        groups.setdefault((w, sz), []).append(n)
    out = {}
    for names in groups.values():
        boxes = [raw[n].getbbox() or (0, 0, 1, 1) for n in names]
        top = min(b[1] for b in boxes)
        bottom = max(b[3] for b in boxes)
        for n in names:
            im = raw[n]
            out[n] = im.crop((0, top, im.width, bottom))
    return out


def main():
    out = ['// สร้างอัตโนมัติโดย tools/gen_thai_text.py — ห้ามแก้ไฟล์นี้ด้วยมือ',
           '#pragma once', '#include <Arduino.h>', '',
           'struct ThaiText { uint16_t w, h; const uint8_t* data; };  // alpha 4 บิต, 2 พิกเซล/ไบต์', '']
    names, total = [], 0
    images = render_all()
    for name, text, weight, size in TEXTS:
        im = images[name]
        w, h = im.size
        px = list(im.tobytes())
        packed = bytearray()
        for y in range(h):
            row = px[y * w:(y + 1) * w] + ([0] if w % 2 else [])
            for i in range(0, len(row), 2):
                packed.append(((row[i] * 15 + 127) // 255) << 4 | ((row[i + 1] * 15 + 127) // 255))
        total += len(packed)
        out.append(f'// {text}')
        out.append(f'static const uint8_t TT_{name}_DATA[] PROGMEM = {{')
        for i in range(0, len(packed), 24):
            out.append('  ' + ','.join(str(b) for b in packed[i:i + 24]) + ',')
        out.append('};')
        names.append((name, w, h))
    out.append('')
    out.append('enum ThaiTextId {')
    out += [f'  TT_{n},' for n, _, _ in names]
    out.append('  TT_COUNT\n};')
    out.append('')
    out.append('static const ThaiText THAI_TEXTS[] = {')
    out += [f'  {{{w}, {h}, TT_{n}_DATA}},' for n, w, h in names]
    out.append('};')
    path = os.path.join(HERE, '..', 'thai_text.h')
    with open(path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out) + '\n')
    print(f'wrote {len(names)} texts, {total} bytes -> {os.path.normpath(path)}')

if __name__ == '__main__':
    main()
