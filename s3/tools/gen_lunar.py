#!/usr/bin/env python3
"""สร้างตารางปฏิทินจันทรคติไทย (วันขึ้น 1 ค่ำของทุกเดือน 2024-12 ถึง 2040) → esp32-weather-s3/lunar.h
ใช้ไลบรารี pythaidate (pip install pythaidate)"""
import datetime, os, re
from pythaidate import CsDate, julianday
ANIMALS = ['ชวด', 'ฉลู', 'ขาล', 'เถาะ', 'มะโรง', 'มะเส็ง', 'มะเมีย', 'มะแม', 'วอก', 'ระกา', 'จอ', 'กุน']
BASE = datetime.date(2024, 12, 1)
rows = []
for i in range(0, (datetime.date(2041, 1, 1) - BASE).days):
    d = BASE + datetime.timedelta(i)
    c = CsDate.fromjulianday(julianday.to_julianday(d.year, d.month, d.day))
    if c.day == 1 or i == 0:
        animal = re.search(r'ปี(\S+)', c.csformat()).group(1)
        rows.append((i - (c.day - 1), c.month, ANIMALS.index(animal)))
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'esp32-weather-s3', 'lunar.h')
with open(out, 'w') as f:
    f.write('// สร้างอัตโนมัติโดย s3/tools/gen_lunar.py — ห้ามแก้ด้วยมือ\n#pragma once\n#include <Arduino.h>\n\n')
    f.write('// วันขึ้น 1 ค่ำของแต่ละเดือน: จำนวนวันนับจาก 1 ธ.ค. 2024, เดือน (88 = เดือน 8 หลัง), ปีนักษัตร\n')
    f.write('struct LunarMonth { int16_t start; uint8_t month, animal; };\n')
    f.write('static const LunarMonth LUNAR[] = {\n')
    for r in rows:
        f.write(f'  {{{r[0]}, {r[1]}, {r[2]}}},\n')
    f.write('};\nconstexpr int LUNAR_N = %d;\n' % len(rows))
    f.write('static const char* const LUNAR_ANIMAL[] = {' + ', '.join(f'"{a}"' for a in ANIMALS) + '};\n')
print(len(rows), 'months ->', out)
