// ESP32 Weather — ข้อมูลตลาดแบบย่อสำหรับบอร์ด ESP32
// GET /api/market -> ราคาทองไทย (สมาคมค้าทองคำ), ดัชนี SET / SET50, ทองคำโลก, USD/THB
// ทุกข้อความเป็นภาษาอังกฤษ/ตัวเลข เพราะจอบอร์ดแสดงภาษาไทยแบบไดนามิกไม่ได้

const UA = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Safari/537.36';
const TZ_OFFSET = 7 * 3600; // Asia/Bangkok

async function getText(url, timeoutMs = 8000) {
  const ctrl = new AbortController();
  const t = setTimeout(() => ctrl.abort(), timeoutMs);
  try {
    const r = await fetch(url, { headers: { 'User-Agent': UA, 'Accept-Language': 'th,en;q=0.8' }, signal: ctrl.signal });
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    return await r.text();
  } finally {
    clearTimeout(t);
  }
}

const num = (s) => {
  if (s == null) return null;
  const v = parseFloat(String(s).replace(/,/g, '').trim());
  return Number.isFinite(v) ? v : null;
};
const r2 = (v) => (v == null ? null : Math.round(v * 100) / 100);

// ---------- Yahoo Finance (ดัชนี SET, SET50, ทองโลก, ค่าเงิน) ----------
async function yahoo(symbol, range = '1d', interval = '5m') {
  const url = `https://query1.finance.yahoo.com/v8/finance/chart/${encodeURIComponent(symbol)}?range=${range}&interval=${interval}&includePrePost=false`;
  const j = JSON.parse(await getText(url));
  const res = j?.chart?.result?.[0];
  if (!res) throw new Error(j?.chart?.error?.description || 'no data');
  const m = res.meta;
  const closes = (res.indicators?.quote?.[0]?.close || []).filter((v) => v != null);
  const price = m.regularMarketPrice ?? closes[closes.length - 1];
  const prev = m.chartPreviousClose ?? m.previousClose;
  return {
    price: r2(price),
    prev: r2(prev),
    change: r2(price - prev),
    pct: r2(((price - prev) / prev) * 100),
    time: m.regularMarketTime || null,
    period: m.currentTradingPeriod?.regular || null,
    closes,
  };
}

function downsample(arr, n) {
  if (arr.length <= n) return arr.map(r2);
  const out = [];
  for (let i = 0; i < n; i++) out.push(r2(arr[Math.round((i * (arr.length - 1)) / (n - 1))]));
  return out;
}

// สถานะตลาดหุ้นไทย (ช่วงเช้า 10:00-12:30, บ่าย 14:30-16:30 วันทำการ)
function setState(now, lastTradeTime) {
  const local = new Date((now + TZ_OFFSET) * 1000);
  const day = local.getUTCDay();
  const mins = local.getUTCHours() * 60 + local.getUTCMinutes();
  if (day === 0 || day === 6) return 'closed';
  // ถ้าวันนี้ไม่มีการซื้อขายเลย (วันหยุด) ให้ถือว่าปิด
  if (lastTradeTime && now - lastTradeTime > 20 * 3600 && mins > 10 * 60 + 30) return 'closed';
  if (mins >= 10 * 60 && mins < 12 * 60 + 30) return 'open';
  if (mins >= 12 * 60 + 30 && mins < 14 * 60 + 30) return 'break';
  if (mins >= 14 * 60 + 30 && mins < 16 * 60 + 30) return 'open';
  return 'closed';
}

// ---------- ราคาทองไทย: สมาคมค้าทองคำ ----------
async function thaiGold() {
  const html = await getText('https://www.goldtraders.or.th/');
  const map = {};
  const re = /id="DetailPlace_uc_goldprices1_(lbl\w+)"[^>]*>([^<]*)</g;
  let m;
  while ((m = re.exec(html))) map[m[1]] = m[2].trim();
  const out = {
    barBuy: num(map.lblBLBuy),
    barSell: num(map.lblBLSell),
    ornBuy: num(map.lblOMBuy),
    ornSell: num(map.lblOMSell),
  };
  if (!out.barSell || !out.ornSell) throw new Error('parse failed');
  // "06/11/2569 เวลา 09:21 น. (ครั้งที่ 3)" -> date, time, round
  const asTime = map.lblAsTime || '';
  const d = asTime.match(/(\d{1,2}\/\d{1,2}\/\d{4})/);
  const t = asTime.match(/(\d{1,2}[:.]\d{2})/);
  const n = asTime.match(/(\d+)\s*\)?\s*$/) || asTime.match(/ครั้งที่\s*(\d+)/);
  out.date = d ? d[1] : null;
  out.time = t ? t[1].replace('.', ':') : null;
  out.round = n ? parseInt(n[1], 10) : null;
  // การเปลี่ยนแปลงวันนี้ (ถ้าหน้าเว็บมี)
  const chg = Object.keys(map).find((k) => /Change/i.test(k));
  out.change = chg ? num(map[chg]) : null;
  out.source = 'goldtraders';
  return out;
}

// ราคาประมาณจากตลาดโลก (ใช้เมื่อดึงจากสมาคมไม่ได้)
function estimateThaiGold(xauUsd, usdThb) {
  // ทอง 96.5% น้ำหนัก 1 บาท = 15.244 กรัม, 1 ออนซ์ = 31.1035 กรัม
  const bar = (xauUsd * usdThb * 15.244 * 0.965) / 31.1035;
  const round50 = (v) => Math.round(v / 50) * 50;
  return {
    barBuy: round50(bar - 100),
    barSell: round50(bar + 100),
    ornBuy: round50(bar * 0.98),
    ornSell: round50(bar + 600),
    date: null, time: null, round: null, change: null,
    source: 'estimate',
  };
}

export async function collect() {
  const now = Math.floor(Date.now() / 1000);
  const errors = {};
  const settle = async (name, fn) => {
    try { return await fn(); } catch (e) { errors[name] = String(e.message || e); return null; }
  };
  const [set, set50, xau, thb, gold] = await Promise.all([
    settle('set', () => yahoo('^SET.BK')),
    settle('set50', () => yahoo('^SET50.BK')),
    settle('xau', () => yahoo('GC=F', '5d', '1h')),
    settle('usdthb', () => yahoo('THB=X', '5d', '1h')),
    settle('gold', thaiGold),
  ]);

  let goldOut = gold;
  if (!goldOut && xau && thb) goldOut = estimateThaiGold(xau.price, thb.price);

  const body = {
    ok: true,
    ts: now,
    gold: goldOut,
    set: set && {
      price: set.price, prev: set.prev, change: set.change, pct: set.pct, time: set.time,
      state: setState(now, set.time),
      spark: downsample(set.closes, 60),
    },
    set50: set50 && { price: set50.price, change: set50.change, pct: set50.pct },
    xau: xau && { price: xau.price, change: xau.change, pct: xau.pct },
    usdthb: thb && { price: r2(thb.price), change: r2(thb.change), pct: thb.pct },
    errors: Object.keys(errors).length ? errors : undefined,
  };
  return body;
}

export default async function handler(req, res) {
  const body = await collect();
  res.setHeader('Cache-Control', 's-maxage=120, stale-while-revalidate=300');
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.status(200).json(body);
}
