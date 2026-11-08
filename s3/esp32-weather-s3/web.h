#pragma once

namespace Web {
void beginPortal();     // โหมดตั้งค่าครั้งแรก: ปล่อย Wi-Fi + หน้าเว็บกรอกรหัส Wi-Fi
void beginDashboard();  // โหมดปกติ: หน้าเว็บแสดงสภาพอากาศ
void loop();
}
