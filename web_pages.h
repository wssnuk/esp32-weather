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
<div class="card" id="alert" style="display:none;border:2px solid #4dc3ff">☔ <b id="alertText"></b></div>
<div class="card" id="aircard" style="display:none">
  <div class="muted">ฝุ่น PM2.5 (มคก./ลบ.ม.)</div>
  <div style="display:flex;align-items:baseline;gap:12px"><span id="pm25" style="font-size:44px;font-weight:700">-</span><b id="aqlevel"></b></div>
  <div class="muted" id="airmore"></div>
</div>
<div class="card" id="mkt" style="display:none"><div class="muted" style="margin-bottom:8px">ราคาทอง &amp; หุ้นไทย</div><div class="grid">
  <div class="item">ทองคำแท่ง รับซื้อ/ขายออก<b id="gbar">-</b></div>
  <div class="item">ทองรูปพรรณ รับซื้อ/ขายออก<b id="gorn">-</b></div>
  <div class="item">ดัชนี SET<b id="setv">-</b></div>
  <div class="item">SET50 · ดอลลาร์<b id="set50">-</b></div>
</div></div>
<div class="card"><div class="muted" style="margin-bottom:8px">พยากรณ์ 3 วัน</div><div class="grid" id="fc" style="grid-template-columns:1fr 1fr 1fr"></div></div>
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
    const AQC=['#0bb4e8','#22a83a','#d9a400','#e8590c','#c2255c'];
    if(w.air){$('aircard').style.display='';$('pm25').textContent=fx(w.air.pm25,0);$('pm25').style.color=AQC[w.air.level]||'';
      $('aqlevel').textContent=w.air.levelName;$('aqlevel').style.color=AQC[w.air.level]||'';
      $('airmore').textContent='PM10 '+fx(w.air.pm10,0)+' · AQI (US) '+w.air.aqi;}
    if(w.rainNext2h>=60){$('alert').style.display='';$('alertText').textContent='ฝนอาจตกภายใน 2 ชั่วโมง (โอกาส '+w.rainNext2h+'%)';}
    else $('alert').style.display='none';
    if(w.market){const m=w.market,n=(v,d=0)=>v==null?'-':Number(v).toLocaleString('th-TH',{minimumFractionDigits:d,maximumFractionDigits:d});
      $('mkt').style.display='';$('gbar').textContent=n(m.barBuy)+' / '+n(m.barSell);$('gorn').textContent=n(m.ornBuy)+' / '+n(m.ornSell);
      $('setv').textContent=n(m.set,2)+' ('+(m.setPct>0?'+':'')+n(m.setPct,2)+'%)';$('setv').style.color=m.setChange>0?'#1a9f3a':m.setChange<0?'#d63333':'';
      $('set50').textContent=n(m.set50,2)+' · '+n(m.usdthb,2)+' ฿';}
    const DN=['อา.','จ.','อ.','พ.','พฤ.','ศ.','ส.'];
    $('fc').innerHTML='';
    (w.forecast||[]).forEach((d,i)=>{const e=document.createElement('div');e.className='item';e.style.textAlign='center';
      const day=i==0?'พรุ่งนี้':DN[new Date(d.date+'T00:00').getDay()];
      e.innerHTML='<div></div><div style="font-size:30px"></div><b></b><div class="muted"></div>';
      e.children[0].textContent=day;e.children[1].textContent=d.icon;e.children[2].textContent=fx(d.max,0)+'° / '+fx(d.min,0)+'°';
      e.children[3].textContent='ฝน '+(d.rain<0?'-':d.rain+'%');$('fc').appendChild(e);});
    $('updated').textContent='ข้อมูล ณ '+w.time.replace('T',' ')+' ('+w.timezone+') · อัปเดตเมื่อ '+w.ageSec+' วินาทีที่แล้ว';
  }catch(e){$('desc').textContent='เชื่อมต่อบอร์ดไม่ได้';}
}
async function refreshNow(){ $('desc').textContent='กำลังอัปเดต...'; await fetch('/refresh',{method:'POST'}); setTimeout(load,4000); }
load(); setInterval(load,30000);
</script></body></html>)HTML";
