"""ภาพตัวอย่างกรอบ (ใช้ matplotlib — ไม่ต้องมีการ์ดจอ)"""
import os, numpy as np, trimesh, matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
HERE = os.path.dirname(os.path.abspath(__file__))
def show(ax, path, color, off=(0, 0, 0), title=''):
    m = trimesh.load(os.path.join(HERE, path))
    m.apply_translation(off)
    v = m.vertices[m.faces]
    n = m.face_normals
    light = np.clip(n @ np.array([0.3, -0.5, 0.8]) / np.linalg.norm([0.3, -0.5, 0.8]), 0, 1) * 0.7 + 0.3
    cols = np.array(color)[None, :] * light[:, None]
    ax.add_collection3d(Poly3DCollection(v, facecolors=np.c_[cols, np.ones(len(cols))], edgecolor='none'))
    return m.bounds
fig = plt.figure(figsize=(14, 6), dpi=110)
views = [('front_bezel.stl', (0.85, 0.65, 0.25), 'ฝาหน้า (Front bezel)', (35, -60)),
         ('back_cover.stl', (0.3, 0.55, 0.85), 'ฝาหลัง (Back cover)', (-35, -120)),
         ('desk_stand.stl', (0.5, 0.5, 0.55), 'ขาตั้ง (Stand)', (25, -50))]
for i, (p, c, t, (el, az)) in enumerate(views):
    ax = fig.add_subplot(1, 3, i + 1, projection='3d')
    b = show(ax, p, c)
    mid = (b[0] + b[1]) / 2; r = (b[1] - b[0]).max() / 2
    ax.set_xlim(mid[0] - r, mid[0] + r); ax.set_ylim(mid[1] - r, mid[1] + r); ax.set_zlim(mid[2] - r, mid[2] + r)
    ax.view_init(el, az); ax.set_axis_off(); ax.set_title(p)
plt.tight_layout()
plt.savefig(os.path.join(HERE, 'case_preview.png'))
