// Setup page served by the hub's hotspot (src/wifimgr.cpp). Self-contained: no internet on the hotspot.
// Same look as the Tile Console web app (Apple-style: system font, concentric corners 20/12/10).
#pragma once

#include <Arduino.h>

const char PORTAL_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tile Hub setup</title>
<style>
:root{--bg:#f2f2f7;--card:#fff;--text:#1c1c1e;--muted:#6b6b70;--line:#e5e5ea;--field:#f2f2f7;--accent:#059669;--bad:#dc2626;--warn:#b45309}
@media(prefers-color-scheme:dark){:root{--bg:#09090b;--card:#18181b;--text:#f4f4f5;--muted:#a1a1aa;--line:#27272a;--field:#09090b;--accent:#34d399;--bad:#f87171;--warn:#fbbf24}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;-webkit-font-smoothing:antialiased}
main{max-width:440px;margin:0 auto;padding:20px 16px 40px}
h1{font-size:28px;margin:4px 0 2px;letter-spacing:-.02em}
.sub{color:var(--muted);font-size:13px;margin-bottom:18px}
.card{background:var(--card);border:1px solid var(--line);border-radius:20px;padding:16px;margin-bottom:14px}
h2{font-size:16px;margin:0 0 10px}
.row{display:flex;align-items:center;gap:10px;padding:10px 4px;border-top:1px solid var(--line)}
.row:first-of-type{border-top:0}
.grow{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.muted{color:var(--muted);font-size:13px}
.ok{color:var(--accent)}.bad{color:var(--bad)}.warn{color:var(--warn)}
button{font:inherit;font-weight:600;border:0;border-radius:10px;padding:10px 14px;cursor:pointer;background:var(--field);color:var(--text)}
button.primary{background:#10b981;color:#09090b;width:100%;margin-top:10px}
button.link{background:none;padding:6px 4px;color:var(--muted);font-weight:500}
button:disabled{opacity:.45}
.pick{width:100%;text-align:left;display:flex;gap:10px;align-items:center;background:none;border-radius:10px;padding:10px 6px;font-weight:500}
.pick:hover,.pick.sel{background:var(--field)}
label{display:block;font-size:13px;color:var(--muted);margin:10px 0 4px}
input{width:100%;font:inherit;padding:10px 12px;border-radius:10px;border:1px solid var(--line);background:var(--field);color:var(--text)}
.bars{font-size:12px;color:var(--muted);letter-spacing:1px}
.note{font-size:13px;color:var(--muted);margin-top:10px}
</style></head><body><main>
<h1>Tile Hub</h1>
<div class="sub" id="sub">Connecting to the hub…</div>

<section class="card"><h2>Status</h2><div id="status" class="muted">…</div><div id="closes" class="note"></div></section>

<section class="card"><h2>Saved networks</h2>
<div class="muted" style="margin-bottom:6px">Tried in this order. The one that worked last comes first.</div>
<div id="saved"></div></section>

<section class="card"><h2>Add or change network</h2>
<button id="scanBtn" onclick="scan()">Scan for networks</button>
<div id="scanList"></div>
<label for="ssid">Network name</label><input id="ssid" autocomplete="off" autocapitalize="none" maxlength="32">
<label for="pass">Password</label><input id="pass" type="password" maxlength="63" placeholder="Leave empty to keep the saved one">
<button class="primary" id="connectBtn" onclick="connectNet()">Connect</button>
<div id="trial" class="note"></div>
<div class="note">2.4 GHz networks only. If this page stops loading after connecting, join <b id="hs">the hub hotspot</b> again: it may have moved to the new network's channel.</div>
</section>
</main>
<script>
const $=id=>document.getElementById(id);
function el(tag,cls,text){const e=document.createElement(tag);if(cls)e.className=cls;if(text!=null)e.textContent=text;return e}
function bars(r){return r>-55?'▂▄▆█':r>-67?'▂▄▆':r>-78?'▂▄':'▂'}
async function post(url,data){const r=await fetch(url,{method:'POST',body:new URLSearchParams(data)});let j={};try{j=await r.json()}catch(e){}if(!r.ok)throw new Error(j.error||('HTTP '+r.status));return j}
let saved=[];
async function refresh(){
  let s;try{s=await(await fetch('/api/status',{cache:'no-store'})).json()}catch(e){$('sub').textContent='Hub not reachable. Are you still on its hotspot?';return}
  $('sub').textContent='Hub fw '+s.fw+' · hotspot '+s.hotspot;$('hs').textContent=s.hotspot;
  const st=$('status');st.textContent='';
  if(s.connected){st.append(el('div',s.online?'ok':'warn',(s.online?'Online':'On WiFi, cloud not reachable yet')));st.append(el('div','muted','Network: '+s.ssid+' · '+bars(s.rssi)+' '+s.rssi+' dBm · channel '+s.channel))}
  else{st.append(el('div','bad','Offline'));st.append(el('div','muted',(s.saved.length?'Retrying saved networks':'No saved network yet')+(s.downS!=null?' · '+s.downS+' s':'')))}
  $('closes').textContent=s.closesInS==null?'This hotspot stays open until the hub is back online.':'This hotspot closes in '+Math.floor(s.closesInS/60)+':'+String(s.closesInS%60).padStart(2,'0')+' if nobody uses it.';
  saved=s.saved;const sv=$('saved');sv.textContent='';
  if(!s.saved.length)sv.append(el('div','muted','None yet.'));
  s.saved.forEach((n,i)=>{const r=el('div','row');r.append(el('div','grow',n));
    if(s.connected&&n===s.ssid)r.append(el('span','ok','Connected'));
    const f=el('button','link','Forget');f.onclick=async()=>{if(!confirm('Forget "'+n+'"?'))return;await post('/api/forget',{ssid:n});refresh()};r.append(f);sv.append(r)});
  const t=s.trial,tr=$('trial');tr.textContent='';tr.className='note';
  if(t.state==='trying'){tr.textContent='Connecting to '+t.ssid+'…';$('connectBtn').disabled=true}
  else{$('connectBtn').disabled=false;
    if(t.state==='ok'){tr.className='note ok';tr.textContent='Connected to '+t.ssid+' and saved as first choice.'}
    if(t.state==='failed'){tr.className='note bad';tr.textContent='Could not connect to '+t.ssid+': '+t.why+'. ';
      const b=el('button','link','Save anyway (try later)');b.onclick=async()=>{await post('/api/save',{ssid:$('ssid').value.trim(),pass:$('pass').value});tr.textContent='Saved.';refresh()};tr.append(b)}}
}
async function scan(){
  $('scanBtn').disabled=true;$('scanBtn').textContent='Scanning…';
  try{await post('/api/scan',{});let j;for(let i=0;i<30;i++){await new Promise(r=>setTimeout(r,700));j=await(await fetch('/api/scan',{cache:'no-store'})).json();if(!j.scanning)break}
    const l=$('scanList');l.textContent='';j.networks.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{const b=el('button','pick');b.append(el('span','grow',n.ssid));
      if(n.saved)b.append(el('span','muted','saved'));if(n.open)b.append(el('span','muted','open'));b.append(el('span','bars',bars(n.rssi)));
      b.onclick=()=>{$('ssid').value=n.ssid;$('pass').value='';$('pass').focus();document.querySelectorAll('.pick').forEach(x=>x.classList.remove('sel'));b.classList.add('sel')};l.append(b)});
    if(!j.networks.length)l.append(el('div','muted','No networks found.'));
  }catch(e){alert('Scan failed: '+e.message)}
  $('scanBtn').disabled=false;$('scanBtn').textContent='Scan again';
}
async function connectNet(){
  const ssid=$('ssid').value.trim(),pass=$('pass').value;
  if(!ssid){$('ssid').focus();return}
  try{await post('/api/connect',{ssid,pass});refresh()}catch(e){$('trial').className='note bad';$('trial').textContent=e.message}
}
refresh();setInterval(refresh,2000);
</script></body></html>)HTML";
