#pragma once
#include <string>

// The page phones see after scanning. Static (data comes from api/manifest) and
// split into chunks to stay under MSVC's string literal limit.
inline const std::string& web_page() {
    static const std::string page = std::string(R"PD(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="dark light"><meta name="theme-color" content="#0f0f13">
<title>PocketDrop</title>
<style>
:root{--bg:#0f0f13;--card:#1a1a21;--line:#2a2a34;--text:#f3f3f6;--mute:#9b9ba8;--acc:#8b7cff;--acc2:#b9afff;--accs:rgba(139,124,255,.16)}
@media (prefers-color-scheme:light){:root{--bg:#f4f4f8;--card:#fff;--line:#e3e3ea;--text:#16161c;--mute:#6b6b78;--acc:#6a58f0;--acc2:#5646d6;--accs:rgba(106,88,240,.12)}}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
main{max-width:560px;margin:0 auto;padding:max(16px,env(safe-area-inset-top)) 16px calc(28px + env(safe-area-inset-bottom))}
header{display:flex;align-items:center;gap:11px;margin:6px 2px 18px}
.logo{width:36px;height:36px;border-radius:11px;background:linear-gradient(135deg,#8b7cff,#4fc3f7);display:grid;place-items:center;flex:none}
.logo svg{width:20px;height:20px;stroke:#fff}
h1{font-size:18px;margin:0;letter-spacing:-.01em}
.sub{color:var(--mute);font-size:13px}
.card{background:var(--card);border:1px solid var(--line);border-radius:18px;padding:16px;margin-bottom:12px}
.sum{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:14px}
.sum b{font-size:22px;letter-spacing:-.02em}
.btn{display:flex;align-items:center;justify-content:center;gap:9px;width:100%;border:0;border-radius:14px;padding:15px 16px;font:600 16px/1 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;color:#fff;background:var(--acc);text-decoration:none;cursor:pointer}
.btn.sec{background:transparent;color:var(--text);border:1px solid var(--line);margin-top:8px}
.btn[aria-disabled=true]{opacity:.6;pointer-events:none}
.btn svg{width:19px;height:19px;stroke:currentColor;fill:none;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}
.photo-note{color:var(--mute);font-size:12.5px;line-height:1.45;margin:10px 4px 0;text-align:center}
.bar{height:4px;border-radius:2px;background:var(--line);overflow:hidden;margin-top:10px}
.bar i{display:block;height:100%;background:var(--acc);width:0;transition:width .3s}
.list{padding:6px}
.row{display:flex;align-items:center;gap:12px;padding:9px;border-radius:12px}
.th{width:50px;height:50px;border-radius:11px;background:var(--line);flex:none;display:grid;place-items:center;overflow:hidden;position:relative}
.th img{width:100%;height:100%;object-fit:cover;position:absolute;inset:0}
.th svg,.dl svg{width:22px;height:22px;stroke:var(--mute);fill:none;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round}
.meta{flex:1;min-width:0;color:inherit;text-decoration:none}
.nm{white-space:nowrap;overflow:hidden;text-overflow:ellipsis;font-weight:500}
.dt{color:var(--mute);font-size:12.5px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.dl{width:40px;height:40px;border-radius:50%;display:grid;place-items:center;flex:none;background:var(--accs)}
.dl svg{width:19px;height:19px;stroke:var(--acc2)}
.miss{opacity:.45}
.txt{white-space:pre-wrap;word-break:break-word;font:14px/1.45 ui-monospace,Menlo,Consolas,monospace;max-height:240px;overflow:auto;margin:0 0 14px}
.two{display:flex;gap:8px}.two .btn{margin:0}
.hint{color:var(--mute);font-size:12.5px;text-align:center;margin:18px 12px 0}
.state{text-align:center;padding:44px 18px}.state b{display:block;font-size:17px;margin-bottom:6px}
.more{color:var(--mute);font-size:13px;text-align:center;padding:10px}
#off{position:fixed;left:50%;transform:translateX(-50%);bottom:calc(18px + env(safe-area-inset-bottom));background:#e5484d;color:#fff;padding:8px 14px;border-radius:99px;font-size:13px;white-space:nowrap}
[hidden]{display:none!important}
</style></head><body>
)PD") + std::string(R"PD(<svg width="0" height="0" style="position:absolute" aria-hidden="true">
<symbol id="i-file" viewBox="0 0 24 24"><path d="M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z"/><path d="M14 3v5h5"/></symbol>
<symbol id="i-text" viewBox="0 0 24 24"><path d="M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z"/><path d="M14 3v5h5M9 13h6M9 17h6"/></symbol>
<symbol id="i-pdf" viewBox="0 0 24 24"><path d="M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z"/><path d="M14 3v5h5M9 15h1.5a1.5 1.5 0 0 0 0-3H9v5"/></symbol>
<symbol id="i-image" viewBox="0 0 24 24"><rect x="3" y="3" width="18" height="18" rx="3"/><circle cx="9" cy="9" r="2"/><path d="m21 15-5-5L5 21"/></symbol>
<symbol id="i-video" viewBox="0 0 24 24"><rect x="2" y="5" width="15" height="14" rx="3"/><path d="m17 10 5-3v10l-5-3"/></symbol>
<symbol id="i-audio" viewBox="0 0 24 24"><path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/></symbol>
<symbol id="i-archive" viewBox="0 0 24 24"><rect x="3" y="4" width="18" height="5" rx="1"/><path d="M5 9v10a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V9M10 13h4"/></symbol>
<symbol id="i-dl" viewBox="0 0 24 24"><path d="M12 4v11M7 10.5l5 5 5-5M5 20h14"/></symbol>
<symbol id="i-copy" viewBox="0 0 24 24"><rect x="9" y="9" width="12" height="12" rx="2"/><path d="M5 15V5a2 2 0 0 1 2-2h10"/></symbol>
<symbol id="i-check" viewBox="0 0 24 24"><path d="M5 12.5l4.5 4.5L19 7.5"/></symbol>
<symbol id="i-link" viewBox="0 0 24 24"><path d="M10 13a5 5 0 0 0 7 0l3-3a5 5 0 0 0-7-7l-1 1"/><path d="M14 11a5 5 0 0 0-7 0l-3 3a5 5 0 0 0 7 7l1-1"/></symbol>
<symbol id="i-share" viewBox="0 0 24 24"><path d="M4 12v7a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-7M16 6l-4-4-4 4M12 2v13"/></symbol>
</svg>
<main>
<header><div class="logo"><svg viewBox="0 0 24 24" fill="none" stroke-width="2.3" stroke-linecap="round" stroke-linejoin="round"><path d="M12 4v11M7 10.5l5 5 5-5M5 20h14"/></svg></div>
<div><h1>PocketDrop</h1><div class="sub" id="from">Connecting…</div></div></header>
<div id="app"></div>
<p class="hint" id="hint"></p>
</main>
<div id="off" hidden>Reconnecting to your PC…</div>
)PD") + std::string(R"PD(<script>
"use strict";
const $=s=>document.querySelector(s);
const esc=s=>String(s).replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const enc=encodeURIComponent;
const ic=n=>`<svg><use href="#i-${n}"/></svg>`;
const icons={image:"image",video:"video",audio:"audio",pdf:"pdf",archive:"archive",text:"text",file:"file"};
const MAX_ROWS=400;
const IOS=/iPhone|iPad|iPod/.test(navigator.userAgent)||(navigator.platform==="MacIntel"&&navigator.maxTouchPoints>1);
let data=null,timer=0,ended=false,shareFiles=null;
function fmt(b){const u=["B","KB","MB","GB","TB"];let i=0;while(b>=1024&&i<4){b/=1024;i++}return(i?b.toFixed(b<10?2:b<100?1:0):b)+" "+u[i]}
const fileUrl=(f,dl)=>`f/${f.i}/${enc(f.name)}${dl?"?dl=1":""}`;
const plural=(n,w)=>`${n} ${w}${n===1?"":"s"}`;

async function poll(){
  clearTimeout(timer);
  if(ended)return;
  try{
    const r=await fetch("api/manifest",{cache:"no-store"});
    if(r.status===404)return gone();
    const m=await r.json();
    $("#off").hidden=true;
    if(!data||m.rev!==data.rev||m.ready!==data.ready){data=m;render()}
    else{data=m;progress()}
  }catch(e){$("#off").hidden=false}
  if(!document.hidden)timer=setTimeout(poll,data&&data.ready?2500:700);
}
document.addEventListener("visibilitychange",()=>{if(!document.hidden)poll()});

function gone(){
  ended=true;
  $("#from").textContent="";$("#hint").textContent="";
  $("#app").innerHTML=`<div class="card state"><b>This drop has ended</b><div class="sub">Scan the new QR code on your PC to keep going.</div></div>`;
}

function progress(){
  const l=$("#ziplbl"),bar=$("#zipbar");
  if(l&&!data.ready)l.textContent=`Packing… ${Math.round(data.prep*100)}%`;
  if(bar)bar.firstChild.style.width=(data.prep*100)+"%";
}

function render(){
  const m=data,app=$("#app");
  $("#from").textContent="from "+m.pc;
  const nf=m.files.length;
  document.title=nf?`PocketDrop · ${plural(nf,"file")}`:"PocketDrop";
  if(!nf&&!m.texts.length){
    app.innerHTML=`<div class="card state"><b>Waiting for files</b><div class="sub">Drop files onto PocketDrop on your PC. This page updates by itself.</div></div>`;
    $("#hint").textContent="";return;
  }
  let h="",th="";
  m.texts.forEach((t,k)=>{
    const u=t.trim(),isUrl=/^https?:\/\/\S+$/i.test(u);
    th+=`<div class="card"><pre class="txt">${esc(t)}</pre><div class="two">`+
      (isUrl?`<a class="btn" href="${esc(u)}" target="_blank" rel="noopener noreferrer">${ic("link")}Open</a>`:"")+
      `<button class="btn${isUrl?" sec":""}" data-copy="${k}">${ic("copy")}Copy</button></div></div>`;
  });
  if(nf){
    const single=nf===1&&!m.zip.preferred;
    h+=`<div class="card"><div class="sum"><b>${plural(nf,"file")}</b><span class="sub">${fmt(m.total)}</span></div>`;
    if(single){
      h+=m.files[0].missing?`<div class="btn" aria-disabled="true">File no longer available</div>`:`<a class="btn" href="${fileUrl(m.files[0],1)}" download>${ic("dl")}${IOS?"Save to Files":"Download"}</a>`;
    }else{
      h+=`<a class="btn" id="zipbtn" href="zip/${enc(m.zip.name)}" download${m.ready?"":' aria-disabled="true"'}>${ic("archive")}<span id="ziplbl">${m.ready?(IOS?"Save ZIP to Files · ":"Download all · ")+fmt(m.zip.size):"Packing… "+Math.round(m.prep*100)+"%"}</span></a>`;
      if(!m.ready)h+=`<div class="bar" id="zipbar"><i style="width:${m.prep*100}%"></i></div>`;
    }
    const media=m.files.filter(f=>(f.kind==="image"||f.kind==="video")&&!f.missing);
    const mb=media.reduce((a,f)=>a+f.size,0);
    const canShare=window.isSecureContext&&navigator.share&&navigator.canShare&&media.length&&media.length<=40&&mb<=400*1048576;
    if(canShare){
      h+=`<button class="btn sec" id="share">${ic("share")}<span>${IOS?`Save ${plural(media.length,"item")} to Photos`:`Share ${plural(media.length,"media item")}`}</span></button>`;
      if(IOS)h+=`<div class="photo-note" id="share-note">After preparation, choose Save Image or Save Video in Apple's share sheet.</div>`;
    }else if(IOS&&media.length){
      h+=`<div class="photo-note">To add media to Photos, open its thumbnail, tap Share, then choose Save Image or Save Video.</div>`;
    }
    h+=`</div>`+th+`<div class="card list">`;
    for(const f of m.files.slice(0,MAX_ROWS)){
      const slash=f.path.lastIndexOf("/"),dir=slash>0?f.path.slice(0,slash)+" · ":"";
      const thumb=["image","video","pdf"].includes(f.kind)&&!f.missing?`<img loading="lazy" alt="" src="t/${f.i}" onerror="this.remove()">`:"";
      h+=`<div class="row${f.missing?" miss":""}"><a class="th" href="${fileUrl(f)}" target="_blank" rel="noopener">${ic(icons[f.kind]||"file")}${thumb}</a>`+
        `<a class="meta" href="${fileUrl(f,1)}" download><div class="nm">${esc(f.name)}</div><div class="dt">${esc(dir)}${f.missing?"unavailable":fmt(f.size)}</div></a>`+
        `<a class="dl" href="${fileUrl(f,1)}" download aria-label="${IOS?"Save to Files":"Download"} ${esc(f.name)}">${ic("dl")}</a></div>`;
    }
    if(nf>MAX_ROWS)h+=`<div class="more">+ ${plural(nf-MAX_ROWS,"more file")} in the zip</div>`;
    h+=`</div>`;
  }else h=th;
  app.innerHTML=h;
  shareFiles=null;
  $("#hint").textContent=nf>1||(m.zip.preferred&&nf)?(IOS?"ZIP files are saved in Files. Tap one there to unzip it.":"The zip keeps your folder structure."):"";
}

async function copyText(t,btn){
  try{await navigator.clipboard.writeText(t)}catch(e){
    const ta=document.createElement("textarea");ta.value=t;ta.setAttribute("readonly","");
    ta.style.cssText="position:fixed;top:0;opacity:0";document.body.appendChild(ta);
    ta.select();ta.setSelectionRange(0,t.length);document.execCommand("copy");ta.remove();
  }
  btn.innerHTML=ic("check")+"Copied";setTimeout(()=>{btn.innerHTML=ic("copy")+"Copy"},1600);
}

async function share(btn){
  const lbl=btn.querySelector("span");
  const note=$("#share-note");
  if(shareFiles){
    try{await navigator.share({files:shareFiles})}
    catch(e){if(e.name!=="AbortError")lbl.textContent="Couldn't open share sheet"}
    return;
  }
  const media=data.files.filter(f=>(f.kind==="image"||f.kind==="video")&&!f.missing);
  btn.setAttribute("aria-disabled","true");
  const out=[];
  try{
    for(let k=0;k<media.length;k++){
      lbl.textContent=`Preparing ${k+1} of ${media.length}…`;
      const b=await(await fetch(fileUrl(media[k]))).blob();
      out.push(new File([b],media[k].name,{type:b.type}));
    }
  }catch(e){lbl.textContent="Couldn't prepare files";btn.removeAttribute("aria-disabled");return}
  btn.removeAttribute("aria-disabled");
  if(!navigator.canShare({files:out})){lbl.textContent="Sharing not supported here";return}
  shareFiles=out;
  lbl.textContent=IOS?"Open Share Sheet":`Share ${plural(out.length,"media item")}`;
  if(note)note.textContent="Choose Save Image or Save Video in the share sheet.";
  if(navigator.userActivation&&navigator.userActivation.isActive){
    try{await navigator.share({files:shareFiles})}
    catch(e){if(e.name!=="AbortError"&&note)note.textContent="Tap Open Share Sheet, then choose Save Image or Save Video."}
  }
}

$("#app").addEventListener("click",e=>{
  const c=e.target.closest("[data-copy]");
  if(c){copyText(data.texts[+c.dataset.copy],c);return}
  const s=e.target.closest("#share");
  if(s)share(s);
});
poll();
</script></body></html>
)PD");
    return page;
}
