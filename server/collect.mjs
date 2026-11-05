// รันโดย GitHub Actions: ดึงข้อมูลตลาดแล้วเขียน market.json ให้บอร์ด ESP32 อ่าน
import { writeFileSync } from 'node:fs';
import { collect } from './api/market.js';

const data = await collect();
writeFileSync(process.argv[2] || 'market.json', JSON.stringify(data));
console.log(JSON.stringify(data, null, 2));
