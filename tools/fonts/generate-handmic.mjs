import {readFile, writeFile, mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

// Deliberately generate only this application's font; never rewrite other products.
const root = fileURLToPath(new URL('../../', import.meta.url));
const source = JSON.parse(await readFile(root + 'assets/fonts/noto-source.json', 'utf8'));
const font = root + 'artifacts/fonts/NotoSansCJKsc-Regular.otf';
await mkdir(root + 'artifacts/fonts', {recursive:true});
let fontData;
try { fontData = await readFile(font); }
catch {
  const response = await fetch(source.url);
  if (!response.ok) throw new Error(`Font download failed: ${response.status}`);
  fontData = Buffer.from(await response.arrayBuffer());
  await writeFile(font, fontData);
}
if (createHash('sha256').update(fontData).digest('hex') !== source.sha256)
  throw new Error('Font SHA256 mismatch');
const ui = await readFile(root + 'firmware/rf-link-handmic/main/hm_ui.c', 'utf8');
const product = await readFile(root + 'firmware/rf-link-handmic/main/hm_product.c', 'utf8');
// Include every static Chinese code point, plus punctuation/labels used by runtime snapshots.
const runtime = '约未知未提供已过期米公里秒网络在线离线守听接收发射忙碌无权限资料接收年龄位置天线高度连接断开蓝牙服务器上下键菜单返回配对确认取消正在等待手机安全未就绪频率网格电台音量';
// GB2312 level 1 + level 2 Chinese: 6763 assigned Han characters. GB18030
// shares their two-byte encoding; the five unassigned D7 FA..FE slots are excluded.
const decoder = new TextDecoder('gb18030', {fatal:true});
const common = new Set();
for (let lead = 0xb0; lead <= 0xf7; ++lead) {
  for (let trail = 0xa1; trail <= 0xfe; ++trail) {
    if (lead === 0xd7 && trail >= 0xfa) continue;
    const char = decoder.decode(Uint8Array.of(lead, trail));
    if ([...char].length === 1 && char !== '\ufffd') common.add(char);
  }
}
if (common.size !== 6763) throw new Error(`GB2312 repertoire changed: ${common.size}`);
const symbols = [...new Set([...common, ...(ui + product + runtime).match(/[^\x00-\x7f]/gu)])].sort().join('');
const output = root + 'firmware/rf-link-handmic/main/hm_font_16.c';
execFileSync(process.execPath, ['node_modules/lv_font_conv/lv_font_conv.js', '--font', font,
  '--range','0x20-0x7e','--symbols',symbols,'--size','16','--bpp','4','--format','lvgl',
  '--no-compress','--lv-include','lvgl.h','--lv-font-name','hm_font_16','--output',output], {stdio:'inherit'});
await writeFile(output, `/* ${source.copyright}\n * Derived font subset, SIL OFL 1.1; license: assets/fonts/Noto-OFL.txt.\n * GB2312 6763 Han characters + static UI symbols + ASCII.
 * Unsupported dynamic Unicode (e.g. emoji) is replaced with '?'\n * and the page visibly reports its character-set limitation.\n */\n` + (await readFile(output, 'utf8')).trimEnd() + '\n');
const generated = await readFile(output, 'utf8');
const bitmap = generated.match(/glyph_bitmap\[\] = \{([\s\S]*?)\n\};/)[1];
const bitmapBytes = [...bitmap.matchAll(/0x[0-9a-f]+/gi)].length;
if (bitmapBytes > 3 * 1024 * 1024) throw new Error(`Unexpected bitmap size: ${bitmapBytes}`);
console.log(`Generated hm_font_16: ASCII + ${symbols.length} Unicode glyphs including 6763 GB2312 Han.`);
console.log(`Read-only bitmap: ${bitmapBytes} bytes; generated C: ${Buffer.byteLength(generated)} bytes.`);
