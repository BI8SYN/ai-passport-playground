import {readFile, writeFile, mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const root = fileURLToPath(new URL('../../', import.meta.url));
const source = JSON.parse(await readFile(root + 'assets/fonts/noto-source.json', 'utf8'));
const path = root + 'artifacts/fonts/NotoSansCJKsc-Regular.otf';
await mkdir(root + 'artifacts/fonts', {recursive: true});
let font;
try { font = await readFile(path); }
catch {
  const response = await fetch(source.url);
  if (!response.ok) throw new Error(`Font download failed: ${response.status}`);
  font = Buffer.from(await response.arrayBuffer());
  await writeFile(path, font);
}
if (createHash('sha256').update(font).digest('hex') !== source.sha256)
  throw new Error('Font SHA256 mismatch');
const ui = await readFile(root + 'firmware/resistor-reader/main/resistor_ui.c', 'utf8');
const symbols = ui.match(/const char rr_ui_glyphs\[\] = "([^"]+)"/)[1];
execFileSync(process.execPath, ['node_modules/lv_font_conv/lv_font_conv.js',
  '--font', path, '--range', '0x20-0x7e', '--symbols', symbols,
  '--size', '16', '--bpp', '4', '--format', 'lvgl', '--no-compress',
  '--lv-include', 'lvgl.h', '--lv-font-name', 'resistor_font_16', '--output',
  root + 'firmware/resistor-reader/main/resistor_font_16.c'], {stdio: 'inherit'});
const output = root + 'firmware/resistor-reader/main/resistor_font_16.c';
const generated = await readFile(output, 'utf8');
await writeFile(output, `/* ${source.copyright}\n * Derived font subset, SIL OFL 1.1; license: assets/fonts/Noto-OFL.txt.\n */\n` + generated.trimEnd() + "\n");
console.log('Generated resistor_font_16 from pinned Noto source; see assets/fonts/Noto-OFL.txt.');
