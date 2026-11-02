#pragma once

namespace md {

// Operator control panel served at GET /. Plain HTML + fetch(), no build step.
inline constexpr const char* kPanelHtml = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Match Director control</title>
<style>
 body{font-family:system-ui,sans-serif;margin:0;padding:16px;background:#0f172a;color:#e2e8f0;max-width:720px}
 h1{font-size:20px;margin:0 0 12px} fieldset{border:1px solid #334155;border-radius:8px;margin:0 0 12px;padding:12px}
 legend{padding:0 6px;color:#94a3b8} button{font:inherit;padding:10px 14px;margin:4px;border:0;border-radius:6px;background:#2563eb;color:#fff;cursor:pointer}
 button.alt{background:#475569} button.warn{background:#ca8a04;color:#111} input{font:inherit;padding:8px;border-radius:6px;border:1px solid #475569;background:#1e293b;color:#e2e8f0;width:calc(100% - 20px);margin:4px 0}
 pre{background:#020617;padding:8px;border-radius:6px;overflow:auto;font-size:12px}
</style></head><body>
<h1>Match Director: operator panel</h1>
<fieldset><legend>Score</legend>
 <button onclick="send({type:'goal',team:'home',text:document.getElementById('scorer').value})">Goal HOME</button>
 <button class="alt" onclick="send({type:'goal',team:'away',text:document.getElementById('scorer').value})">Goal AWAY</button>
 <input id="scorer" placeholder="Scorer (optional), e.g. No. 9">
</fieldset>
<fieldset><legend>Replay</legend>
 <button class="warn" onclick="send({type:'replay',seconds:6})">Replay last 6 s</button>
 <button class="warn" onclick="send({type:'replay',seconds:10})">Replay last 10 s</button>
</fieldset>
<fieldset><legend>Lower third</legend>
 <input id="lt" placeholder="Caption text">
 <button onclick="send({type:'lower_third',text:document.getElementById('lt').value,seconds:6})">Show</button>
 <button class="alt" onclick="send({type:'clear_lower_third'})">Clear</button>
</fieldset>
<fieldset><legend>Teams / clock</legend>
 <input id="home" placeholder="Home name"><input id="away" placeholder="Away name">
 <button onclick="send({type:'teams',home:document.getElementById('home').value,away:document.getElementById('away').value})">Set names</button>
 <button class="alt" onclick="send({type:'kickoff'})">Kick-off (clock 00:00)</button>
</fieldset>
<pre id="state">loading...</pre>
<script>
async function send(cmd){
  const r = await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(cmd)});
  document.getElementById('state').textContent = await r.text();
}
async function poll(){
  try{ const r = await fetch('/api/state'); document.getElementById('state').textContent = JSON.stringify(await r.json(),null,2); }catch(e){}
  setTimeout(poll, 1000);
}
poll();
</script></body></html>
)HTML";

}  // namespace md
