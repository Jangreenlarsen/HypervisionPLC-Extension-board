#pragma once

// v0.36.0 — den OFFENTLIGE, skrivebeskyttede statusside (GET /). Ingen login,
// ingen knapper, ingen formularer, ingen handlinger: siden henter kun
// /status.json (lib/public_status — en fast allowlist af uskadelige felter) og
// viser det. Al tekst sættes via textContent (aldrig innerHTML), så intet
// fra boardet kan blive fortolket som HTML/script.
static const char kStatusPageHtml[] = R"HTML(<!doctype html>
<html lang="da">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Expansion Board status</title>
<style>
:root{--bg:#f4f7f6;--card:#fff;--ink:#16232a;--muted:#5a6b72;--line:#d3dcdc;--ok:#1d6f45;--okbg:#e0f1e7;--warn:#7d5200;--warnbg:#fbefd2;--bad:#9e2f29;--badbg:#f8e2df;--off:#55636a;--offbg:#e6ecec;--acc:#0b6677}
@media (prefers-color-scheme:dark){:root{--bg:#0f171b;--card:#152128;--ink:#e2eced;--muted:#9db0b5;--line:#2a3c44;--ok:#77d6a1;--okbg:#12301f;--warn:#f1c469;--warnbg:#35290f;--bad:#f2918a;--badbg:#3a1a18;--off:#9db0b5;--offbg:#1d2b32;--acc:#5fc3d3}}
*{box-sizing:border-box}
body{margin:0;padding:20px 16px 40px;background:var(--bg);color:var(--ink);font:16px/1.5 system-ui,-apple-system,"Segoe UI",sans-serif}
main{max-width:980px;margin:0 auto;display:grid;gap:16px}
header{display:flex;flex-wrap:wrap;gap:8px 16px;align-items:baseline;justify-content:space-between;border-bottom:2px solid var(--ink);padding-bottom:10px}
h1{margin:0;font-size:22px}
.host{color:var(--muted);font-size:15px}
.live{font-size:14px;color:var(--muted);display:flex;align-items:center;gap:8px}
.dot{width:10px;height:10px;border-radius:50%;background:var(--off)}
.dot.on{background:var(--ok)}.dot.err{background:var(--bad)}
.banner{padding:10px 14px;border-radius:8px;background:var(--warnbg);color:var(--warn);border:1px solid var(--warn)}
.banner.bad{background:var(--badbg);color:var(--bad);border-color:var(--bad)}
.grid{display:grid;gap:10px;grid-template-columns:repeat(auto-fit,minmax(210px,1fr))}
.tile{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:10px 14px}
.tile b{display:block;font-size:12px;letter-spacing:.06em;text-transform:uppercase;color:var(--muted);font-weight:600}
.tile span{font-size:17px;overflow-wrap:anywhere}
h2{margin:6px 0 0;font-size:17px}
.tw{overflow-x:auto;background:var(--card);border:1px solid var(--line);border-radius:8px}
table{border-collapse:collapse;width:100%;font-size:15px;font-variant-numeric:tabular-nums}
th,td{padding:8px 12px;text-align:left;border-bottom:1px solid var(--line);white-space:nowrap}
th{font-size:12px;letter-spacing:.05em;text-transform:uppercase;color:var(--muted);background:var(--offbg)}
tr:last-child td{border-bottom:none}
td.n,th.n{text-align:right}
.pill{display:inline-block;padding:1px 9px;border-radius:999px;font-size:13px;font-weight:600}
.pill.ok{background:var(--okbg);color:var(--ok)}.pill.warn{background:var(--warnbg);color:var(--warn)}
.pill.bad{background:var(--badbg);color:var(--bad)}.pill.off{background:var(--offbg);color:var(--off)}
footer{color:var(--muted);font-size:13px}
</style>
</head>
<body>
<main>
<header>
 <div><h1>HypervisionPLC Extension Board</h1><div class="host" id="host">&nbsp;</div></div>
 <div class="live"><span class="dot" id="dot"></span><span id="upd">Henter&hellip;</span></div>
</header>
<div id="banners"></div>
<div class="grid" id="tiles"></div>
<h2>Kanaler</h2>
<div class="tw"><table>
<thead><tr><th>Kanal</th><th>Status</th><th>Mode</th><th>Port</th><th class="n">Foresp&oslash;rgsler</th><th class="n">Lykkedes</th><th class="n">Timeouts</th><th class="n">CRC</th><th class="n">Exceptions</th></tr></thead>
<tbody id="rows"><tr><td colspan="9">Henter&hellip;</td></tr></tbody>
</table></div>
<footer>Skrivebeskyttet side uden login. Den viser ingen adgangskoder, tokens eller IP-adresser, og kan ikke &aelig;ndre noget &mdash; al konfiguration sker fra PLC&rsquo;en. Opdateres automatisk hvert 5. sekund.</footer>
</main>
<script>
(function(){
var RESET={power_on:'Strøm tændt',external:'Ekstern reset',software:'Softwarestart (fx opdatering)',panic:'Nedbrud (panic)',int_wdt:'Watchdog (interrupt)',task_wdt:'Watchdog (task)',wdt:'Watchdog',deep_sleep:'Deep sleep',brownout:'Spændingsfald',sdio:'SDIO',unknown:'Ukendt'};
var ETH={connected:'Forbundet',waiting_dhcp:'Venter på DHCP',link_down:'Intet link (tjek kabel)',not_detected:'Intet Ethernet-modul fundet'};
var EXP={ok:'CJMCU-752 fundet',not_fitted:'Ikke monteret',not_found:'Monteret, men svarer ikke'};
var CH={ok:['OK','ok'],error:['Fejl','warn'],disabled:['Slået fra','off'],unavailable:['Utilgængelig','bad']};
function $(id){return document.getElementById(id)}
function el(tag,text,cls){var e=document.createElement(tag);if(text!==undefined)e.textContent=text;if(cls)e.className=cls;return e}
function up(s){var d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return (d?d+' d ':'')+h+' t '+m+' m'}
function n(v){return Number(v).toLocaleString('da-DK')}
function tile(label,value){var t=el('div',undefined,'tile');t.appendChild(el('b',label));t.appendChild(el('span',value));return t}
function banner(text,bad){$('banners').appendChild(el('div',text,'banner'+(bad?' bad':'')))}
function render(d){
 $('host').textContent=d.hostname||'';
 var t=$('tiles');t.textContent='';
 t.appendChild(tile('Firmware','v'+d.fw_version+' build '+d.fw_build));
 t.appendChild(tile('Oppetid',up(d.uptime_s)));
 t.appendChild(tile('Sidste genstart',RESET[d.reset_reason]||d.reset_reason));
 t.appendChild(tile('Fri hukommelse',n(Math.round(d.heap_free_bytes/1024))+' kB'));
 t.appendChild(tile('Board-type',d.board_type));
 t.appendChild(tile('UART-expander',EXP[d.expander]||d.expander));
 t.appendChild(tile('Ethernet',ETH[d.ethernet]||d.ethernet));
 t.appendChild(tile('WiFi',d.wifi_connected?'Forbundet':'Ikke forbundet'));
 var b=$('banners');b.textContent='';
 if(d.ota_pending_confirm)banner('Ny firmware afventer bekræftelse fra PLC’en. Bekræftes den ikke i tide, rulles der automatisk tilbage.',false);
 if(d.ota_last_update_rolled_back)banner('Den seneste firmwareopdatering blev rullet tilbage — boardet kører den forrige firmware.',false);
 if(d.expander==='not_found')banner('UART-expanderen (CJMCU-752) er monteret ifølge jumperen, men svarer ikke. Kanal C og D er ikke tilgængelige.',true);
 var rows=$('rows');rows.textContent='';
 d.channels.forEach(function(c,i){
  var tr=document.createElement('tr'),s=CH[c.status]||[c.status,'off'];
  tr.appendChild(el('td','Kanal '+c.channel));
  var st=document.createElement('td');st.appendChild(el('span',s[0],'pill '+s[1]));tr.appendChild(st);
  tr.appendChild(el('td',(c.mode==='rs232'?'RS232':'RS485')+' · '+n(c.baudrate)+' baud · '+(c.parity==='none'?'N':c.parity==='even'?'E':'O')+'8'+c.stop_bits));
  tr.appendChild(el('td',String(502+i)));
  var pct=c.total_requests>0?(' ('+(100*c.successful_requests/c.total_requests).toFixed(1).replace('.',',')+' %)'):'';
  tr.appendChild(el('td',n(c.total_requests),'n'));
  tr.appendChild(el('td',n(c.successful_requests)+pct,'n'));
  tr.appendChild(el('td',n(c.timeout_errors),'n'));
  tr.appendChild(el('td',n(c.crc_errors),'n'));
  tr.appendChild(el('td',n(c.exception_errors),'n'));
  rows.appendChild(tr);
 });
}
function tick(){
 if(document.hidden){setTimeout(tick,5000);return}
 fetch('/status.json',{cache:'no-store'}).then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);return r.json()})
 .then(function(d){render(d);$('dot').className='dot on';$('upd').textContent='Opdateret '+new Date().toLocaleTimeString('da-DK')})
 .catch(function(e){$('dot').className='dot err';$('upd').textContent='Ingen forbindelse til boardet ('+e.message+')'})
 .then(function(){setTimeout(tick,5000)});
}
tick();
})();
</script>
</body>
</html>)HTML";
