#pragma once
#include <Arduino.h>

static const char PAGE_STYLE[] PROGMEM = R"CSS(
:root{--bg:#eef3f9;--card:#fff;--text:#1d2533;--muted:#6b7686;--accent:#2563eb}
@media (prefers-color-scheme:dark){:root{--bg:#0f1520;--card:#1a2230;--text:#e6ebf2;--muted:#94a0b3;--accent:#60a5fa}}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,-apple-system,"Noto Sans Thai",sans-serif;background:var(--bg);color:var(--text)}
main{max-width:520px;margin:0 auto;padding:20px 16px}
h1{font-size:20px;margin:0 0 12px}
.card{background:var(--card);border-radius:16px;padding:18px;margin-bottom:14px;box-shadow:0 1px 4px #0002}
.muted{color:var(--muted);font-size:14px}
.hero{text-align:center}.hero .ic{font-size:64px;line-height:1.1}.hero .t{font-size:56px;font-weight:700}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.item{background:var(--bg);border-radius:12px;padding:10px 12px}.item b{display:block;font-size:18px;margin-top:2px}
input,select,button{width:100%;font-size:16px;padding:12px;border-radius:10px;border:1px solid #8884;margin:6px 0 12px;background:var(--card);color:var(--text)}
button{background:var(--accent);color:#fff;border:0;font-weight:600}
button.sec{background:transparent;color:var(--accent);border:1px solid var(--accent)}
a{color:var(--accent)}
)CSS";

static const char PAGE_PORTAL[] PROGMEM = R"HTML(<!doctype html><html lang="th"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>ตั้งค่า ESP32 Weather</title>
<link rel="stylesheet" href="/style.css"></head><body><main>
<h1>🌤️ ตั้งค่า ESP32 Weather</h1>
<form class="card" method="post" action="/save">
<label>เลือก Wi-Fi บ้านคุณ</label>
<select id="list" onchange="document.getElementById('ssid').value=this.value"><option value="">กำลังค้นหา...</option></select>
<label>ชื่อ Wi-Fi (SSID)</label><input id="ssid" name="ssid" required>
<label>รหัสผ่าน Wi-Fi</label><input name="pass" type="password">
<label>เมืองที่ต้องการดูสภาพอากาศ</label><input name="city" placeholder="เช่น Bangkok, Chiang Mai, ขอนแก่น" value="%CITY%">
<button>บันทึกและเชื่อมต่อ</button>
<p class="muted">รองรับเฉพาะ Wi-Fi 2.4 GHz (ESP32 ใช้ 5 GHz ไม่ได้)</p>
</form></main>
<script>
fetch('/scan').then(r=>r.json()).then(l=>{const s=document.getElementById('list');
s.innerHTML='<option value="">-- เลือก --</option>';
l.forEach(n=>{const o=document.createElement('option');o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+' dBm)';s.appendChild(o);});});
</script></body></html>)HTML";

static const char PAGE_DASHBOARD[] PROGMEM = R"HTML(<!doctype html><html lang="th"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>ESP32 Weather</title>
<link rel="stylesheet" href="/style.css"></head><body><main>
<div class="card hero">
  <div class="muted" id="city">-</div>
  <div class="ic" id="icon">⏳</div>
  <div class="t" id="temp">--</div>
  <div id="desc">กำลังโหลดข้อมูล...</div>
  <div class="muted" id="range"></div>
</div>
<div class="card grid">
  <div class="item">รู้สึกเหมือน<b id="feels">-</b></div>
  <div class="item">ความชื้น<b id="hum">-</b></div>
  <div class="item">ลม<b id="wind">-</b></div>
  <div class="item">ความกดอากาศ<b id="pres">-</b></div>
  <div class="item">ปริมาณฝนตอนนี้<b id="rain">-</b></div>
  <div class="item">โอกาสฝนวันนี้<b id="chance">-</b></div>
  <div class="item">เมฆปกคลุม<b id="cloud">-</b></div>
  <div class="item">UV สูงสุดวันนี้<b id="uv">-</b></div>
  <div class="item">พระอาทิตย์ขึ้น<b id="rise">-</b></div>
  <div class="item">พระอาทิตย์ตก<b id="set">-</b></div>
</div>
<div class="card">
  <div class="muted" id="updated">-</div>
  <button class="sec" onclick="refreshNow()">🔄 อัปเดตเดี๋ยวนี้</button>
  <form method="post" action="/city"><label>เปลี่ยนเมือง</label>
  <input name="city" placeholder="เช่น Chiang Mai, ภูเก็ต" required><button>เปลี่ยนเมือง</button></form>
  <div class="muted" id="dev"></div>
  <p class="muted"><a href="/wifi-reset" onclick="return confirm('ล้างการตั้งค่า Wi-Fi และเข้าโหมดตั้งค่าใหม่?')">ตั้งค่า Wi-Fi ใหม่</a></p>
</div></main>
<script>
const $=id=>document.getElementById(id), fx=(v,d=1)=>v==null?'-':Number(v).toFixed(d);
async function load(){
  try{
    const w=await (await fetch('/api/weather')).json();
    $('city').textContent='📍 '+w.city;
    $('dev').textContent='อุปกรณ์: '+w.device.ip+' · Wi-Fi '+w.device.rssi+' dBm · ทำงานมา '+Math.floor(w.device.uptime/60)+' นาที';
    if(!w.valid){$('desc').textContent=w.error?('ดึงข้อมูลไม่สำเร็จ: '+w.error):'กำลังโหลดข้อมูล...';return;}
    $('icon').textContent=w.icon; $('temp').textContent=fx(w.temp)+'°';
    $('desc').textContent=w.description;
    $('range').textContent='สูงสุด '+fx(w.tempMax,0)+'° / ต่ำสุด '+fx(w.tempMin,0)+'°';
    $('feels').textContent=fx(w.feelsLike)+' °C'; $('hum').textContent=w.humidity+' %';
    $('wind').textContent=fx(w.windSpeed)+' กม./ชม. ('+w.windFrom+')';
    $('pres').textContent=fx(w.pressure,0)+' hPa'; $('rain').textContent=fx(w.precipitation)+' มม.';
    $('chance').textContent=(w.rainChance<0?'-':w.rainChance+' %'); $('cloud').textContent=w.cloudCover+' %';
    $('uv').textContent=fx(w.uvMax); $('rise').textContent=w.sunrise; $('set').textContent=w.sunset;
    $('updated').textContent='ข้อมูล ณ '+w.time.replace('T',' ')+' ('+w.timezone+') · อัปเดตเมื่อ '+w.ageSec+' วินาทีที่แล้ว';
  }catch(e){$('desc').textContent='เชื่อมต่อบอร์ดไม่ได้';}
}
async function refreshNow(){ $('desc').textContent='กำลังอัปเดต...'; await fetch('/refresh',{method:'POST'}); setTimeout(load,4000); }
load(); setInterval(load,30000);
</script></body></html>)HTML";
