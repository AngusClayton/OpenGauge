import fs from 'node:fs/promises';
import createRenderer from '../configurator/web/opengauge-renderer.js';

const wasmPath = new URL('../configurator/web/opengauge-renderer.wasm', import.meta.url);
const configPath = new URL('../configurator/default_config.json', import.meta.url);
const renderer = await createRenderer({wasmBinary: await fs.readFile(wasmPath)});
const config = await fs.readFile(configPath, 'utf8');
const samples = JSON.stringify({boostPress:12.4,waterTemp:92,intakeTemp:31});
const bytes = new TextEncoder();
const result = renderer.ccall('og_render','number',['string','number','number','string','number','number'],
  [config,bytes.encode(config).length,0,samples,bytes.encode(samples).length,10000]);
if (result !== 0) throw new Error(renderer.UTF8ToString(renderer.ccall('og_last_error','number',[],[])));
const size = renderer.ccall('og_framebuffer_size','number',[],[]);
if (size !== 240*240*2) throw new Error(`Unexpected framebuffer size: ${size}`);
console.log('WASM renderer smoke test passed');
