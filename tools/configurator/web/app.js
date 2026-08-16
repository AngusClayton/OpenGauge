let config = null;
let renderer = null;
let previewTimer = null;
const $ = id => document.getElementById(id);
const message = (text, error=false) => { $('message').textContent=text; $('message').className=error?'error':'ok'; };
const sourceOptions = selected => config.dataSources.map(s => `<option ${s.id===selected?'selected':''}>${s.id}</option>`).join('');
const input = (label,value,onchange,type='text') => { const el=document.createElement('label'); el.textContent=label; const control=document.createElement('input'); control.type=type; control.value=value ?? ''; control.onchange=()=>{onchange(type==='number'?Number(control.value):control.value);schedulePreview();}; el.append(control); return el; };
const select = (label,value,choices,onchange) => { const el=document.createElement('label'); el.textContent=label; const control=document.createElement('select'); control.innerHTML=choices.map(v=>`<option value="${v}" ${v===value?'selected':''}>${v}</option>`).join(''); control.onchange=()=>{onchange(control.value);schedulePreview();}; el.append(control); return el; };
const button = (text,fn,warning=false) => { const el=document.createElement('button'); el.textContent=text; if(warning) el.className='warn'; el.onclick=fn; return el; };
const colorOptions = ['white','gray','blue','cyan','green','yellow','orange','red'];
const icons = {
  drag: '<svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="9" cy="6" r="1.5"/><circle cx="15" cy="6" r="1.5"/><circle cx="9" cy="12" r="1.5"/><circle cx="15" cy="12" r="1.5"/><circle cx="9" cy="18" r="1.5"/><circle cx="15" cy="18" r="1.5"/></svg>',
  trash: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M9 3h6l1 2h4v2H4V5h4l1-2Zm-3 6h12l-1 12H7L6 9Zm4 2v7h2v-7h-2Zm4 0v7h2v-7h-2Z"/></svg>'
};

function syncRaw() { $('raw').value=JSON.stringify(config,null,2); refreshPreviewControls(); }
function normalizeConfig() {
  for(const gauge of config.gauges||[]) for(const secondary of gauge.secondaries||[]) {
    delete secondary.posY;
    if(secondary.rangeColors===undefined && secondary.dynamicColor!==undefined) secondary.rangeColors=!!secondary.dynamicColor;
    if(secondary.lowerThreshold===undefined && secondary.coldBelow!==undefined) secondary.lowerThreshold=secondary.coldBelow;
    if(secondary.upperThreshold===undefined && secondary.hotAbove!==undefined) secondary.upperThreshold=secondary.hotAbove;
    delete secondary.dynamicColor; delete secondary.coldBelow; delete secondary.hotAbove;
    if(secondary.rangeColors) { secondary.lowerThreshold??=0; secondary.upperThreshold??=100; secondary.colorBelow??='blue'; secondary.colorBetween??='cyan'; secondary.colorAbove??='red'; }
  }
}
function checkboxField(labelText,help,checked,onchange) { const el=document.createElement('label'), line=document.createElement('span'), box=document.createElement('input'); el.className='checkbox-field'; line.className='checkbox-line'; box.type='checkbox'; box.checked=checked; box.onchange=()=>onchange(box.checked); line.append(box,labelText); el.append(line); if(help){const note=document.createElement('span');note.className='field-help';note.textContent=help;el.append(note);} return el; }
function moveGauge(fromIndex,toIndex) { if(fromIndex===toIndex)return; const [gauge]=config.gauges.splice(fromIndex,1); config.gauges.splice(toIndex,0,gauge); renderAll(); }
function startGaugeDrag(event,index,handle) {
  event.preventDefault(); event.stopPropagation();
  const card=handle.closest('.card'); card.classList.add('dragging'); handle.setPointerCapture(event.pointerId);
  const move=moveEvent=>{ document.querySelectorAll('.card.drop-target').forEach(el=>el.classList.remove('drop-target')); const target=document.elementFromPoint(moveEvent.clientX,moveEvent.clientY)?.closest('.card[data-gauge-index]'); if(target&&target!==card) target.classList.add('drop-target'); };
  const finish=upEvent=>{ handle.removeEventListener('pointermove',move); handle.removeEventListener('pointerup',finish); handle.removeEventListener('pointercancel',cancel); const target=document.elementFromPoint(upEvent.clientX,upEvent.clientY)?.closest('.card[data-gauge-index]'); document.querySelectorAll('.card.drop-target').forEach(el=>el.classList.remove('drop-target')); card.classList.remove('dragging'); if(target) moveGauge(index,Number(target.dataset.gaugeIndex)); };
  const cancel=()=>{ handle.removeEventListener('pointermove',move); handle.removeEventListener('pointerup',finish); handle.removeEventListener('pointercancel',cancel); document.querySelectorAll('.card.drop-target').forEach(el=>el.classList.remove('drop-target')); card.classList.remove('dragging'); };
  handle.addEventListener('pointermove',move); handle.addEventListener('pointerup',finish); handle.addEventListener('pointercancel',cancel);
}
function card(title,{onDelete=null,dragIndex=null}={}) {
  const el=document.createElement('details'); el.className='card';
  const summary=document.createElement('summary'), label=document.createElement('span'), actions=document.createElement('span'); label.className='card-title'; label.textContent=title; actions.className='card-actions'; summary.append(label,actions);
  if(dragIndex!==null) { el.dataset.gaugeIndex=dragIndex; const drag=document.createElement('button'); drag.type='button'; drag.className='icon-button drag-handle'; drag.innerHTML=icons.drag; drag.title='Drag to reorder'; drag.setAttribute('aria-label',`Drag ${title} to reorder`); drag.onclick=event=>{event.preventDefault();event.stopPropagation();}; drag.onpointerdown=event=>startGaugeDrag(event,dragIndex,drag); actions.append(drag); }
  if(onDelete) { const remove=document.createElement('button'); remove.type='button'; remove.className='icon-button trash'; remove.innerHTML=icons.trash; remove.title='Delete'; remove.setAttribute('aria-label',`Delete ${title}`); remove.onclick=event=>{event.preventDefault();event.stopPropagation();if(window.confirm(`Delete ${title}?`))onDelete();}; actions.append(remove); }
  el.append(summary); return el;
}
function renderSources() {
  const root=$('sources'); root.replaceChildren(); $('source-count').textContent=`(${config.dataSources.length})`;
  config.dataSources.forEach((source,index)=>{
    const el=card(`Source ${index+1}: ${source.id || 'Unnamed source'}`,{onDelete:()=>{config.dataSources.splice(index,1);renderAll();}}), row=document.createElement('div'); row.className='row accordion-content';
    row.append(input('ID',source.id,v=>{source.id=v; renderAll();}), select('Type',source.type,['obd','analog'],v=>{source.type=v; renderAll();}));
    if(source.type==='obd') row.append(input('PID',source.pid,v=>source.pid=v,'number'),input('Formula',source.formula,v=>source.formula=v,'number'));
    else row.append(input('GPIO',source.pin,v=>source.pin=v,'number'),input('Multiplier',source.multiplier,v=>source.multiplier=v,'number'),input('Offset',source.offset,v=>source.offset=v,'number'));
    el.append(row); root.append(el);
  });
}
function renderSecondaries(gauge, root, gaugeIndex) {
  (gauge.secondaries||[]).forEach((secondary,index)=>{
    const row=document.createElement('div'); row.className='row secondary';
    row.append(select('Source',secondary.sourceId,config.dataSources.map(s=>s.id),v=>secondary.sourceId=v),input('Label',secondary.prefix,v=>secondary.prefix=v),input('Unit',secondary.suffix,v=>secondary.suffix=v));
    row.append(checkboxField('Value-based colours','Change the value colour when it crosses the configured thresholds.',!!secondary.rangeColors,v=>{secondary.rangeColors=v;if(v){secondary.lowerThreshold??=0;secondary.upperThreshold??=100;secondary.colorBelow??='blue';secondary.colorBetween??='cyan';secondary.colorAbove??='red';}renderAll(gaugeIndex);}));
    if(secondary.rangeColors) row.append(input('Lower threshold',secondary.lowerThreshold??0,v=>secondary.lowerThreshold=v,'number'),select('Below colour',secondary.colorBelow??'blue',colorOptions,v=>secondary.colorBelow=v),select('Between colour',secondary.colorBetween??'cyan',colorOptions,v=>secondary.colorBetween=v),input('Upper threshold',secondary.upperThreshold??100,v=>secondary.upperThreshold=v,'number'),select('Above colour',secondary.colorAbove??'red',colorOptions,v=>secondary.colorAbove=v));
    row.append(button('Remove secondary',()=>{gauge.secondaries.splice(index,1);renderAll(gaugeIndex);},true)); root.append(row);
  });
  if((gauge.secondaries||[]).length<3) root.append(button('Add secondary',()=>{(gauge.secondaries??=[]).push({sourceId:config.dataSources[0]?.id||'',prefix:'Value: ',suffix:'',rangeColors:false});renderAll(gaugeIndex);}));
}
function renderGauges(openGaugeIndex=null) {
  const root=$('gauges'); root.replaceChildren(); $('gauge-count').textContent=`(${config.gauges.length})`;
  config.gauges.forEach((gauge,index)=>{
    const el=card(`Screen ${index+1}: ${gauge.name || 'Unnamed screen'}`,{dragIndex:index,onDelete:()=>{config.gauges.splice(index,1);renderAll();}}), row=document.createElement('div'); row.className='row accordion-content';
    if(index===openGaugeIndex) el.open=true;
    row.append(input('Name',gauge.name,v=>{gauge.name=v; renderAll(index);}),select('Type',gauge.type,['standard','shiftlight','gmeter','accelTimer'],v=>{const name=gauge.name; Object.keys(gauge).forEach(k=>delete gauge[k]); Object.assign(gauge,{type:v,name}); if(v==='standard') Object.assign(gauge,{mainSourceId:config.dataSources[0]?.id||'',minVal:0,maxVal:100,unitLabel:'',secondaries:[]}); if(v==='shiftlight') gauge.shiftTargets=[6500,6300,6100,6000,5800,0]; if(v==='accelTimer') Object.assign(gauge,{mainSourceId:config.dataSources.find(s=>s.id==='speed')?.id||config.dataSources[0]?.id||'',minVal:0,maxVal:100,unitLabel:'km/h'}); renderAll(index);})); el.append(row);
    if(gauge.type==='standard') { const group=document.createElement('fieldset'), legend=document.createElement('legend'), fields=document.createElement('div'); group.className='option-group'; legend.textContent='Main reading'; fields.className='row'; fields.append(select('Source',gauge.mainSourceId,config.dataSources.map(s=>s.id),v=>gauge.mainSourceId=v),input('Minimum',gauge.minVal,v=>gauge.minVal=v,'number'),input('Maximum',gauge.maxVal,v=>gauge.maxVal=v,'number'),input('Unit label',gauge.unitLabel,v=>gauge.unitLabel=v),checkboxField('Automatic boost/vacuum units','Show inHg below zero and PSI at or above zero. Leave off for normal gauges.',!!gauge.boostUnits,v=>gauge.boostUnits=v)); group.append(legend,fields); el.append(group); const heading=document.createElement('h4');heading.textContent='Secondary readings';el.append(heading); renderSecondaries(gauge,el,index); }
    if(gauge.type==='shiftlight') { const fields=document.createElement('div'); fields.className='row'; gauge.shiftTargets.forEach((value,gear)=>fields.append(input(`G${gear+1} RPM`,value,v=>gauge.shiftTargets[gear]=v,'number'))); el.append(fields); }
    if(gauge.type==='accelTimer') { const fields=document.createElement('div'); fields.className='row'; fields.append(select('Speed source',gauge.mainSourceId,config.dataSources.map(s=>s.id),v=>gauge.mainSourceId=v),input('Start speed',gauge.minVal,v=>gauge.minVal=v,'number'),input('Finish speed',gauge.maxVal,v=>gauge.maxVal=v,'number'),input('Unit',gauge.unitLabel,v=>gauge.unitLabel=v)); el.append(fields); }
    if(gauge.type==='gmeter') { const note=document.createElement('p'); note.className='small'; note.textContent='This screen has no additional configurable fields.'; el.append(note); }
    root.append(el);
  });
}
function defaultSamples(gauge) { if(gauge.type==='shiftlight') return {rpm:5900,speed:120}; if(gauge.type==='gmeter') return {lateralG:-0.42,longitudinalG:0.18,peakLat:-0.95,peakLong:0.68}; if(gauge.type==='accelTimer') return {[gauge.mainSourceId||'speed']:gauge.maxVal??100,timerMs:7420}; const values={}; values[gauge.mainSourceId]=12.4; for(const s of gauge.secondaries||[]) values[s.sourceId]=s.sourceId==='waterTemp'?92:31; return values; }
function refreshPreviewControls() { const selectEl=$('preview-gauge'), selected=Number(selectEl.value||0); selectEl.innerHTML=config.gauges.map((g,i)=>`<option value="${i}" ${i===selected?'selected':''}>${i+1}: ${g.name}</option>`).join(''); renderSamples(); }
function renderSamples() { const gauge=config.gauges[Number($('preview-gauge').value||0)]; const root=$('samples'); root.replaceChildren(); const samples=defaultSamples(gauge); Object.entries(samples).forEach(([name,value])=>root.append(input(name,value,v=>{samples[name]=v;root.dataset.samples=JSON.stringify(samples);schedulePreview();},'number'))); root.dataset.samples=JSON.stringify(samples); schedulePreview(); }
function renderAll(openGaugeIndex=null) { normalizeConfig(); renderSources(); renderGauges(openGaugeIndex); syncRaw(); }
function samplesFromForm() { const samples=JSON.parse($('samples').dataset.samples||'{}'); $('samples').querySelectorAll('input').forEach(el=>samples[el.parentElement.firstChild.textContent]=Number(el.value)); return samples; }
function drawRgb565(bytes) { const canvas=$('preview-canvas'), context=canvas.getContext('2d'), image=context.createImageData(240,240); for(let y=0;y<240;y++)for(let x=0;x<240;x++){const target=(y*240+x)*4;if((x-119.5)**2+(y-119.5)**2>120**2){image.data.set([96,96,96,255],target);continue;}const source=(y*240+x)*2,value=(bytes[source]<<8)|bytes[source+1];image.data[target]=((value>>11)&31)*255/31;image.data[target+1]=((value>>5)&63)*255/63;image.data[target+2]=(value&31)*255/31;image.data[target+3]=255;} context.putImageData(image,0,0); canvas.hidden=false; $('preview-image').hidden=true; }
async function loadRenderer() { try { const factory=(await import('./opengauge-renderer.js')).default; renderer=await factory({locateFile:path=>path}); schedulePreview(); } catch(error) { renderer=null; } }
function renderWasm() { if(!renderer)return false; const configJson=JSON.stringify(config),samplesJson=JSON.stringify(samplesFromForm()),index=Number($('preview-gauge').value),encoder=new TextEncoder(); const status=renderer.ccall('og_render','number',['string','number','number','string','number','number'],[configJson,encoder.encode(configJson).length,index,samplesJson,encoder.encode(samplesJson).length,10000]); if(status){const pointer=renderer.ccall('og_last_error','number',[],[]);throw new Error(renderer.UTF8ToString(pointer));}const pointer=renderer.ccall('og_framebuffer_ptr','number',[],[]),size=renderer.ccall('og_framebuffer_size','number',[],[]);drawRgb565(new Uint8Array(renderer.HEAPU8.buffer,pointer,size));return true; }
function schedulePreview(){if(!renderer)return;clearTimeout(previewTimer);previewTimer=setTimeout(()=>{try{renderWasm();}catch(error){message(error.message,true);}},150);}
async function requestPreview() { try { if(renderWasm()){message('Preview rendered.');return;} const samples=samplesFromForm(); const response=await fetch('/api/preview',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({config,gaugeIndex:Number($('preview-gauge').value),samples})}); if(!response.ok){const body=await response.json();throw new Error(body.errors?.join('\n')||'The on-device WASM renderer is unavailable.');} $('preview-image').src=URL.createObjectURL(await response.blob());$('preview-image').hidden=false;$('preview-canvas').hidden=true;message('Preview rendered.'); } catch(error){message(error.message,true);} }
async function load(url='/api/config') { const response=await fetch(url); config=await response.json(); renderAll(); }
async function save() { const response=await fetch('/api/config',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify(config)}); const body=await response.json(); message(body.errors?.join('\n')||body.message,!response.ok); }
$('add-source').onclick=()=>{config.dataSources.push({id:'newSource',type:'obd',pid:0,formula:0});renderAll();}; $('add-gauge').onclick=()=>{config.gauges.push({type:'standard',name:'New gauge',mainSourceId:config.dataSources[0]?.id||'',minVal:0,maxVal:100,unitLabel:'',secondaries:[]});renderAll();};
$('preview-gauge').onchange=renderSamples; $('render').onclick=requestPreview; $('save').onclick=save; $('restore').onclick=()=>load('/api/default-config');
$('stop-portal').onclick=async()=>{await fetch('/api/portal/stop',{method:'POST'});message('Hotspot is stopping.');};
$('apply-raw').onclick=()=>{try{config=JSON.parse($('raw').value);renderAll();message('Raw JSON loaded into editor.');}catch(error){message(error.message,true);}};
$('download').onclick=()=>{const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([JSON.stringify(config,null,2)],{type:'application/json'}));a.download='opengauge-config.json';a.click();};
$('upload').onchange=event=>{const file=event.target.files[0];if(!file)return;const reader=new FileReader();reader.onload=()=>{try{config=JSON.parse(reader.result);renderAll();message('Upload loaded into editor.');}catch(error){message(error.message,true);}};reader.readAsText(file);};
fetch('/api/status').then(response=>{if(response.ok)$('stop-portal').hidden=false;}).catch(()=>{});
setInterval(()=>fetch('/api/status').catch(()=>{}),30000);
load(); loadRenderer();
