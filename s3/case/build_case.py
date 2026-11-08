#!/usr/bin/env python3
"""กรอบพิมพ์ 3 มิติ สำหรับบอร์ด ESP32-S3 2.8" (LCDwiki/QDtech ES3C28P)

ขนาดอ้างอิงจากแบบของผู้ผลิต (ES3C28P_Size.pdf, V1.0 2025-06-11):
  PCB 50.00 x 86.00 มม. หนา 1.60, มุมโค้ง R3.5
  ด้านหน้า (กระจกสัมผัส+จอ) สูงจากผิว PCB 4.30 มม. (รวมทั้งบอร์ด 5.90), ด้านหลังอุปกรณ์สูงสุด 4.70 มม.
  กระจก/ทัช 50.00 x 69.20 เริ่มที่ 8.22 มม. จากขอบบน
  พื้นที่แสดงผล (VA) 43.60 x 58.05 เริ่มที่ x 3.20 / y 10.96
  รูยึด 4 รู Ø3.20 ห่างขอบ 4.00 มม. (ระยะรู 42.00 x 78.00)
  ขอบล่าง: USB-C ตรงกลาง, ปุ่ม BOOT/RESET อยู่ด้านหลังห่างขอบ 3.26 มม.

พิกัดในไฟล์นี้: มองจากด้านหน้า x = ซ้าย→ขวา (0..50), y = บน→ล่าง (0..86)
z = 0 ที่ผิวหลังของ PCB, ด้านหน้า (จอ) เป็น z บวก

ผลลัพธ์: front_bezel.stl (ฝาหน้า พิมพ์คว่ำหน้า), back_cover.stl (ฝาหลัง), desk_stand.stl (ขาตั้งโต๊ะ)
สกรู: M3 x 20 มม. 4 ตัว (หรือสกรูเกลียวปล่อย M3) ขันจากด้านหลังทะลุ PCB เข้าเสาในฝาหน้า
"""
import os
import sys
import numpy as np
import trimesh
from manifold3d import Manifold, CrossSection, JoinType

HERE = os.path.dirname(os.path.abspath(__file__))

# ---------------- ขนาดบอร์ด (มม.) ----------------
PCB_W, PCB_H, PCB_T, PCB_R = 50.0, 86.0, 1.6, 3.5
FRONT_STACK = 4.3            # กระจก+จอ เหนือผิว PCB
BACK_PARTS = 4.7             # อุปกรณ์ด้านหลังสูงสุด
GLASS_Y0, GLASS_H = 8.22, 69.2
VA_X, VA_Y, VA_W, VA_H = 3.2, 10.96, 43.6, 58.05
HOLES = [(4.0, 4.0), (46.0, 4.0), (4.0, 82.0), (46.0, 82.0)]
USB_X = 26.0                 # กึ่งกลาง USB-C (มองจากด้านหน้า)
BTN = [(13.44, 82.74, 'BOOT'), (38.44, 82.74, 'RESET')]  # ตำแหน่งปุ่ม (มองจากด้านหน้า)
SD_Y0, SD_Y1 = 28.0, 44.0    # ช่องการ์ด SD ขอบซ้าย (มองจากด้านหน้า)
MIC = (38.5, 3.6)

# ---------------- ค่ากรอบ ----------------
CLR = 0.3                    # ระยะเผื่อรอบบอร์ด
WALL = 2.0
PLATE = 1.6                  # ความหนาแผ่นหน้า
BACK_DEPTH = 12.0            # ช่องว่างด้านหลัง (อุปกรณ์ + สายลำโพง/แบต + ลำโพงเล็ก)
BACK_PLATE = 2.0
SPLIT_Z = -2.0               # รอยต่อฝาหน้า/ฝาหลัง
LIP = 1.6

IN_W, IN_H = PCB_W + 2 * CLR, PCB_H + 2 * CLR
OUT_W, OUT_H = IN_W + 2 * WALL, IN_H + 2 * WALL
FRONT_Z = PCB_T + FRONT_STACK          # ผิวกระจก (z = 5.9)
TOP_Z = FRONT_Z + PLATE                # ผิวหน้ากรอบ
BOTTOM_Z = -BACK_DEPTH - BACK_PLATE    # ผิวหลังกรอบ
SEG = 64


def rrect(w, h, r, x0=0.0, y0=0.0):
    """สี่เหลี่ยมมุมมน (2D) มุมซ้ายบนที่ (x0, y0)"""
    r = max(0.01, min(r, w / 2, h / 2))
    return CrossSection.square([w - 2 * r, h - 2 * r]).translate([x0 + r, y0 + r]).offset(r, JoinType.Round, circular_segments=SEG)


def slab(cs, z0, z1):
    return Manifold.extrude(cs, z1 - z0).translate([0, 0, z0])


def box(x0, y0, z0, x1, y1, z1):
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


def cyl(x, y, z0, z1, r, r2=None):
    return Manifold.cylinder(z1 - z0, r, r if r2 is None else r2, SEG).translate([x, y, z0])


OX, OY = -CLR - WALL, -CLR - WALL  # มุมนอกซ้ายบน
OUTER = rrect(OUT_W, OUT_H, PCB_R + CLR + WALL, OX, OY)
INNER = rrect(IN_W, IN_H, PCB_R + CLR, -CLR, -CLR)


def front_bezel():
    body = slab(OUTER, SPLIT_Z, TOP_Z) - slab(INNER, SPLIT_Z - 1, FRONT_Z)
    # หน้าต่างจอ (เผื่อ 0.4 มม.) ลบมุมเอียงด้านนอกให้มองมุมเฉียงได้
    m = 0.4
    win = slab(rrect(VA_W + 2 * m, VA_H + 2 * m, 1.0, VA_X - m, VA_Y - m), FRONT_Z - 0.1, TOP_Z + 0.1)
    bevel = Manifold.hull(slab(rrect(VA_W + 2 * m, VA_H + 2 * m, 1.0, VA_X - m, VA_Y - m), FRONT_Z + 0.5, FRONT_Z + 0.51) +
                          slab(rrect(VA_W + 2 * m + 2.4, VA_H + 2 * m + 2.4, 2.2, VA_X - m - 1.2, VA_Y - m - 1.2), TOP_Z + 0.05, TOP_Z + 0.06))
    body = body - win - bevel
    # รูไมโครโฟน
    body = body - cyl(*MIC, FRONT_Z - 1, TOP_Z + 1, 0.8)
    # เสายึดสกรู (กดบนผิว PCB ด้านหน้า นอกพื้นที่กระจก)
    for (x, y) in HOLES:
        post = cyl(x, y, PCB_T, FRONT_Z + 0.1, 2.6)
        body = body + post - cyl(x, y, PCB_T - 0.1, FRONT_Z, 1.25)  # รูนำ M3 เกลียวปล่อย
    # ช่อง USB-C และช่องการ์ด SD (ส่วนบนของช่อง)
    body = body - box(USB_X - 6.5, PCB_H - 2, SPLIT_Z - 1, USB_X + 6.5, PCB_H + CLR + WALL + 1, 1.2)
    body = body - box(OX - 1, SD_Y0, SPLIT_Z - 1, -CLR + 0.5, SD_Y1, 0.6)
    return body


def back_cover():
    shell = slab(OUTER, BOTTOM_Z, SPLIT_Z) - slab(INNER, BOTTOM_Z + BACK_PLATE, SPLIT_Z + 1)
    # ขอบสวมเข้าไปในฝาหน้า
    lip = slab(rrect(IN_W - 0.3, IN_H - 0.3, PCB_R + CLR, -CLR + 0.15, -CLR + 0.15), SPLIT_Z, SPLIT_Z + LIP) - \
        slab(rrect(IN_W - 0.3 - 2 * 1.2, IN_H - 0.3 - 2 * 1.2, PCB_R, -CLR + 1.35, -CLR + 1.35), SPLIT_Z - 1, SPLIT_Z + LIP + 1)
    body = shell + lip
    # เสารองรับใต้ PCB + รูสกรูทะลุ + เคาเตอร์บอร์หัวสกรู
    for (x, y) in HOLES:
        body = body + cyl(x, y, BOTTOM_Z + BACK_PLATE - 0.1, 0.0, 2.8)
        body = body - cyl(x, y, BOTTOM_Z - 1, 1.0, 1.7) - cyl(x, y, BOTTOM_Z - 1, BOTTOM_Z + 2.6, 3.1)
    # ช่อง USB-C / SD (ส่วนล่างของช่อง)
    body = body - box(USB_X - 6.5, PCB_H - 2, -5.5, USB_X + 6.5, PCB_H + CLR + WALL + 1, SPLIT_Z + LIP + 1)
    body = body - box(OX - 1, SD_Y0, -3.5, -CLR + 0.5, SD_Y1, SPLIT_Z + LIP + 1)
    # รูกดปุ่ม BOOT / RESET (ใช้ไม้จิ้มฟัน/คลิปกด)
    for (x, y, _) in BTN:
        body = body - cyl(x, y, BOTTOM_Z - 1, BOTTOM_Z + BACK_PLATE + 1, 1.6)
    # ตะแกรงลำโพง (กลางฝาหลัง)
    cx, cy = PCB_W / 2, 50.0
    for i in range(-3, 4):
        for j in range(-3, 4):
            if i * i + j * j <= 10:
                body = body - cyl(cx + i * 3.2, cy + j * 3.2, BOTTOM_Z - 1, BOTTOM_Z + BACK_PLATE + 1, 0.9)
    # ช่องระบายอากาศด้านบน
    for k in range(5):
        x = 13 + k * 6
        body = body - box(x, 14, BOTTOM_Z - 1, x + 2.2, 24, BOTTOM_Z + BACK_PLATE + 1)
    return body


def desk_stand():
    """ขาตั้งโต๊ะ: วางกรอบ (แนวนอนหรือแนวตั้ง) เอนไปด้านหลัง 20° ขอบหน้ากันลื่น"""
    import math
    d = TOP_Z - BOTTOM_Z + 1.0              # ความหนากรอบ + เผื่อ
    t = math.tan(math.radians(20))
    x0 = 5 + d / math.cos(math.radians(20))  # จุดเริ่มพนักพิง
    prof = [(0, 0), (64, 0), (64, 4), (54, 4), (x0 + 30 * t + 4, 34), (x0 + 30 * t, 34), (x0, 4), (5, 4), (5, 10), (0, 10)]
    width = 50.0
    body = Manifold.extrude(CrossSection([prof]), width).rotate([90, 0, 0]).translate([0, width, 0])
    # ลดเนื้อพลาสติกตรงกลางพนักพิง
    body = body - box(x0 + 6, 12, 8, 64, width - 12, 40)
    return body


def save(m, name):
    mesh = m.to_mesh()
    t = trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3], faces=np.asarray(mesh.tri_verts))
    path = os.path.join(HERE, name)
    t.export(path)
    ext = t.bounds[1] - t.bounds[0]
    print(f'{name}: {ext[0]:.1f} x {ext[1]:.1f} x {ext[2]:.1f} mm, {len(t.faces)} faces, watertight={t.is_watertight}')
    return t


if __name__ == '__main__':
    parts = {'front_bezel.stl': front_bezel(), 'back_cover.stl': back_cover(), 'desk_stand.stl': desk_stand()}
    meshes = {n: save(m, n) for n, m in parts.items()}
    print(f'outer size: {OUT_W:.1f} x {OUT_H:.1f} x {TOP_Z - BOTTOM_Z:.1f} mm')
