#ifndef WEB_UI_H
#define WEB_UI_H

#include <Arduino.h>

// ============================================================================
// PlayMode — Embedded Web UI (Phase 6)
// ============================================================================
//
// HTML/CSS/JS interface embedded in PROGMEM.
// Single-page app with:
//   - Real-time dashboard (WebSocket)
//   - Instrument / actuator / MIDI mapping management
//   - Interactive virtual piano
//   - Power / safety monitoring
//   - Advanced configuration
//

const char WEB_UI_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PlayMode</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
:root{
  --bg:#0d1117;--bg2:#161b22;--bg3:#21262d;
  --fg:#c9d1d9;--fg2:#8b949e;--accent:#58a6ff;
  --green:#3fb950;--yellow:#d29922;--red:#f85149;
  --border:#30363d;--radius:8px;
}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
  background:var(--bg);color:var(--fg);font-size:14px;line-height:1.5}
a{color:var(--accent);text-decoration:none}

/* Layout */
.header{background:var(--bg2);border-bottom:1px solid var(--border);
  padding:12px 20px;display:flex;align-items:center;gap:12px;flex-wrap:wrap;position:relative}
.header h1{font-size:18px;font-weight:600;margin:0}
.header h1 span{color:var(--accent)}
.header .logo{position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);line-height:0}
.header .status{margin-left:auto}
.header .dot{width:8px;height:8px;border-radius:50%;display:inline-block;
  margin-right:4px;background:var(--green)}
.header .dot.off{background:var(--red)}

nav{background:var(--bg2);border-bottom:1px solid var(--border);
  display:flex;gap:0;overflow-x:auto;-webkit-overflow-scrolling:touch}
nav::-webkit-scrollbar{height:2px}
nav::-webkit-scrollbar-thumb{background:var(--border)}
nav button{background:none;border:none;color:var(--fg2);padding:10px 18px;
  cursor:pointer;font-size:13px;border-bottom:2px solid transparent;
  white-space:nowrap;transition:all .15s}
nav button:hover{color:var(--fg);background:var(--bg3)}
nav button.active{color:var(--accent);border-bottom-color:var(--accent)}

.page{display:none;padding:20px;max-width:1200px;margin:0 auto}
.page.active{display:block}

/* Cards */
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px;margin-bottom:20px}
.card{background:var(--bg2);border:1px solid var(--border);border-radius:var(--radius);padding:16px}
.card h3{font-size:13px;color:var(--fg2);margin-bottom:8px;text-transform:uppercase;letter-spacing:.5px}
.card .val{font-size:28px;font-weight:700}
.card .unit{font-size:13px;color:var(--fg2);margin-left:4px}
.card .bar{height:6px;background:var(--bg3);border-radius:3px;margin-top:8px;overflow:hidden}
.card .bar-fill{height:100%;border-radius:3px;transition:width .3s}
.card .sub{font-size:12px;color:var(--fg2);margin-top:4px}

/* Alerts */
.alert{padding:10px 14px;border-radius:var(--radius);margin-bottom:12px;
  font-size:13px;display:none;align-items:center;gap:8px}
.alert.warn{display:flex;background:#d299221a;border:1px solid #d2992233;color:var(--yellow)}
.alert.danger{display:flex;background:#f851491a;border:1px solid #f8514933;color:var(--red)}
.alert.ok{display:flex;background:#3fb9501a;border:1px solid #3fb95033;color:var(--green)}

/* Tables */
.table-responsive{overflow-x:auto;-webkit-overflow-scrolling:touch}
table{width:100%;border-collapse:collapse;margin-bottom:16px}
th,td{text-align:left;padding:8px 12px;border-bottom:1px solid var(--border)}
th{font-size:12px;color:var(--fg2);text-transform:uppercase;letter-spacing:.5px;background:var(--bg2)}
td{font-size:13px}
tr:hover td{background:var(--bg2)}
.badge{display:inline-block;padding:2px 8px;border-radius:12px;font-size:11px;font-weight:600}
.badge.on{background:#3fb95033;color:var(--green)}
.badge.off{background:var(--bg3);color:var(--fg2)}
.badge.servo{background:#58a6ff22;color:var(--accent)}
.badge.sol{background:#d2992222;color:var(--yellow)}
.inst-separator td{background:var(--bg3);font-weight:600;font-size:13px;padding:8px 10px !important;
  border-top:2px solid var(--border)}

/* Buttons — min-height 44px WCAG 2.5.5 touch target */
.btn{background:var(--bg3);border:1px solid var(--border);color:var(--fg);
  padding:6px 14px;border-radius:6px;cursor:pointer;font-size:13px;
  transition:all .15s;display:inline-flex;align-items:center;gap:6px;min-height:44px}
.btn:hover{background:var(--border);border-color:var(--fg2)}
.btn.primary{background:var(--accent);border-color:var(--accent);color:#fff}
.btn.primary:hover{opacity:.85}
.btn.danger{background:var(--red);border-color:var(--red);color:#fff}
.btn.danger:hover{opacity:.85}
.btn.sm{padding:4px 10px;font-size:12px;min-height:36px}
.btn-row{display:flex;gap:8px;margin:12px 0;flex-wrap:wrap}

/* Forms */
.form-group{margin-bottom:14px}
.form-group label{display:block;font-size:12px;color:var(--fg2);
  margin-bottom:4px;text-transform:uppercase;letter-spacing:.5px}
.form-group input,.form-group select{width:100%;background:var(--bg);
  border:1px solid var(--border);color:var(--fg);padding:8px 10px;
  border-radius:6px;font-size:13px}
.form-group input:focus,.form-group select:focus{border-color:var(--accent);outline:none}
.form-row{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.form-row.tri{grid-template-columns:1fr 1fr 1fr}
.form-select{background:var(--bg);color:var(--fg);border:1px solid var(--border);
  padding:4px 8px;border-radius:4px;font-size:13px}

/* Modal */
.modal-overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,.6);
  z-index:100;align-items:center;justify-content:center;padding:8px}
.modal-overlay.show{display:flex}
.modal{background:var(--bg2);border:1px solid var(--border);border-radius:var(--radius);
  padding:24px;width:90%;max-width:500px;max-height:90vh;overflow-y:auto}
.modal h2{font-size:16px;margin-bottom:16px}

/* Piano — wider keys for touch */
.piano-scroll-wrap{display:flex;align-items:center;gap:4px}
.piano-scroll-wrap .piano-nav{display:none;background:var(--bg3);border:none;color:var(--fg);
  font-size:20px;padding:8px 6px;border-radius:6px;cursor:pointer;flex-shrink:0;
  height:60px;align-self:center;line-height:1;touch-action:manipulation}
.piano-scroll-wrap .piano-nav:active{background:var(--accent);color:#fff}
.piano-container{overflow-x:auto;padding:12px 0;-webkit-overflow-scrolling:touch;touch-action:pan-x;flex:1;min-width:0}
.piano{--wk:40px;display:flex;position:relative;height:130px;user-select:none;touch-action:none}
.piano .white{width:var(--wk);height:130px;background:#f0f0f0;border:1px solid #999;
  border-radius:0 0 4px 4px;cursor:pointer;position:relative;z-index:1;
  display:flex;align-items:flex-end;justify-content:center;padding-bottom:4px;
  font-size:9px;color:#666;transition:background .1s;flex-shrink:0;
  -webkit-user-select:none;user-select:none;-webkit-touch-callout:none}
.piano .white:hover{background:#e0e8f0}
.piano .white.active{background:var(--accent);color:#fff}
.piano .white.muted{display:none}
.piano .black{width:calc(var(--wk) * 0.65);height:85px;background:#222;border:1px solid #000;
  border-radius:0 0 3px 3px;cursor:pointer;position:absolute;z-index:2;
  transition:background .1s;
  -webkit-user-select:none;user-select:none;-webkit-touch-callout:none}
.piano .black:hover{background:#444}
.piano .black.active{background:var(--accent)}
.piano .black.muted{display:none}

/* Section titles — flexbox for mobile alignment */
.section-title{font-size:16px;font-weight:600;margin-bottom:16px;
  padding-bottom:8px;border-bottom:1px solid var(--border);
  display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px}

/* Log container */
.log-container{overflow-y:auto;max-height:calc(100vh - 280px)}

/* Responsive — mobile (<600px) */
@media(max-width:600px){
  .page{padding:10px 8px}
  .cards{grid-template-columns:1fr 1fr}
  .form-row{grid-template-columns:1fr}
  .form-row.tri{grid-template-columns:1fr}
  nav{flex-wrap:wrap;gap:4px;padding:8px}
  nav button{padding:8px 12px;font-size:12px;flex:1;min-width:80px}
  .form-group input,.form-group select{font-size:16px}
  .header{padding:12px}
  .header h1{font-size:20px}
  .header .status{}
  .log-container{max-height:calc(100vh - 150px)}
  .modal{padding:14px;width:95%}
  .section-title{font-size:14px;flex-wrap:wrap;gap:6px}
  .btn-row{flex-wrap:wrap;gap:6px}
  .table-responsive{overflow-x:auto;-webkit-overflow-scrolling:touch}
  table{font-size:12px;min-width:500px}
  table th,table td{padding:6px 4px;white-space:nowrap}
  .piano-container{overflow-x:auto;-webkit-overflow-scrolling:touch;padding-bottom:8px}
  .piano-scroll-wrap .piano-nav{display:block}
  .piano{--wk:44px;min-width:auto;height:120px}
  .piano .white{height:120px;font-size:8px}
  .piano .black{height:75px}
  .welcome h2{font-size:22px}
  .welcome-steps{flex-direction:column;align-items:center}
  .welcome-step{max-width:100%;min-width:auto}
  .section-collapse-toggle{font-size:13px;padding:10px 12px}
}
@media(max-width:380px){
  .cards{grid-template-columns:1fr}
  .piano{--wk:40px;height:110px}
  .piano .white{height:110px;font-size:7px}
  .piano .black{height:68px}
  nav button{font-size:11px;padding:6px 8px}
}
@media(min-width:601px) and (max-width:768px){
  .cards{grid-template-columns:repeat(auto-fit,minmax(160px,1fr))}
  .form-row.tri{grid-template-columns:1fr 1fr}
}
@media(max-width:768px) and (orientation:landscape){
  .log-container{max-height:calc(100vh - 120px)}
  .modal{max-height:95vh}
}
/* Gear settings dropdown */
.gear-btn{background:none;border:none;color:var(--fg2);font-size:20px;cursor:pointer;
  padding:6px 10px;border-radius:6px;transition:all .15s;min-width:44px;min-height:44px;
  display:flex;align-items:center;justify-content:center}
.gear-btn:hover{color:var(--fg);background:var(--bg3)}
/* Help text under form fields */
.help{font-size:11px;color:var(--fg2);margin-top:2px;line-height:1.3}

/* Angle preview for servo */
.angle-preview{background:var(--bg);border:1px solid var(--border);border-radius:var(--radius);
  padding:12px;margin:12px 0;text-align:center}
.angle-preview svg{display:block;margin:0 auto}
.angle-info{display:flex;justify-content:space-around;margin-top:8px;font-size:12px;color:var(--fg2)}
.angle-info span{color:var(--fg);font-weight:600}

/* Mic status indicator */
.mic-status{display:flex;align-items:center;gap:8px;padding:10px 14px;border-radius:var(--radius);
  margin-bottom:16px;font-size:13px}
.mic-status.ok{background:#3fb9501a;border:1px solid #3fb95033;color:var(--green)}
.mic-status.no{background:#f851491a;border:1px solid #f8514933;color:var(--red)}

/* Wizard steps */
.wiz-steps{display:flex;gap:4px;margin-bottom:16px;align-items:flex-start}
.wiz-step-item{display:flex;flex-direction:column;align-items:center;gap:2px;flex:1}
.wiz-dot{width:28px;height:28px;border-radius:50%;background:var(--bg3);color:var(--fg2);
  display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:600}
.wiz-dot.active{background:var(--accent);color:#fff}
.wiz-dot.done{background:var(--green);color:#fff}
.wiz-step-label{font-size:10px;color:var(--fg2);text-align:center;white-space:nowrap}
.wiz-step-item.active .wiz-step-label{color:var(--accent);font-weight:600}
.wiz-connector{flex:1;height:2px;background:var(--bg3);align-self:center;margin-top:13px;min-width:12px}
.wiz-connector.done{background:var(--green)}
.wiz-panel{min-height:120px}

/* Inline note input in actuator table */
.note-input{width:56px;background:var(--bg);border:1px solid var(--border);color:var(--fg);
  padding:2px 6px;border-radius:4px;font-size:12px;text-align:center}
.note-input:focus{border-color:var(--accent);outline:none}
.note-label{font-size:11px;color:var(--fg2);margin-left:4px}

/* Expert collapsible sections */
.expert-section{margin:12px 0}
.expert-toggle{background:none;border:1px dashed var(--border);color:var(--fg2);padding:8px 12px;
  border-radius:6px;cursor:pointer;font-size:12px;width:100%;text-align:left;
  display:flex;align-items:center;gap:6px;transition:all .15s}
.expert-toggle:hover{color:var(--fg);background:var(--bg3)}
.expert-toggle::before{content:'\25B6';font-size:10px;transition:transform .2s;display:inline-block}
.expert-toggle.open::before{transform:rotate(90deg)}
.expert-body{display:none;padding:12px 0 0}
.expert-body.open{display:block}

/* Wizard note table */
.wiz-note-table{max-height:200px;overflow-y:auto;border:1px solid var(--border);
  border-radius:var(--radius);margin:8px 0}
.wiz-note-table table{margin:0}
.wiz-note-table td{padding:4px 8px;font-size:12px}
.wiz-note-table th{padding:4px 8px;position:sticky;top:0;z-index:1}

/* Welcome page (first-run) */
.welcome{text-align:center;padding:40px 20px;max-width:600px;margin:0 auto}
.welcome h2{font-size:28px;font-weight:700;margin-bottom:8px}
.welcome h2 span{color:var(--accent)}
.welcome .subtitle{color:var(--fg2);font-size:15px;margin-bottom:36px;line-height:1.6}
.welcome-steps{display:flex;gap:20px;justify-content:center;margin-bottom:36px;flex-wrap:wrap}
.welcome-step{background:var(--bg2);border:1px solid var(--border);border-radius:var(--radius);
  padding:20px 16px;flex:1;min-width:150px;max-width:180px}
.welcome-step .step-num{width:36px;height:36px;border-radius:50%;background:var(--accent);color:#fff;
  display:inline-flex;align-items:center;justify-content:center;font-size:16px;font-weight:700;margin-bottom:10px}
.welcome-step h3{font-size:14px;font-weight:600;margin-bottom:4px}
.welcome-step p{font-size:12px;color:var(--fg2);line-height:1.4}
.welcome .btn-big{padding:14px 32px;font-size:16px;font-weight:600;min-height:52px;border-radius:8px}

/* Collapsible sections for merged content */
.section-collapse{margin:20px 0}
.section-collapse-toggle{background:var(--bg2);border:1px solid var(--border);color:var(--fg);
  padding:12px 16px;border-radius:var(--radius);cursor:pointer;font-size:14px;font-weight:600;
  width:100%;text-align:left;display:flex;align-items:center;justify-content:space-between;
  transition:all .15s}
.section-collapse-toggle:hover{background:var(--bg3);border-color:var(--fg2)}
.section-collapse-toggle .toggle-count{font-size:12px;color:var(--fg2);font-weight:400}
.section-collapse-toggle::after{content:'\25B6';font-size:11px;color:var(--fg2);transition:transform .2s}
.section-collapse-toggle.open::after{transform:rotate(90deg)}
.section-collapse-body{display:none;padding:16px 0 0}
.section-collapse-body.open{display:block}
</style>
</head>
<body>

<!-- Header -->
<div class="header">
  <h1><span>Play</span>Mode</h1>
  <div class="logo" aria-label="B-infini-P">
    <svg viewBox="0 0 80 32" width="80" height="32">
      <text x="4" y="23" font-size="18" font-weight="700" fill="currentColor">B</text>
      <circle cx="40" cy="16" r="13" fill="none" stroke="var(--accent)" stroke-width="2"/>
      <text x="40" y="22" font-size="18" font-weight="700" fill="var(--accent)" text-anchor="middle">&infin;</text>
      <text x="64" y="23" font-size="18" font-weight="700" fill="currentColor">P</text>
    </svg>
  </div>
  <div class="status">
    <span class="dot" id="ws-dot"></span>
  </div>
  <button class="gear-btn" onclick="showPage('settings')" title="System settings">&#9881;</button>
</div>

<!-- Navigation -->
<nav id="main-nav">
  <button class="active" onclick="showPage('instrument')">Instrument</button>
  <button onclick="showPage('midi')">MIDI</button>
  <button onclick="showPage('actuators')">Actuators</button>
  <button onclick="showPage('calibration')" id="nav-cal" style="display:none">Calibration</button>
</nav>

<!-- ============ WELCOME (First-run) ============ -->
<div class="page" id="page-welcome">
  <div class="welcome">
    <h2>Welcome to Midi <span>B&infin;p</span></h2>
    <p class="subtitle">Turn any object into a MIDI musical instrument.<br>
    Servos, solenoids, percussion... anything is possible.</p>
    <div class="welcome-steps">
      <div class="welcome-step">
        <div class="step-num">1</div>
        <h3>Create</h3>
        <p>Define your instrument and its actuators</p>
      </div>
      <div class="welcome-step">
        <div class="step-num">2</div>
        <h3>Connect</h3>
        <p>Plug in your servos or solenoids via PCA9685</p>
      </div>
      <div class="welcome-step">
        <div class="step-num">3</div>
        <h3>Play</h3>
        <p>Send MIDI and let the music do its thing</p>
      </div>
    </div>
    <button class="btn primary btn-big" onclick="openWizard()">Create my first instrument</button>
    <p style="color:var(--fg2);font-size:12px;margin-top:16px">Or <a href="#" onclick="showPage('instrument');return false">skip to manual configuration</a></p>
  </div>
</div>

<!-- ============ INSTRUMENT (Instruments + independent pianos) ============ -->
<div class="page active" id="page-instrument">
  <div id="alert-zone"></div>

  <div class="section-title"><span>My instruments</span>
    <div style="display:flex;gap:8px">
      <button class="btn primary sm" onclick="openWizard()">+ Wizard</button>
      <button class="btn sm" onclick="openInstrumentModal()">+ Manual</button>
    </div>
  </div>
  <div class="table-responsive">
  <table>
    <thead><tr><th>Name</th><th>Channel</th><th>Type</th><th>Actuators</th><th>Status</th><th>Actions</th></tr></thead>
    <tbody id="home-instruments-table"><tr><td colspan="6" style="color:var(--fg2)">Loading...</td></tr></tbody>
  </table>
  </div>

  <!-- One piano per instrument (generated dynamically) -->
  <div id="pianos-container"></div>
</div>

<!-- ============ SETTINGS (unified gear menu page) ============ -->
<div class="page" id="page-settings">

  <!-- Monitoring -->
  <div class="section-title">Monitoring</div>
  <div class="cards">
    <div class="card">
      <h3>MIDI</h3>
      <div class="val" id="d-midi-recv">0</div>
      <div class="sub">Routed: <span id="d-midi-routed">0</span> | Rejected: <span id="d-midi-unmapped">0</span></div>
    </div>
    <div class="card">
      <h3>Polyphony</h3>
      <div class="val"><span id="d-active">0</span><span class="unit">/ <span id="p-poly-max">12</span></span></div>
      <div class="bar"><div class="bar-fill" id="p-total-bar" style="width:0%;background:var(--green)"></div></div>
      <div class="sub">Rejected: <span id="p-rejected">0</span></div>
    </div>
    <div class="card">
      <h3>Scheduler</h3>
      <div class="val" id="d-sched-queued">0</div>
      <div class="sub">queued | <span id="d-sched-processed">0</span> processed</div>
    </div>
    <div class="card">
      <h3>WiFi</h3>
      <div class="val" id="d-wifi-rssi">-</div>
      <div class="sub" id="d-wifi-status">-</div>
    </div>
  </div>

  <!-- Polyphony & Safety -->
  <div class="section-title" style="margin-top:24px">Polyphony &amp; Safety</div>
  <div id="safety-alert-zone"></div>
  <div class="form-row" style="margin-bottom:12px">
    <div class="form-group">
      <label>Max polyphony</label>
      <input type="number" id="pw-poly" value="12" min="1" max="64">
      <div class="help">Maximum number of simultaneously active actuators</div>
    </div>
    <div class="form-group" style="display:flex;align-items:center;gap:8px;padding-top:18px">
      <button class="btn primary sm" onclick="savePowerBudget()">Apply</button>
    </div>
  </div>
  <div class="cards" style="grid-template-columns:repeat(auto-fit,minmax(120px,1fr));margin-bottom:12px">
    <div class="card">
      <h3>Kill Switch</h3>
      <div class="val" id="s-kill" style="color:var(--green)">OFF</div>
    </div>
    <div class="card">
      <h3>Degradation</h3>
      <div class="val" id="s-degrad" style="color:var(--green)">No</div>
    </div>
  </div>
  <div class="btn-row" style="margin-bottom:12px">
    <button class="btn danger sm" onclick="toggleKillSwitch(true)">KILL SWITCH ON</button>
    <button class="btn sm" onclick="toggleKillSwitch(false)">Kill Switch Off</button>
  </div>

  <div class="section-collapse">
    <button class="section-collapse-toggle" onclick="toggleCollapse(this)">
      <span>Safety limits (advanced)</span>
    </button>
    <div class="section-collapse-body">
      <div class="form-row tri">
        <div class="form-group">
          <label>Duty cycle max (%)</label>
          <input type="number" id="sf-duty" value="80" min="10" max="100">
        </div>
        <div class="form-group">
          <label>Max frequency (Hz)</label>
          <input type="number" id="sf-freq" value="50" min="1" max="200">
        </div>
        <div class="form-group">
          <label>Watchdog timeout (ms)</label>
          <input type="number" id="sf-watchdog" value="5000" min="1000" max="30000">
        </div>
      </div>
      <button class="btn primary sm" onclick="saveSafetyConfig()">Apply limits</button>
    </div>
  </div>

  <!-- Logs -->
  <div class="section-title" style="margin-top:24px"><span>System log</span>
    <div style="display:flex;gap:6px">
      <button class="btn sm" onclick="loadLogs()">Refresh</button>
      <button class="btn sm" onclick="clearLogs()">Clear</button>
    </div>
  </div>
  <div style="display:flex;gap:8px;margin-bottom:10px;flex-wrap:wrap;align-items:center;font-size:12px">
    <select id="log-level-filter" onchange="renderLogs()" class="form-select" style="font-size:12px">
      <option value="0">DEBUG+</option>
      <option value="1">INFO+</option>
      <option value="2">WARN+</option>
      <option value="3">ERROR+</option>
    </select>
    <select id="log-cat-filter" onchange="renderLogs()" class="form-select" style="font-size:12px">
      <option value="-1">All</option>
      <option value="0">System</option>
      <option value="1">MIDI</option>
      <option value="2">Scheduler</option>
      <option value="3">Safety</option>
      <option value="4">Power</option>
      <option value="5">Calibration</option>
      <option value="6">Test</option>
    </select>
    <label style="margin-left:auto;display:flex;align-items:center;gap:4px"><input type="checkbox" id="log-autoscroll" checked> Auto-scroll</label>
  </div>
  <div class="log-container">
    <div class="table-responsive">
    <table>
      <thead><tr><th style="width:80px">Time</th><th style="width:50px">Level</th><th style="width:65px">Cat</th><th>Message</th></tr></thead>
      <tbody id="log-table"><tr><td colspan="4" style="color:var(--fg2)">Loading...</td></tr></tbody>
    </table>
    </div>
  </div>
  <div id="log-count-info" style="color:var(--fg2);font-size:12px;margin-top:6px;text-align:right"></div>

  <!-- WiFi -->
  <div class="section-title" style="margin-top:24px">WiFi Connection</div>
  <div class="form-row">
    <div class="form-group">
      <label>Network SSID</label>
      <input type="text" id="set-ssid" maxlength="32" placeholder="WiFi name">
    </div>
    <div class="form-group">
      <label>Password</label>
      <input type="password" id="set-pass" maxlength="64">
    </div>
  </div>
  <div class="form-row">
    <div class="form-group">
      <label>Hostname</label>
      <input type="text" id="set-hostname" value="play-mode" maxlength="31">
      <div class="help">Accessible via hostname.local on the network</div>
    </div>
    <div class="form-group">
      <label>AP Fallback</label>
      <select id="set-ap-fallback">
        <option value="1">Yes &mdash; creates an access point if WiFi fails</option>
        <option value="0">No</option>
      </select>
    </div>
  </div>
  <button class="btn primary" onclick="saveWiFiConfig()">Save WiFi</button>

  <!-- I&sup2;C Bus -->
  <div class="section-collapse" style="margin-top:24px">
    <button class="section-collapse-toggle" onclick="toggleCollapse(this)">
      <span>I&sup2;C Bus (advanced)</span>
    </button>
    <div class="section-collapse-body">
      <div class="table-responsive">
      <table>
        <thead><tr><th>Bus</th><th>SDA</th><th>SCL</th><th>OE</th><th>Freq I&sup2;C</th><th>Freq PWM</th><th>PCA detected</th><th>Status</th></tr></thead>
        <tbody id="buses-table"><tr><td colspan="8" style="color:var(--fg2)">Loading...</td></tr></tbody>
      </table>
      </div>
      <button class="btn" onclick="scanI2C()">Scan I&sup2;C bus</button>
    </div>
  </div>

  <!-- Config -->
  <div class="section-title" style="margin-top:24px">Configuration</div>
  <div class="btn-row">
    <button class="btn primary" onclick="saveConfig()">Save to flash</button>
    <button class="btn danger" onclick="confirmResetDefaults()">Reset</button>
  </div>
  <div class="sub" style="margin-top:8px">Config version: <span id="set-version">-</span></div>
</div>

<!-- ============ MIDI (MIDI Inputs + Received Messages) ============ -->
<div class="page" id="page-midi">
  <div class="section-title">MIDI Inputs</div>
  <p style="color:var(--fg2);font-size:12px;margin-bottom:12px">Choose how PlayMode receives MIDI messages. Multiple inputs can be active at the same time.</p>
  <div class="cards" style="margin-bottom:16px">
    <div class="card">
      <h3>MIDI Cable (DIN / TRS)</h3>
      <label><input type="checkbox" id="midi-serial" onchange="updateMidiConfig()"> Active</label>
      <div class="sub">Classic wired connection via 5-pin MIDI jack or TRS jack.</div>
      <div class="sub" style="margin-top:4px;font-size:11px;color:var(--fg2)">GPIO <span id="midi-rx-pin">4</span> &mdash; 31250 baud</div>
    </div>
    <div class="card">
      <h3>WiFi &mdash; direct send (UDP)</h3>
      <label><input type="checkbox" id="midi-udp" onchange="updateMidiConfig()"> Active</label>
      <div class="sub">Send MIDI messages from software to PlayMode's IP address. Simple but without synchronization.</div>
      <div class="sub" style="margin-top:4px;font-size:11px;color:var(--fg2)">Port <span id="midi-udp-port">5004</span> &mdash; Requires WiFi</div>
    </div>
    <div class="card">
      <h3>WiFi &mdash; Apple / RTP-MIDI</h3>
      <label><input type="checkbox" id="midi-rtp" onchange="updateMidiConfig()"> Active</label>
      <div class="sub">Compatible with macOS, iOS, rtpMIDI (Windows). Appears automatically in MIDI software. Synchronized.</div>
      <div class="sub" style="margin-top:4px;font-size:11px;color:var(--fg2)">Port <span id="midi-rtp-port">5004</span> &mdash; Requires WiFi</div>
    </div>
    <div class="card">
      <h3>Jitter Buffer</h3>
      <div class="val"><span id="midi-jitter-val">30</span><span class="unit">ms</span></div>
      <input type="range" id="midi-jitter" min="10" max="80" value="30" style="width:100%;margin-top:8px"
        oninput="document.getElementById('midi-jitter-val').textContent=this.value"
        onchange="updateMidiConfig()">
      <div class="help">Anti-jitter buffer for network MIDI (UDP / RTP). Increase if notes arrive out of order.</div>
    </div>
  </div>

  <!-- Latest received MIDI messages -->
  <div class="section-title" style="margin-top:24px"><span>Received MIDI messages</span>
    <div style="display:flex;gap:6px;align-items:center">
      <label style="font-size:12px;display:flex;align-items:center;gap:4px;color:var(--fg2)"><input type="checkbox" id="midi-log-pause"> Pause</label>
      <button class="btn sm" onclick="clearMidiLog()">Clear</button>
    </div>
  </div>
  <div class="table-responsive" style="max-height:320px;overflow-y:auto" id="midi-log-scroll">
  <table>
    <thead><tr><th style="width:70px">Time</th><th>Source</th><th>Channel</th><th>Type</th><th>Data</th><th>Routed</th></tr></thead>
    <tbody id="midi-log-table"><tr><td colspan="6" style="color:var(--fg2)">Waiting for MIDI messages...</td></tr></tbody>
  </table>
  </div>
  <div id="midi-log-count" style="color:var(--fg2);font-size:11px;margin-top:4px;text-align:right"></div>
</div>

<!-- (Config is now in the unified Settings page) -->

<!-- ============ MODALS ============ -->

<!-- Modal Confirm (replaces native confirm/alert) -->
<div class="modal-overlay" id="modal-confirm">
  <div class="modal" style="max-width:380px;text-align:center">
    <div id="confirm-icon" style="font-size:32px;margin-bottom:8px"></div>
    <h2 id="confirm-title" style="text-align:center">Confirm</h2>
    <p id="confirm-message" style="color:var(--fg2);margin-bottom:20px;white-space:pre-line"></p>
    <div class="btn-row" id="confirm-buttons" style="justify-content:center"></div>
  </div>
</div>

<!-- Modal Instrument -->
<div class="modal-overlay" id="modal-instrument">
  <div class="modal">
    <h2 id="modal-inst-title">New instrument</h2>
    <div class="form-group">
      <label>Instrument name</label>
      <input type="text" id="mi-name" maxlength="31" placeholder="E.g.: Xylophone, Snare drum...">
      <div class="help">Free-form name to identify this instrument in the interface</div>
    </div>
    <div class="form-row">
      <div class="form-group">
        <label>MIDI Channel (0=Omni, 1-16)</label>
        <input type="number" id="mi-channel" min="0" max="16" value="0">
        <div class="help">0 = listens on all channels, 1-16 = specific channel</div>
      </div>
      <div class="form-group">
        <label>Bus I&sup2;C</label>
        <select id="mi-bus"><option value="0">Bus 0 (Servos)</option><option value="1">Bus 1 (Solenoids)</option></select>
        <div class="help">Physical bus to which the actuators are connected</div>
      </div>
    </div>
    <div class="form-row">
      <div class="form-group">
        <label>Latency (ms)</label>
        <input type="number" id="mi-latency" value="10" min="0" max="500">
        <div class="help">Compensation delay between MIDI reception and triggering</div>
      </div>
      <div class="form-group">
        <label>Microphone auto-calibration</label>
        <select id="mi-autocal"><option value="0">No</option><option value="1">Yes</option></select>
        <div class="help">Automatic latency measurement via I&sup2;S microphone</div>
      </div>
    </div>
    <div class="btn-row">
      <button class="btn primary" onclick="saveInstrument()">Save</button>
      <button class="btn" onclick="closeModal('modal-instrument')">Cancel</button>
    </div>
  </div>
</div>

<!-- Modal Actuator -->
<div class="modal-overlay" id="modal-actuator">
  <div class="modal">
    <h2 id="modal-act-title">New actuator</h2>
    <div class="form-row">
      <div class="form-group">
        <label>Actuator ID</label>
        <input type="number" id="ma-id" min="0" max="31" value="0">
        <div class="help">Unique identifier (0-31), automatically assigned</div>
      </div>
      <div class="form-group">
        <label>Actuator type</label>
        <select id="ma-type" onchange="toggleActuatorFields()"><option value="0">Servo motor</option><option value="1">Solenoid</option></select>
        <div class="help">Servo = rotary motion | Solenoid = linear strike</div>
      </div>
    </div>
    <div class="form-row">
      <div class="form-group">
        <label>Bus I&sup2;C</label>
        <select id="ma-bus"><option value="0">Bus 0</option><option value="1">Bus 1</option></select>
        <div class="help">Physical I&sup2;C bus (PWM frequency configurable in MIDI &gt; I&sup2;C)</div>
      </div>
      <div class="form-group">
        <label>PCA9685 board</label>
        <select id="ma-pca"><option value="64">0x40 (board 1)</option><option value="65">0x41 (board 2)</option><option value="66">0x42 (board 3)</option><option value="67">0x43 (board 4)</option></select>
        <div class="help">I&sup2;C address of the PWM board (16 channels each)</div>
      </div>
    </div>
    <div class="form-group">
      <label>PCA Channel (0-15)</label>
      <input type="number" id="ma-ch" min="0" max="15" value="0">
      <div class="help">PWM output on the board (auto-incremented)</div>
    </div>

    <div class="expert-section">
      <button type="button" class="expert-toggle" onclick="toggleExpert(this)">Advanced settings</button>
      <div class="expert-body">
        <div class="form-group">
          <label>Latency (ms)</label>
          <input type="number" id="ma-latency" min="0" max="500" value="10">
          <div class="help">Mechanical compensation delay (default: 10ms)</div>
        </div>
      </div>
    </div>

    <!-- Servo fields -->
    <div id="servo-fields">
      <div style="border-top:1px solid var(--border);margin:12px 0;padding-top:12px">
        <div style="font-size:13px;font-weight:600;margin-bottom:8px">Servo settings</div>
      </div>
      <div class="form-row">
        <div class="form-group">
          <label>Play mode</label>
          <select id="ma-servo-behavior" onchange="toggleServoDirection()"><option value="0">Strike (quick back-and-forth)</option><option value="1">Alternate (A/B toggle)</option><option value="2">Strum (continuous motion)</option><option value="3">Key (hold down)</option></select>
          <div class="help">Strike: percussion | Alternate: toggle between 2 positions | Strum: back-and-forth | Key: hold</div>
        </div>
        <div class="form-group" id="servo-direction-group">
          <label>Strike direction</label>
          <select id="ma-hit-reverse" onchange="updateAnglePreview()">
            <option value="0">Clockwise (+) &mdash; rest &rarr; rest + amplitude</option>
            <option value="1">Counter-clockwise (&minus;) &mdash; rest &rarr; rest &minus; amplitude</option>
          </select>
          <div class="help">Direction of motion relative to the rest angle</div>
        </div>
      </div>
      <div id="servo-standard-fields">
        <div class="form-row">
          <div class="form-group">
            <label>Rest angle (&deg;)</label>
            <input type="number" id="ma-angle-init" min="0" max="180" value="90" oninput="updateAnglePreview()">
            <div class="help">Arm position at rest (0&deg; to 180&deg;)</div>
          </div>
          <div class="form-group">
            <label>Amplitude (&deg;)</label>
            <input type="number" id="ma-amplitude" min="0" max="180" value="45" oninput="updateAnglePreview()">
            <div class="help">Range of motion in degrees</div>
          </div>
        </div>
      </div>
      <div id="servo-alterne-fields" style="display:none">
        <div class="form-row">
          <div class="form-group">
            <label>Angle A (&deg;)</label>
            <input type="number" id="ma-angle-a-alt" min="0" max="180" value="90" oninput="updateAnglePreview()">
            <div class="help">First toggle position</div>
          </div>
          <div class="form-group">
            <label>Angle B (&deg;)</label>
            <input type="number" id="ma-angle-b" min="0" max="180" value="120" oninput="updateAnglePreview()">
            <div class="help">Second toggle position</div>
          </div>
        </div>
      </div>
      <div class="form-group">
        <label>Movement duration (ms)</label>
        <input type="number" id="ma-speed" min="10" max="2000" value="150">
        <div class="help">Time for a single stroke (10=fast, 500=slow)</div>
      </div>
      <!-- Angle visual preview -->
      <div class="angle-preview" id="angle-preview"></div>
    </div>

    <!-- Solenoid fields -->
    <div id="solenoid-fields" style="display:none">
      <div style="border-top:1px solid var(--border);margin:12px 0;padding-top:12px">
        <div style="font-size:13px;font-weight:600;margin-bottom:8px">Solenoid settings</div>
      </div>
      <div class="form-group">
        <label>Strike mode</label>
        <select id="ma-sol-behavior" onchange="toggleHitHoldFields()"><option value="0">Strike (short pulse)</option><option value="1">Hit-and-Hold (strike then hold)</option></select>
        <div class="help">Strike: short pulse | Hit-and-Hold: strong strike then soft hold</div>
      </div>
      <div class="form-row">
        <div class="form-group">
          <label>Min pulse (ms) &mdash; velocity 1</label>
          <input type="number" id="ma-pulse-min" min="2" max="100" value="5">
          <div class="help">Shortest duration (low velocity, soft strike)</div>
        </div>
        <div class="form-group">
          <label>Max pulse (ms) &mdash; velocity 127</label>
          <input type="number" id="ma-pulse-max" min="5" max="200" value="30">
          <div class="help">Longest duration (high velocity, hard strike)</div>
        </div>
      </div>
      <div class="form-row">
        <div class="form-group">
          <label>Attack PWM (0-4095)</label>
          <input type="number" id="ma-pwm-init" min="0" max="4095" value="4095">
          <div class="help">Initial strike power. 4095 = maximum</div>
        </div>
      </div>
      <div id="hit-hold-fields" class="expert-section" style="display:none">
        <button type="button" class="expert-toggle" onclick="toggleExpert(this)">Hit-and-Hold (advanced)</button>
        <div class="expert-body">
          <div class="form-row">
            <div class="form-group">
              <label>Hold PWM (0-4095)</label>
              <input type="number" id="ma-pwm-hold" min="0" max="4095" value="2048">
              <div class="help">Reduced power after the strike</div>
            </div>
            <div class="form-group">
              <label>Transition ramp (ms)</label>
              <input type="number" id="ma-ramp" min="10" max="500" value="50">
              <div class="help">Duration of attack &rarr; hold transition</div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <div class="btn-row">
      <button class="btn primary" onclick="saveActuator()">Save</button>
      <button class="btn" onclick="closeModal('modal-actuator')">Cancel</button>
    </div>
  </div>
</div>

<!-- Modal Add/Edit CC Mapping -->
<div class="modal-overlay" id="modal-cc">
  <div class="modal" style="max-width:460px">
    <h2 id="modal-cc-title">Add CC Mapping</h2>
    <div class="form-row">
      <div class="form-group">
        <label>CC Number (0-127)</label>
        <input type="number" id="cc-num" min="0" max="127" value="1">
        <div class="help">E.g.: CC1 = Modulation, CC7 = Volume, CC11 = Expression</div>
      </div>
      <div class="form-group">
        <label>Control type</label>
        <select id="cc-category" class="form-select" onchange="toggleCCCategory()">
          <option value="position">Servo position (direct movement)</option>
          <option value="modifier">Modifier (amplitude / speed)</option>
        </select>
      </div>
    </div>

    <!-- Category: Position — free servo -->
    <div id="cc-cat-position">
      <div class="form-group">
        <label>Dedicated servo</label>
        <select id="cc-actuator-free" class="form-select"></select>
        <div class="help">Only servos not assigned to an instrument are listed</div>
      </div>
      <div id="cc-create-servo-hint" style="display:none;margin:-4px 0 10px">
        <span style="color:var(--yellow);font-size:12px">No free servo.</span>
        <button class="btn sm" onclick="quickCreateServoForCC()" style="margin-left:6px">Create a servo</button>
      </div>
      <div class="form-row">
        <div class="form-group">
          <label>Angle min (&deg;)</label>
          <input type="number" id="cc-pos-min" min="0" max="180" value="0">
          <div class="help">Position when CC = 0</div>
        </div>
        <div class="form-group">
          <label>Angle max (&deg;)</label>
          <input type="number" id="cc-pos-max" min="0" max="180" value="180">
          <div class="help">Position when CC = 127</div>
        </div>
      </div>
    </div>

    <!-- Category: Modifier — instrument servo -->
    <div id="cc-cat-modifier" style="display:none">
      <div class="form-group">
        <label>Instrument servo</label>
        <select id="cc-actuator-inst" class="form-select" onchange="updateCCServoInfo()"></select>
        <div class="help">Servos assigned to the selected instrument</div>
      </div>
      <div id="cc-servo-info" style="display:none;background:var(--bg3);border-radius:6px;padding:8px 10px;margin-bottom:10px;font-size:12px;color:var(--fg2)"></div>
      <div class="form-group">
        <label>Parameter to modify</label>
        <select id="cc-mod-target" class="form-select" onchange="updateCCModRangeHints()">
          <option value="1">Amplitude &mdash; strike range (&deg;)</option>
          <option value="2">Speed &mdash; movement duration (ms)</option>
        </select>
        <div class="help" id="cc-mod-help">Modifies the strike range in real time for upcoming notes</div>
      </div>
      <div class="form-row">
        <div class="form-group">
          <label>Min value <span id="cc-mod-min-unit">(&deg;)</span></label>
          <input type="number" id="cc-mod-min" value="0">
          <div class="help">Value when CC = 0</div>
        </div>
        <div class="form-group">
          <label>Max value <span id="cc-mod-max-unit">(&deg;)</span></label>
          <input type="number" id="cc-mod-max" value="180">
          <div class="help">Value when CC = 127</div>
        </div>
      </div>
    </div>

    <div class="btn-row">
      <button class="btn primary" id="cc-save-btn" onclick="saveCC()">Add</button>
      <button class="btn" onclick="closeModal('modal-cc')">Cancel</button>
    </div>
  </div>
</div>

<!-- Wizard Instrument -->
<div class="modal-overlay" id="modal-wizard">
  <div class="modal" style="max-width:550px">
    <h2>Instrument creation wizard</h2>
    <div class="wiz-steps" id="wiz-steps">
      <div class="wiz-step-item active" id="wiz-item-1"><div class="wiz-dot active" id="wiz-dot-1">1</div><div class="wiz-step-label">Identity</div></div>
      <div class="wiz-connector" id="wiz-conn-1"></div>
      <div class="wiz-step-item" id="wiz-item-2"><div class="wiz-dot" id="wiz-dot-2">2</div><div class="wiz-step-label">Type</div></div>
      <div class="wiz-connector" id="wiz-conn-2"></div>
      <div class="wiz-step-item" id="wiz-item-3"><div class="wiz-dot" id="wiz-dot-3">3</div><div class="wiz-step-label">Notes</div></div>
      <div class="wiz-connector" id="wiz-conn-3"></div>
      <div class="wiz-step-item" id="wiz-item-4"><div class="wiz-dot" id="wiz-dot-4">4</div><div class="wiz-step-label">Creation</div></div>
    </div>

    <!-- Step 1: Identity -->
    <div id="wiz-step-1" class="wiz-panel">
      <div style="font-size:14px;font-weight:600;margin-bottom:12px">Instrument identity</div>
      <div class="form-group">
        <label>Name</label>
        <input type="text" id="wiz-name" maxlength="31" placeholder="E.g.: Xylophone, Snare drum...">
      </div>
      <div class="form-group">
        <label>MIDI Channel (0=Omni, 1-16)</label>
        <input type="number" id="wiz-channel" min="0" max="16" value="1">
        <div class="help">0 = all channels, 1-16 = specific channel</div>
      </div>
    </div>

    <!-- Step 2: Actuator type -->
    <div id="wiz-step-2" class="wiz-panel" style="display:none">
      <div style="font-size:14px;font-weight:600;margin-bottom:12px">Actuator type</div>
      <div class="form-group">
        <label>Type</label>
        <select id="wiz-type" onchange="wizUpdateBehaviors()">
          <option value="0">Servo motor (rotary motion)</option>
          <option value="1">Solenoid (linear strike)</option>
        </select>
        <div class="help">All actuators will use this type</div>
      </div>
      <div class="form-group">
        <label>Play mode</label>
        <select id="wiz-behavior"></select>
        <div class="help">Default behavior for all actuators</div>
      </div>
    </div>

    <!-- Step 3: Notes + PCA -->
    <div id="wiz-step-3" class="wiz-panel" style="display:none">
      <div style="font-size:14px;font-weight:600;margin-bottom:12px">MIDI note assignment</div>
      <div class="form-row tri">
        <div class="form-group">
          <label>Actuators</label>
          <input type="number" id="wiz-count" min="1" max="64" value="8" oninput="wizBuildNoteTable()">
        </div>
        <div class="form-group">
          <label>Scale</label>
          <select id="wiz-scale" onchange="wizBuildNoteTable()">
            <option value="chromatic">Chromatic (semitones)</option>
            <option value="major">Major (C D E F G A B)</option>
            <option value="pentatonic">Pentatonic (5 notes)</option>
          </select>
        </div>
        <div class="form-group">
          <label>Start note</label>
          <input type="number" id="wiz-start-note" min="0" max="127" value="48" oninput="wizBuildNoteTable()">
          <div class="help">C3=48, C4=60</div>
        </div>
      </div>
      <div class="help" style="margin-bottom:4px">Edit each note individually if needed:</div>
      <div class="wiz-note-table" id="wiz-note-table"></div>
      <div class="expert-section">
        <button type="button" class="expert-toggle" onclick="toggleExpert(this)">Advanced PCA settings</button>
        <div class="expert-body">
          <div class="form-row">
            <div class="form-group">
              <label>Starting PCA board</label>
              <select id="wiz-pca">
                <option value="64">0x40 (board 1)</option>
                <option value="65">0x41 (board 2)</option>
                <option value="66">0x42 (board 3)</option>
                <option value="67">0x43 (board 4)</option>
              </select>
            </div>
            <div class="form-group">
              <label>Starting PCA channel</label>
              <input type="number" id="wiz-start-ch" min="0" max="15" value="0">
              <div class="help">Auto-incremented</div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Step 4: Review -->
    <div id="wiz-step-4" class="wiz-panel" style="display:none">
      <div style="font-size:14px;font-weight:600;margin-bottom:12px">Summary</div>
      <div id="wiz-summary" style="background:var(--bg);border:1px solid var(--border);border-radius:var(--radius);padding:12px;font-size:13px;line-height:1.8"></div>
    </div>

    <div class="btn-row" style="margin-top:16px">
      <button class="btn" id="wiz-prev" onclick="wizPrev()" style="display:none">&larr; Previous</button>
      <button class="btn primary" id="wiz-next" onclick="wizNext()">Next &rarr;</button>
      <button class="btn" onclick="closeModal('modal-wizard')">Cancel</button>
    </div>
  </div>
</div>

<!-- ============ CALIBRATION (Actuators + CC + Acoustic Calibration) ============ -->
<!-- ============ ACTUATORS (table + CC mapping) ============ -->
<div class="page" id="page-actuators">

  <!-- === Actuators === -->
  <div class="section-title"><span>Actuators</span>
    <button class="btn primary sm" onclick="openActuatorModal()">+ Add</button>
  </div>
  <div class="table-responsive">
  <table>
    <thead><tr><th>ID</th><th>Type</th><th>MIDI Note</th><th>Bus</th><th>PCA</th><th>Ch</th><th>Mode</th><th>Status</th><th>Actions</th></tr></thead>
    <tbody id="actuators-table"><tr><td colspan="9" style="color:var(--fg2)">Loading...</td></tr></tbody>
  </table>
  </div>

  <!-- === CC Mapping === -->
  <div class="section-title" style="margin-top:20px"><span>Control Changes (CC)</span>
    <button class="btn primary sm" onclick="openAddCCModal()">+ Add CC</button>
  </div>
  <div style="margin-bottom:8px">
    <select id="cc-instrument" onchange="loadCCRouting()" class="form-select" style="max-width:250px"></select>
  </div>
  <p style="color:var(--fg2);font-size:12px;margin-bottom:8px">Position: moves a dedicated servo. Modifier: adjusts amplitude or speed of an instrument servo.</p>
  <div class="table-responsive">
  <table>
    <thead><tr><th>CC#</th><th>Type</th><th>Servo</th><th>Action</th><th>Range</th><th>Actions</th></tr></thead>
    <tbody id="mapping-cc-table"><tr><td colspan="6" style="color:var(--fg2)">Select an instrument</td></tr></tbody>
  </table>
  </div>
</div>

<!-- ============ ACOUSTIC CALIBRATION (visible only if microphone present) ============ -->
<div class="page" id="page-calibration">
  <div class="section-title">Acoustic Calibration</div>
  <div class="mic-status" id="mic-status">Checking microphone...</div>
  <div id="cal-controls">
    <div class="cards" style="grid-template-columns:repeat(auto-fit,minmax(140px,1fr))">
      <div class="card">
        <h3>Status</h3>
        <div class="val" id="cal-state" style="font-size:18px">Inactive</div>
      </div>
      <div class="card">
        <h3>Progress</h3>
        <div class="val"><span id="cal-progress">0</span><span class="unit">%</span></div>
        <div class="bar"><div class="bar-fill" id="cal-bar" style="width:0%;background:var(--accent)"></div></div>
      </div>
      <div class="card">
        <h3>Actuator</h3>
        <div class="val" id="cal-cur-act">&mdash;</div>
      </div>
      <div class="card">
        <h3>Results</h3>
        <div class="val"><span id="cal-result-count">0</span><span class="unit">measurements</span></div>
      </div>
    </div>
    <div class="btn-row" style="margin:16px 0">
      <button class="btn primary sm" onclick="startCalibrateAll()">&#9654; Calibrate all</button>
      <button class="btn sm" onclick="startCalibrateOne()" id="cal-btn-one">Calibrate one...</button>
      <button class="btn danger sm" onclick="stopCalibration()" id="cal-btn-stop" style="display:none">Stop</button>
      <button class="btn sm" onclick="applyCalibrateResults()" id="cal-btn-apply" style="display:none">&#10003; Apply</button>
      <button class="btn sm" onclick="loadCalibrateResults()" style="margin-left:auto">&#8635;</button>
    </div>
    <div id="cal-single-sel" style="display:none;margin-bottom:12px">
      <label style="font-size:13px;margin-right:8px">Actuator:</label>
      <select id="cal-act-select" class="form-select"></select>
      <button class="btn primary sm" style="margin-left:8px" onclick="confirmCalibrateOne()">Go</button>
      <button class="btn sm" style="margin-left:4px" onclick="document.getElementById('cal-single-sel').style.display='none'">&#10005;</button>
    </div>
    <div class="table-responsive">
    <table>
      <thead><tr><th>ID</th><th>Type</th><th>Current latency</th><th>Measured latency</th><th>Measurements</th><th>Status</th></tr></thead>
      <tbody id="cal-results-table"><tr><td colspan="6" style="color:var(--fg2);text-align:center">Start a calibration</td></tr></tbody>
    </table>
    </div>
  </div>
</div>

<!-- (Logs are now in the unified Settings page) -->

<script>
// ============================================================================
// State
// ============================================================================
let ws = null;
let wsConnected = false;
let currentPage = 'instrument';
let instruments = [];
let actuators = [];
let routing = [];
let pianoNotes = {}; // instIndex -> { note -> actuator_id }
let pressedKeys = {}; // "instIdx-note" -> true (keys currently held down by user)
let editingInstrumentIdx = -1;
let editingActuatorId = -1;

const SERVO_BEHAVIORS = ['Strike','Alternate','Strum','Key'];
const SOL_BEHAVIORS = ['Strike','Hit-and-Hold'];
const CC_TARGETS = ['Position (\u00b0)','Amplitude (\u00b0)','Speed (ms)','PWM hold'];
const CC_TARGET_UNITS = ['\u00b0','\u00b0','ms',''];
const CC_TARGET_RANGES = [[0,180],[0,180],[10,2000],[0,4095]];
const NOTE_NAMES = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];
const SCALES = {
  chromatic:{intervals:[0,1,2,3,4,5,6,7,8,9,10,11]},
  major:{intervals:[0,2,4,5,7,9,11]},
  pentatonic:{intervals:[0,2,4,7,9]}
};

// Log Manager (Phase 9)
let logCache = [];
let logLastCount = 0;
let logPollInterval = null;

function noteName(n) { return NOTE_NAMES[n%12] + Math.floor(n/12-1); }

// ============================================================================
// WebSocket
// ============================================================================
function connectWS() {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  ws = new WebSocket(proto + '//' + location.host + '/ws');

  ws.onopen = () => {
    wsConnected = true;
    // AUDIT FIX: reset log counter to -1 to force a refresh
    // on the first message (handles case of server restart + already connected client)
    logLastCount = -1;
    document.getElementById('ws-dot').className = 'dot';
    document.getElementById('ws-status').textContent = 'Connected';
  };

  ws.onclose = () => {
    wsConnected = false;
    document.getElementById('ws-dot').className = 'dot off';
    document.getElementById('ws-status').textContent = 'Disconnected';
    setTimeout(connectWS, 2000);
  };

  ws.onerror = () => { ws.close(); };

  ws.onmessage = (evt) => {
    try {
      const d = JSON.parse(evt.data);
      updateDashboard(d);
      // MIDI messages received via WebSocket
      if (d.midi_msg) pushMidiLog(d.midi_msg);
      if (d.midi_msgs) for (const m of d.midi_msgs) pushMidiLog(m);
      // Refresh logs if new entries detected
      if (d.log_count !== undefined && d.log_count !== logLastCount) {
        logLastCount = d.log_count;
        if (currentPage === 'settings') loadLogs();
      }
    } catch(e) {}
  };
}

function updateDashboard(d) {
  // MIDI Transport
  if (d.midi) {
    el('d-midi-recv', (d.midi.serial_bytes || 0) + (d.midi.udp_packets || 0) + (d.midi.rtp_packets || 0));
    el('d-midi-routed', d.dispatcher ? d.dispatcher.dispatched : 0);
    el('d-midi-unmapped', d.dispatcher ? d.dispatcher.dropped : 0);
  }
  if (d.dispatcher) {
    el('p-rejected', d.dispatcher.pwr_rejected || 0);
  }

  // Scheduler
  if (d.scheduler) {
    el('d-sched-queued', d.scheduler.queued || 0);
    el('d-sched-processed', d.scheduler.processed || 0);
  }

  // Power + Polyphony (unified card)
  if (d.power || d.safety) {
    const active = d.power?.active_count || d.safety?.active || 0;
    el('d-active', active);
    const pbar = document.getElementById('p-total-bar');
    if (pbar) {
      const max = d.power?.max_polyphony || 12;
      const pct = max > 0 ? Math.min(100, Math.round(active / max * 100)) : 0;
      pbar.style.width = pct + '%';
      pbar.style.background = pct > 80 ? 'var(--red)' : 'var(--green)';
    }
  }

  // Safety
  if (d.safety) {

    const killEl = document.getElementById('s-kill');
    if (killEl) {
      killEl.textContent = d.safety.kill_switch ? 'ON' : 'OFF';
      killEl.style.color = d.safety.kill_switch ? 'var(--red)' : 'var(--green)';
    }
    const degEl = document.getElementById('s-degrad');
    if (degEl) {
      degEl.textContent = d.safety.degradation ? 'Yes' : 'No';
      degEl.style.color = d.safety.degradation ? 'var(--yellow)' : 'var(--green)';
    }

    // Alerts
    updateAlerts(d);
  }

  // WiFi
  if (d.wifi) {
    el('d-wifi-rssi', d.wifi.rssi ? d.wifi.rssi + ' dBm' : 'N/A');
    el('d-wifi-status', d.wifi.connected ? 'Connected' : 'Disconnected');
  }

  // Update piano active notes
  if (d.active_actuators) {
    updatePianoActive(d.active_actuators);
  }
}

function updateAlerts(d) {
  let html = '';
  if (d.safety && d.safety.kill_switch) {
    html += '<div class="alert danger">KILL SWITCH ACTIVE — All outputs are disabled</div>';
  }
  if (d.safety && d.safety.degradation) {
    html += '<div class="alert warn">DEGRADATION — Approaching safety threshold</div>';
  }
  if (d.power && d.power.degradation) {
    html += '<div class="alert warn">POWER BUDGET — Graceful degradation active</div>';
  }
  document.getElementById('alert-zone').innerHTML = html;

  let safetyHtml = '';
  if (d.safety && d.safety.kill_switch) {
    safetyHtml += '<div class="alert danger">KILL SWITCH ACTIVE</div>';
  }
  const sz = document.getElementById('safety-alert-zone');
  if (sz) sz.innerHTML = safetyHtml;
}

// ============================================================================
// Navigation
// ============================================================================
function showPage(page) {
  currentPage = page;
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  const navEl = document.getElementById('main-nav');
  if (navEl) navEl.querySelectorAll('button').forEach(b => b.classList.remove('active'));

  const pageEl = document.getElementById('page-' + page);
  if (pageEl) pageEl.classList.add('active');

  // Highlight nav button
  if (navEl) navEl.querySelectorAll('button').forEach(b => {
    if (b.onclick && b.onclick.toString().includes("'" + page + "'"))
      b.classList.add('active');
  });

  // Show/hide nav for welcome page
  if (navEl) navEl.style.display = (page === 'welcome') ? 'none' : 'flex';

  // Load data for specific pages
  if (page === 'instrument') {
    loadHomeInstruments(); loadInstrumentSelects(); buildAllPianos();
  }
  if (page === 'midi') { loadMidiConfig(); }
  if (page === 'actuators') {
    loadActuatorsWithNotes(); loadInstrumentSelects(); loadCCRouting();
  }
  if (page === 'calibration') {
    loadCalibrateStatus(); loadCalibrateResults();
  }
  if (page === 'settings') {
    loadPower(); loadSafety(); loadLogs(); loadWiFiConfig(); loadBuses();
  }
}

// ============================================================================
// API helpers
// ============================================================================
async function api(url, method='GET', body=null) {
  const opts = { method, headers: {'Content-Type':'application/json'} };
  if (body) opts.body = JSON.stringify(body);
  const res = await fetch(url, opts);
  return res.json();
}

function el(id, val) {
  const e = document.getElementById(id);
  if (e) e.textContent = val;
}

// ============================================================================
// Instruments (Home page)
// ============================================================================
async function loadHomeInstruments() {
  instruments = await api('/api/instruments') || [];
  loadHomeInstruments_render();
}

function loadHomeInstruments_render() {
  const tbody = document.getElementById('home-instruments-table');
  if (!tbody) return;
  if (!instruments || instruments.length === 0) {
    tbody.innerHTML = '<tr><td colspan="6" style="color:var(--fg2)">No instrument &mdash; use the wizard to get started</td></tr>';
    return;
  }
  let html = '';
  for (const inst of instruments) {
    const busType = inst.bus_id === 0 ? 'Servos' : 'Solenoids';
    // Compute actuator count from routing if backend sends 0
    let actCount = inst.actuator_count || 0;
    if (actCount === 0 && routing) {
      const r = routing.find(x => x.instrument === inst.index);
      if (r && r.notes) actCount = r.notes.filter(n => n.enabled).length;
    }
    html += '<tr>';
    html += '<td><strong>' + esc(inst.name) + '</strong></td>';
    html += '<td>' + (inst.channel === 0 ? 'Omni' : 'Ch. ' + inst.channel) + '</td>';
    html += '<td>' + busType + '</td>';
    html += '<td>' + actCount + '</td>';
    html += '<td>' + (inst.enabled ? '<span class="badge on">Active</span>' : '<span class="badge off">Inactive</span>') + '</td>';
    html += '<td><button class="btn sm" onclick="editInstrument(' + inst.index + ')">Edit</button> ';
    html += '<button class="btn sm" onclick="deleteInstrument(' + inst.index + ')">Delete</button></td>';
    html += '</tr>';
  }
  tbody.innerHTML = html;
}

function openInstrumentModal() {
  editingInstrumentIdx = -1;
  document.getElementById('mi-name').value = '';
  document.getElementById('mi-channel').value = Math.min(instruments ? instruments.length + 1 : 1, 16);
  document.getElementById('mi-bus').value = '0';
  document.getElementById('mi-latency').value = '10';
  document.getElementById('mi-autocal').value = '0';
  document.getElementById('modal-inst-title').textContent = 'New instrument';
  document.getElementById('modal-instrument').classList.add('show');
}

function editInstrument(idx) {
  const inst = instruments.find(i => i.index === idx);
  if (!inst) return;
  editingInstrumentIdx = idx;
  document.getElementById('mi-name').value = inst.name;
  document.getElementById('mi-channel').value = inst.channel;
  document.getElementById('mi-bus').value = inst.bus_id;
  document.getElementById('mi-latency').value = inst.latency_ms;
  document.getElementById('mi-autocal').value = inst.auto_cal ? '1' : '0';
  document.getElementById('modal-inst-title').textContent = 'Edit ' + inst.name;
  document.getElementById('modal-instrument').classList.add('show');
}

function closeModal(id) {
  document.getElementById(id).classList.remove('show');
}

// Themed confirm/alert modals (replace native confirm/alert)
function appConfirm(title, message, {confirmText='Confirm', cancelText='Cancel', danger=false, icon='\u26a0\ufe0f'}={}) {
  return new Promise(resolve => {
    document.getElementById('confirm-icon').textContent = icon;
    document.getElementById('confirm-title').textContent = title;
    document.getElementById('confirm-message').textContent = message;
    const btns = document.getElementById('confirm-buttons');
    btns.innerHTML = '';
    const btnCancel = document.createElement('button');
    btnCancel.className = 'btn';
    btnCancel.textContent = cancelText;
    btnCancel.onclick = () => { closeModal('modal-confirm'); resolve(false); };
    const btnOk = document.createElement('button');
    btnOk.className = 'btn ' + (danger ? 'danger' : 'primary');
    btnOk.textContent = confirmText;
    btnOk.onclick = () => { closeModal('modal-confirm'); resolve(true); };
    btns.appendChild(btnCancel);
    btns.appendChild(btnOk);
    document.getElementById('modal-confirm').classList.add('show');
  });
}

function appAlert(title, message, {btnText='OK', icon='\u2139\ufe0f'}={}) {
  return new Promise(resolve => {
    document.getElementById('confirm-icon').textContent = icon;
    document.getElementById('confirm-title').textContent = title;
    document.getElementById('confirm-message').textContent = message;
    const btns = document.getElementById('confirm-buttons');
    btns.innerHTML = '';
    const btnOk = document.createElement('button');
    btnOk.className = 'btn primary';
    btnOk.textContent = btnText;
    btnOk.onclick = () => { closeModal('modal-confirm'); resolve(); };
    btns.appendChild(btnOk);
    document.getElementById('modal-confirm').classList.add('show');
  });
}

async function saveInstrument() {
  const data = {
    name: document.getElementById('mi-name').value || 'Instrument',
    channel: parseInt(document.getElementById('mi-channel').value),
    bus_id: parseInt(document.getElementById('mi-bus').value),
    latency_ms: parseInt(document.getElementById('mi-latency').value),
    auto_cal: document.getElementById('mi-autocal').value === '1',
    enabled: true
  };
  if (editingInstrumentIdx >= 0) data.index = editingInstrumentIdx;
  await api('/api/instrument', 'POST', data);
  closeModal('modal-instrument');
  editingInstrumentIdx = -1;
  instruments = [];  // Force refresh
  routing = await api('/api/routing') || [];
  loadHomeInstruments();
  loadInstrumentSelects();
  buildAllPianos();
  // After creating first instrument, go to instrument page (exit welcome)
  if (currentPage === 'welcome') showPage('instrument');
}

async function deleteInstrument(idx) {
  if (!await appConfirm('Delete instrument', 'This will delete the instrument and its mappings.', {danger:true, icon:'\ud83d\uddd1\ufe0f'})) return;
  await api('/api/instrument?index=' + idx, 'DELETE');
  instruments = [];  // Force refresh
  routing = await api('/api/routing') || [];
  loadHomeInstruments();
  loadInstrumentSelects();
  buildAllPianos();
}

// ============================================================================
// Actuators (with MIDI note display)
// ============================================================================
async function loadActuators() {
  actuators = await api('/api/actuators');
}

function buildActNoteMap() {
  // Build reverse map: actuator_id -> {note, instIdx}
  const map = {};
  if (routing) {
    for (const r of routing) {
      if (r.notes) {
        for (const nm of r.notes) {
          if (nm.enabled) map[nm.actuator] = {note: nm.note, inst: r.instrument};
        }
      }
    }
  }
  return map;
}

async function loadActuatorsWithNotes() {
  await loadActuators();
  if (!routing || routing.length === 0) {
    routing = await api('/api/routing') || [];
  }
  const noteMap = buildActNoteMap();
  const tbody = document.getElementById('actuators-table');
  if (!actuators || actuators.length === 0) {
    tbody.innerHTML = '<tr><td colspan="9" style="color:var(--fg2)">No actuator configured</td></tr>';
    return;
  }
  // Group actuators by instrument
  const groups = {};
  const unassigned = [];
  for (const act of actuators) {
    const nm = noteMap[act.id];
    if (nm) {
      if (!groups[nm.inst]) groups[nm.inst] = [];
      groups[nm.inst].push(act);
    } else {
      unassigned.push(act);
    }
  }
  let html = '';
  function renderRow(act) {
    const isServo = act.type === 0;
    const behaviors = isServo ? SERVO_BEHAVIORS : SOL_BEHAVIORS;
    const nm = noteMap[act.id];
    html += '<tr>';
    html += '<td>' + act.id + '</td>';
    html += '<td>' + (isServo ? '<span class="badge servo">Servo</span>' : '<span class="badge sol">Solenoid</span>') + '</td>';
    html += '<td><input class="note-input" type="number" min="0" max="127" value="' + (nm ? nm.note : '') + '" placeholder="-" '
      + 'onchange="setActuatorNote(' + act.id + ',this.value)">';
    if (nm) html += '<span class="note-label">' + noteName(nm.note) + '</span>';
    html += '</td>';
    html += '<td>Bus ' + act.bus_id + '</td>';
    html += '<td>0x' + act.pca_addr.toString(16).toUpperCase() + '</td>';
    html += '<td>' + act.pca_ch + '</td>';
    html += '<td>' + (behaviors[act.behavior] || '?') + '</td>';
    html += '<td>' + (act.state && act.state.active ? '<span class="badge on">Active</span>' : '<span class="badge off">Idle</span>') + '</td>';
    html += '<td><button class="btn sm" onclick="editActuator(' + act.id + ')">Edit</button> ';
    html += '<button class="btn sm" onclick="testActuator(' + act.id + ')">Test</button> ';
    html += '<button class="btn sm" onclick="deleteActuator(' + act.id + ')">Delete</button></td>';
    html += '</tr>';
  }
  // Render each instrument group
  for (const inst of (instruments || [])) {
    const acts = groups[inst.index];
    if (!acts || acts.length === 0) continue;
    html += '<tr class="inst-separator"><td colspan="9">' + esc(inst.name) + ' <span style="font-weight:400;color:var(--fg2)">(' + acts.length + ')</span></td></tr>';
    for (const act of acts) renderRow(act);
  }
  // Unassigned actuators
  if (unassigned.length > 0) {
    html += '<tr class="inst-separator"><td colspan="9">Unassigned <span style="font-weight:400;color:var(--fg2)">(' + unassigned.length + ')</span></td></tr>';
    for (const act of unassigned) renderRow(act);
  }
  tbody.innerHTML = html;
  updateCountBadges();
}

async function setActuatorNote(actId, noteVal) {
  const note = parseInt(noteVal);
  if (isNaN(note) || note < 0 || note > 127) return;
  // Find which instrument this actuator belongs to (use first, or 0)
  let instIdx = 0;
  if (routing) {
    for (const r of routing) {
      if (r.notes && r.notes.find(n => n.actuator === actId)) {
        instIdx = r.instrument;
        break;
      }
    }
  }
  // Get current routing for this instrument
  const r = routing ? routing.find(x => x.instrument === instIdx) : null;
  let notes = r && r.notes ? r.notes.filter(n => n.actuator !== actId) : [];
  notes.push({note: note, actuator: actId, enabled: true});
  await api('/api/routing', 'POST', {instrument: instIdx, notes: notes});
  routing = await api('/api/routing') || [];
  loadActuatorsWithNotes();
}

function toggleActuatorFields() {
  const type = document.getElementById('ma-type').value;
  document.getElementById('servo-fields').style.display = type === '0' ? 'block' : 'none';
  document.getElementById('solenoid-fields').style.display = type === '1' ? 'block' : 'none';
  // Auto-select bus: servo=bus0, solenoid=bus1
  if (editingActuatorId < 0) {
    document.getElementById('ma-bus').value = type === '1' ? '1' : '0';
  }
  toggleHitHoldFields();
  toggleServoDirection();
  updateAnglePreview();
}

function toggleHitHoldFields() {
  const el = document.getElementById('hit-hold-fields');
  if (!el) return;
  const mode = document.getElementById('ma-sol-behavior').value;
  el.style.display = (mode === '1') ? 'block' : 'none';
}

function toggleServoDirection() {
  const el = document.getElementById('servo-direction-group');
  const stdFields = document.getElementById('servo-standard-fields');
  const altFields = document.getElementById('servo-alterne-fields');
  const mode = document.getElementById('ma-servo-behavior').value;
  const isAlterne = mode === '1';
  // Show direction only for strike (0) and key (3)
  if (el) el.style.display = (mode === '0' || mode === '3') ? 'block' : 'none';
  // Show standard fields (idle + amplitude) for all modes except alternate
  if (stdFields) stdFields.style.display = isAlterne ? 'none' : '';
  // Show angle A / angle B only for alternate
  if (altFields) altFields.style.display = isAlterne ? '' : 'none';
  updateAnglePreview();
}

function openActuatorModal() {
  editingActuatorId = -1;
  // Auto-increment actuator ID
  const nextId = (actuators && actuators.length > 0)
    ? Math.max(...actuators.map(a => a.id)) + 1 : 0;
  document.getElementById('ma-id').value = nextId;
  document.getElementById('ma-type').value = '0';
  // Auto-increment PCA channel based on existing actuators
  let nextCh = 0;
  let pcaAddr = 64; // 0x40
  if (actuators && actuators.length > 0) {
    const sorted = [...actuators].sort((a,b) =>
      a.pca_addr !== b.pca_addr ? a.pca_addr - b.pca_addr : a.pca_ch - b.pca_ch);
    const last = sorted[sorted.length - 1];
    nextCh = last.pca_ch + 1;
    pcaAddr = last.pca_addr;
    if (nextCh > 15) { nextCh = 0; pcaAddr = Math.min(pcaAddr + 1, 67); }
  }
  document.getElementById('ma-ch').value = nextCh;
  document.getElementById('ma-pca').value = pcaAddr;
  document.getElementById('ma-latency').value = '10';
  // Reset servo fields
  document.getElementById('ma-servo-behavior').value = '0';
  document.getElementById('ma-angle-init').value = '90';
  document.getElementById('ma-amplitude').value = '45';
  document.getElementById('ma-speed').value = '150';
  document.getElementById('ma-angle-a-alt').value = '90';
  document.getElementById('ma-angle-b').value = '120';
  // Reset solenoid fields
  document.getElementById('ma-sol-behavior').value = '0';
  document.getElementById('ma-pulse-min').value = '5';
  document.getElementById('ma-pulse-max').value = '30';
  document.getElementById('ma-pwm-init').value = '4095';
  document.getElementById('ma-pwm-hold').value = '2048';
  document.getElementById('ma-ramp').value = '50';
  toggleActuatorFields();
  document.getElementById('modal-act-title').textContent = 'New actuator';
  document.getElementById('modal-actuator').classList.add('show');
  updateAnglePreview();
}

function editActuator(id) {
  const act = actuators.find(a => a.id === id);
  if (!act) return;
  editingActuatorId = id;
  document.getElementById('ma-id').value = act.id;
  document.getElementById('ma-type').value = act.type;
  document.getElementById('ma-bus').value = act.bus_id;
  document.getElementById('ma-pca').value = act.pca_addr;
  document.getElementById('ma-ch').value = act.pca_ch;
  document.getElementById('ma-latency').value = act.latency_ms;
  toggleActuatorFields();
  if (act.type === 0) {
    document.getElementById('ma-servo-behavior').value = act.behavior || 0;
    document.getElementById('ma-hit-reverse').value = act.hit_reverse ? '1' : '0';
    document.getElementById('ma-angle-init').value = act.angle_init !== undefined ? act.angle_init : 90;
    document.getElementById('ma-amplitude').value = act.amplitude !== undefined ? act.amplitude : 45;
    document.getElementById('ma-speed').value = act.speed_ms || 150;
    document.getElementById('ma-angle-a-alt').value = act.angle_init !== undefined ? act.angle_init : 90;
    document.getElementById('ma-angle-b').value = act.angle_b !== undefined ? act.angle_b : 120;
    toggleServoDirection();
  } else {
    document.getElementById('ma-sol-behavior').value = act.behavior || 0;
    document.getElementById('ma-pulse-min').value = act.pulse_min_ms !== undefined ? act.pulse_min_ms : 5;
    document.getElementById('ma-pulse-max').value = act.pulse_max_ms !== undefined ? act.pulse_max_ms : (act.pulse_ms || 30);
    document.getElementById('ma-pwm-init').value = act.pwm_initial !== undefined ? act.pwm_initial : 4095;
    document.getElementById('ma-pwm-hold').value = act.pwm_hold !== undefined ? act.pwm_hold : 2048;
    document.getElementById('ma-ramp').value = act.ramp_ms || 50;
    toggleHitHoldFields();
  }
  document.getElementById('modal-act-title').textContent = 'Edit actuator #' + id;
  document.getElementById('modal-actuator').classList.add('show');
  updateAnglePreview();
}

async function saveActuator() {
  const type = parseInt(document.getElementById('ma-type').value);
  const data = {
    id: parseInt(document.getElementById('ma-id').value),
    type: type,
    bus_id: parseInt(document.getElementById('ma-bus').value),
    pca_addr: parseInt(document.getElementById('ma-pca').value),
    pca_ch: parseInt(document.getElementById('ma-ch').value),
    latency_ms: parseInt(document.getElementById('ma-latency').value),
    enabled: true
  };

  if (type === 0) { // Servo
    data.behavior = parseInt(document.getElementById('ma-servo-behavior').value);
    data.hit_reverse = document.getElementById('ma-hit-reverse').value === '1';
    if (data.behavior === 1) { // Alternate: use angle A/B fields
      data.angle_init = parseInt(document.getElementById('ma-angle-a-alt').value);
      data.amplitude = 0;
    } else {
      data.angle_init = parseInt(document.getElementById('ma-angle-init').value);
      data.amplitude = parseInt(document.getElementById('ma-amplitude').value);
    }
    data.speed_ms = parseInt(document.getElementById('ma-speed').value);
    data.angle_b = parseInt(document.getElementById('ma-angle-b').value);
  } else { // Solenoid
    data.behavior = parseInt(document.getElementById('ma-sol-behavior').value);
    data.pulse_min_ms = parseInt(document.getElementById('ma-pulse-min').value);
    data.pulse_max_ms = parseInt(document.getElementById('ma-pulse-max').value);
    data.pulse_ms = data.pulse_max_ms; // backward compat
    data.pwm_initial = parseInt(document.getElementById('ma-pwm-init').value);
    data.pwm_hold = parseInt(document.getElementById('ma-pwm-hold').value);
    data.ramp_ms = parseInt(document.getElementById('ma-ramp').value);
  }

  await api('/api/actuator', 'POST', data);
  closeModal('modal-actuator');
  editingActuatorId = -1;
  loadActuatorsWithNotes();
}

async function testActuator(id) {
  await api('/api/test/actuator', 'POST', {id: id, velocity: 100, note_on: true});
  setTimeout(() => {
    api('/api/test/actuator', 'POST', {id: id, velocity: 0, note_on: false});
  }, 500);
}

async function deleteActuator(id) {
  if (!await appConfirm('Delete actuator', 'Delete actuator #' + id + '?', {danger:true, icon:'\ud83d\uddd1\ufe0f'})) return;
  await api('/api/actuator?id=' + id, 'DELETE');
  loadActuatorsWithNotes();
}

// ============================================================================
// MIDI Config
// ============================================================================
async function loadMidiConfig() {
  const d = await api('/api/midi');
  if (!d) return;
  document.getElementById('midi-serial').checked = d.serial_enabled;
  document.getElementById('midi-udp').checked = d.udp_enabled;
  document.getElementById('midi-rtp').checked = d.rtp_enabled;
  el('midi-rx-pin', d.serial_rx_pin);
  el('midi-udp-port', d.udp_port);
  el('midi-rtp-port', d.rtp_port);
  document.getElementById('midi-jitter').value = d.jitter_buffer_ms;
  el('midi-jitter-val', d.jitter_buffer_ms);
}

async function updateMidiConfig() {
  await api('/api/midi', 'POST', {
    serial_enabled: document.getElementById('midi-serial').checked,
    udp_enabled: document.getElementById('midi-udp').checked,
    rtp_enabled: document.getElementById('midi-rtp').checked,
    jitter_buffer_ms: parseInt(document.getElementById('midi-jitter').value)
  });
}

// ============================================================================
// MIDI Message Log (last 50 messages, fed by WebSocket)
// ============================================================================
const MIDI_LOG_MAX = 50;
let midiLogEntries = [];
const MIDI_TYPE_NAMES = {
  0x80:'Note Off', 0x90:'Note On', 0xA0:'Aftertouch', 0xB0:'CC',
  0xC0:'Program', 0xD0:'Ch Pressure', 0xE0:'Pitch Bend'
};
const MIDI_SOURCE_NAMES = {serial:'Cable', udp:'UDP', rtp:'RTP'};

function formatMidiData(m) {
  const t = m.type & 0xF0;
  if (t === 0x90 || t === 0x80) return noteName(m.d1) + ' (' + m.d1 + ') v=' + m.d2;
  if (t === 0xB0) return 'CC' + m.d1 + ' = ' + m.d2;
  if (t === 0xC0) return 'Prog ' + m.d1;
  if (t === 0xE0) return '' + ((m.d2 << 7 | m.d1) - 8192);
  return m.d1 + (m.d2 !== undefined ? ', ' + m.d2 : '');
}

function pushMidiLog(m) {
  if (document.getElementById('midi-log-pause')?.checked) return;
  midiLogEntries.push(m);
  if (midiLogEntries.length > MIDI_LOG_MAX) midiLogEntries.shift();
  renderMidiLog();
}

function renderMidiLog() {
  const tbody = document.getElementById('midi-log-table');
  if (!tbody) return;
  if (midiLogEntries.length === 0) {
    tbody.innerHTML = '<tr><td colspan="6" style="color:var(--fg2)">Waiting for MIDI messages...</td></tr>';
    return;
  }
  let html = '';
  for (let i = midiLogEntries.length - 1; i >= 0; i--) {
    const m = midiLogEntries[i];
    const typeName = MIDI_TYPE_NAMES[m.type & 0xF0] || '0x' + m.type.toString(16);
    const ch = (m.type & 0x0F) + 1;
    const src = MIDI_SOURCE_NAMES[m.src] || m.src || '?';
    const routed = m.routed ? '<span class="badge on">Yes</span>' : '<span class="badge off">No</span>';
    html += '<tr>';
    html += '<td style="font-size:11px;font-family:monospace;white-space:nowrap">' + formatLogTime(m.t || 0) + '</td>';
    html += '<td>' + src + '</td>';
    html += '<td>' + ch + '</td>';
    html += '<td>' + typeName + '</td>';
    html += '<td>' + formatMidiData(m) + '</td>';
    html += '<td>' + routed + '</td>';
    html += '</tr>';
  }
  tbody.innerHTML = html;
  const info = document.getElementById('midi-log-count');
  if (info) info.textContent = midiLogEntries.length + ' / ' + MIDI_LOG_MAX + ' messages';
}

function clearMidiLog() {
  midiLogEntries = [];
  renderMidiLog();
}

async function loadInstrumentSelects() {
  if (!instruments || instruments.length === 0) {
    instruments = await api('/api/instruments');
  }
  const selects = ['cc-instrument'];
  for (const sid of selects) {
    const sel = document.getElementById(sid);
    if (!sel) continue;
    sel.innerHTML = '';
    if (!instruments || instruments.length === 0) {
      sel.innerHTML = '<option value="">No instrument</option>';
      continue;
    }
    for (const inst of instruments) {
      const opt = document.createElement('option');
      opt.value = inst.index;
      opt.textContent = inst.name + ' (' + (inst.channel === 0 ? 'Omni' : 'ch.' + inst.channel) + ')';
      sel.appendChild(opt);
    }
  }
}

// ============================================================================
// CC Routing (Control Changes only)
// ============================================================================
async function loadCCRouting() {
  routing = await api('/api/routing');
  const sel = document.getElementById('cc-instrument');
  const instIdx = parseInt(sel ? sel.value : '0');
  const r = routing ? routing.find(x => x.instrument === instIdx) : null;

  const ctbody = document.getElementById('mapping-cc-table');
  if (!ctbody) return;
  if (!r || !r.ccs || r.ccs.length === 0) {
    ctbody.innerHTML = '<tr><td colspan="6" style="color:var(--fg2)">No CC mapping</td></tr>';
  } else {
    let html = '';
    for (let ci = 0; ci < r.ccs.length; ci++) {
      const cm = r.ccs[ci];
      const isPos = cm.target === 0;
      const unit = CC_TARGET_UNITS[cm.target] || '';
      html += '<tr>';
      html += '<td>CC ' + cm.cc + '</td>';
      html += '<td>' + (isPos ? '<span class="badge servo">Position</span>' : '<span class="badge sol">Modifier</span>') + '</td>';
      html += '<td>Servo #' + cm.actuator + '</td>';
      html += '<td>' + (CC_TARGETS[cm.target] || '?') + '</td>';
      html += '<td>' + cm.min + unit + ' \u2192 ' + cm.max + unit + '</td>';
      html += '<td><button class="btn sm" onclick="editCC(' + instIdx + ',' + cm.cc + ')">Edit</button> ';
      html += '<button class="btn sm" onclick="deleteCC(' + instIdx + ',' + cm.cc + ')">Delete</button></td>';
      html += '</tr>';
    }
    ctbody.innerHTML = html;
  }

  updateCountBadges();
}

// Build map: actuator_id -> instrument index (for servos used by note mappings)
function getInstrumentActuatorIds() {
  const used = {};
  if (routing) {
    for (const r of routing) {
      if (r.notes) {
        for (const n of r.notes) {
          if (n.enabled) used[n.actuator] = r.instrument;
        }
      }
    }
  }
  return used;
}

// Populate servo select with FREE servos only (not assigned to any instrument note)
function populateFreeServoSelect(selectedActId) {
  const sel = document.getElementById('cc-actuator-free');
  sel.innerHTML = '';
  const usedByInst = getInstrumentActuatorIds();
  if (actuators) {
    for (const a of actuators) {
      if (a.type !== 0) continue;
      if (usedByInst[a.id] !== undefined) continue;
      const opt = document.createElement('option');
      opt.value = a.id;
      opt.textContent = 'Servo #' + a.id;
      if (selectedActId !== undefined && a.id === selectedActId) opt.selected = true;
      sel.appendChild(opt);
    }
  }
  const hint = document.getElementById('cc-create-servo-hint');
  if (sel.options.length === 0) {
    sel.innerHTML = '<option value="">No free servo</option>';
    if (hint) hint.style.display = '';
  } else {
    if (hint) hint.style.display = 'none';
  }
}

// Populate servo select with INSTRUMENT servos (assigned to current instrument notes)
function populateInstServoSelect(instIdx, selectedActId) {
  const sel = document.getElementById('cc-actuator-inst');
  sel.innerHTML = '';
  const r = routing ? routing.find(x => x.instrument === instIdx) : null;
  const instActIds = new Set();
  if (r && r.notes) {
    for (const n of r.notes) {
      if (n.enabled) instActIds.add(n.actuator);
    }
  }
  if (actuators) {
    for (const a of actuators) {
      if (a.type !== 0) continue;
      if (!instActIds.has(a.id)) continue;
      const opt = document.createElement('option');
      opt.value = a.id;
      opt.textContent = 'Servo #' + a.id;
      if (selectedActId !== undefined && a.id === selectedActId) opt.selected = true;
      sel.appendChild(opt);
    }
  }
  if (sel.options.length === 0) {
    sel.innerHTML = '<option value="">No servo in this instrument</option>';
  }
}

function toggleCCCategory() {
  const cat = document.getElementById('cc-category').value;
  document.getElementById('cc-cat-position').style.display = cat === 'position' ? '' : 'none';
  document.getElementById('cc-cat-modifier').style.display = cat === 'modifier' ? '' : 'none';
}

function updateCCModRangeHints() {
  const t = parseInt(document.getElementById('cc-mod-target').value);
  const range = CC_TARGET_RANGES[t] || [0,180];
  const unit = CC_TARGET_UNITS[t] || '';
  const help = document.getElementById('cc-mod-help');
  if (t === 1) {
    if (help) help.textContent = 'Modifies the strike range in real-time for upcoming notes';
  } else {
    if (help) help.textContent = 'Modifies the forward movement duration (10ms = fast, 2000ms = slow)';
  }
  document.getElementById('cc-mod-min-unit').textContent = unit ? '(' + unit + ')' : '';
  document.getElementById('cc-mod-max-unit').textContent = unit ? '(' + unit + ')' : '';
  if (editingCCNum < 0) {
    document.getElementById('cc-mod-min').value = range[0];
    document.getElementById('cc-mod-max').value = range[1];
  }
  updateCCServoInfo();
}

function updateCCServoInfo() {
  const infoDiv = document.getElementById('cc-servo-info');
  if (!infoDiv) return;
  const actId = parseInt(document.getElementById('cc-actuator-inst').value);
  const act = actuators ? actuators.find(a => a.id === actId) : null;
  if (!act || isNaN(actId)) { infoDiv.style.display = 'none'; return; }
  const init = act.angle_init !== undefined ? act.angle_init : 90;
  const amp = act.amplitude !== undefined ? act.amplitude : 45;
  const rev = act.hit_reverse;
  const speed = act.speed_ms || 150;
  const beh = SERVO_BEHAVIORS[act.behavior] || '?';
  let minAngle, maxAngle;
  if (act.behavior === 1) { // Alternate
    minAngle = init;
    maxAngle = act.angle_b !== undefined ? act.angle_b : 120;
  } else if (rev) {
    minAngle = Math.max(0, init - amp);
    maxAngle = init;
  } else {
    minAngle = init;
    maxAngle = Math.min(180, init + amp);
  }
  let s = '<strong>Servo #' + act.id + '</strong> \u2014 ' + beh;
  s += ' \u2022 Idle: ' + init + '\u00b0';
  s += ' \u2022 Range: ' + minAngle + '\u00b0 \u2192 ' + maxAngle + '\u00b0';
  s += ' \u2022 Speed: ' + speed + 'ms';
  infoDiv.innerHTML = s;
  infoDiv.style.display = '';
}

// Quick-create a servo for CC usage
async function quickCreateServoForCC() {
  const nextId = (actuators && actuators.length > 0) ? Math.max(...actuators.map(a => a.id)) + 1 : 0;
  let nextCh = 0, pcaAddr = 64;
  if (actuators && actuators.length > 0) {
    const sorted = [...actuators].sort((a,b) => a.pca_addr !== b.pca_addr ? a.pca_addr - b.pca_addr : a.pca_ch - b.pca_ch);
    const last = sorted[sorted.length - 1];
    nextCh = last.pca_ch + 1;
    pcaAddr = last.pca_addr;
    if (nextCh > 15) { nextCh = 0; pcaAddr = Math.min(pcaAddr + 1, 67); }
  }
  const data = {
    id: nextId, type: 0, bus_id: 0, pca_addr: pcaAddr, pca_ch: nextCh,
    latency_ms: 0, behavior: 0, hit_reverse: false,
    angle_init: 90, amplitude: 90, speed_ms: 200, angle_b: 120, enabled: true
  };
  await api('/api/actuator', 'POST', data);
  actuators = await api('/api/actuators') || [];
  populateFreeServoSelect(nextId);
  toast('Servo #' + nextId + ' created', 'ok');
}

let editingCCNum = -1;

function openAddCCModal() {
  editingCCNum = -1;
  const instSel = document.getElementById('cc-instrument');
  const instIdx = parseInt(instSel ? instSel.value : '0');
  document.getElementById('cc-category').value = 'position';
  toggleCCCategory();
  populateFreeServoSelect();
  populateInstServoSelect(instIdx);
  document.getElementById('cc-num').value = '1';
  document.getElementById('cc-num').disabled = false;
  document.getElementById('cc-pos-min').value = '0';
  document.getElementById('cc-pos-max').value = '180';
  document.getElementById('cc-mod-target').value = '1';
  updateCCModRangeHints();
  document.getElementById('modal-cc-title').textContent = 'Add CC Mapping';
  document.getElementById('cc-save-btn').textContent = 'Add';
  document.getElementById('modal-cc').classList.add('show');
}

function editCC(instIdx, ccNum) {
  const r = routing ? routing.find(x => x.instrument === instIdx) : null;
  if (!r || !r.ccs) return;
  const cm = r.ccs.find(c => c.cc === ccNum);
  if (!cm) return;
  editingCCNum = ccNum;
  const isPos = cm.target === 0;
  document.getElementById('cc-category').value = isPos ? 'position' : 'modifier';
  toggleCCCategory();
  if (isPos) {
    populateFreeServoSelect(cm.actuator);
    document.getElementById('cc-pos-min').value = cm.min;
    document.getElementById('cc-pos-max').value = cm.max;
  } else {
    populateInstServoSelect(instIdx, cm.actuator);
    document.getElementById('cc-mod-target').value = cm.target;
    document.getElementById('cc-mod-min').value = cm.min;
    document.getElementById('cc-mod-max').value = cm.max;
    updateCCModRangeHints();
  }
  document.getElementById('cc-num').value = cm.cc;
  document.getElementById('cc-num').disabled = true;
  document.getElementById('modal-cc-title').textContent = 'Edit CC ' + ccNum;
  document.getElementById('cc-save-btn').textContent = 'Save';
  document.getElementById('modal-cc').classList.add('show');
}

async function saveCC() {
  const instSel = document.getElementById('cc-instrument');
  const instIdx = parseInt(instSel ? instSel.value : '0');
  const ccNum = parseInt(document.getElementById('cc-num').value);
  const cat = document.getElementById('cc-category').value;
  let actId, target, min, max;
  if (cat === 'position') {
    actId = parseInt(document.getElementById('cc-actuator-free').value);
    target = 0;
    min = parseInt(document.getElementById('cc-pos-min').value);
    max = parseInt(document.getElementById('cc-pos-max').value);
  } else {
    actId = parseInt(document.getElementById('cc-actuator-inst').value);
    target = parseInt(document.getElementById('cc-mod-target').value);
    min = parseInt(document.getElementById('cc-mod-min').value);
    max = parseInt(document.getElementById('cc-mod-max').value);
  }
  if (isNaN(ccNum) || isNaN(actId)) { toast('Invalid values', 'error'); return; }

  const r = routing ? routing.find(x => x.instrument === instIdx) : null;
  let ccs = r && r.ccs ? [...r.ccs] : [];
  ccs = ccs.filter(c => c.cc !== ccNum);
  ccs.push({cc: ccNum, actuator: actId, target, min, max, enabled: true});
  await api('/api/routing/cc', 'POST', {instrument: instIdx, ccs});
  closeModal('modal-cc');
  toast(editingCCNum >= 0 ? 'CC ' + ccNum + ' updated' : 'CC ' + ccNum + ' added', 'ok');
  editingCCNum = -1;
  loadCCRouting();
}

async function deleteCC(instIdx, ccNum) {
  if (!await appConfirm('Delete CC', 'Delete CC mapping ' + ccNum + '?', {danger:true, icon:'\ud83d\uddd1\ufe0f'})) return;
  const r = routing ? routing.find(x => x.instrument === instIdx) : null;
  if (!r || !r.ccs) return;
  const ccs = r.ccs.filter(c => c.cc !== ccNum);
  await api('/api/routing/cc', 'POST', {instrument: instIdx, ccs});
  loadCCRouting();
}

// ============================================================================
// Piano
// ============================================================================
// Build all pianos — one per instrument
function buildAllPianos() {
  const container = document.getElementById('pianos-container');
  if (!container) return;
  container.innerHTML = '';
  pianoNotes = {};
  pressedKeys = {};

  if (!instruments || instruments.length === 0) {
    container.innerHTML = '<p style="color:var(--fg2);font-size:13px;margin-top:16px">No instrument. Use the wizard to create your first instrument.</p>';
    return;
  }

  for (const inst of instruments) {
    const idx = inst.index;
    const r = routing ? routing.find(x => x.instrument === idx) : null;
    let mappedNotes = new Set();
    pianoNotes[idx] = {};
    if (r && r.notes) {
      for (const nm of r.notes) {
        if (nm.enabled) {
          mappedNotes.add(nm.note);
          pianoNotes[idx][nm.note] = nm.actuator;
        }
      }
    }
    if (mappedNotes.size === 0) continue; // skip instruments with no notes

    // Section wrapper
    const section = document.createElement('div');
    section.style.cssText = 'margin-top:20px';

    const title = document.createElement('div');
    title.className = 'section-title';
    title.style.fontSize = '14px';
    const chLabel = inst.channel === 0 ? 'Omni' : 'ch.' + inst.channel;
    title.innerHTML = '<span>' + esc(inst.name) + ' <span style="font-weight:400;color:var(--fg2)">(' + chLabel + ')</span></span>';
    section.appendChild(title);

    const pianoWrap = document.createElement('div');
    pianoWrap.className = 'piano-container';
    const piano = document.createElement('div');
    piano.className = 'piano';
    piano.id = 'piano-' + idx;

    // Compute note range — tight range around mapped notes
    const minNote = Math.min(...mappedNotes);
    const maxNote = Math.max(...mappedNotes);
    // Pad 1 note below and above to give context, clamp to octave boundary
    let startNote = Math.max(0, minNote - 1);
    // Align to nearest white key below
    while (startNote > 0 && [1,3,6,8,10].includes(startNote % 12)) startNote--;
    let endNote = Math.min(128, maxNote + 2);
    // Align to nearest white key above
    while (endNote < 128 && [1,3,6,8,10].includes(endNote % 12)) endNote++;

    // Render ALL notes in the range for correct piano layout
    // Unmapped notes are shown dimmed but maintain proper spatial ordering
    const notesToRender = new Set();
    for (let n = startNote; n <= endNote; n++) {
      notesToRender.add(n);
    }

    // White keys first (they define layout flow)
    let wIdx = 0;
    const whiteIdxMap = {}; // note -> white key index (for black key positioning)
    for (let n = startNote; n <= endNote; n++) {
      const nio = n % 12;
      if ([1,3,6,8,10].includes(nio)) continue; // skip black
      if (!notesToRender.has(n)) continue;
      whiteIdxMap[n] = wIdx;
      const k = document.createElement('div');
      const wMapped = mappedNotes.has(n);
      k.className = 'white';
      if (!wMapped) { k.style.opacity = '0.4'; k.style.cursor = 'default'; }
      k.dataset.note = n;
      k.dataset.inst = idx;
      k.textContent = noteName(n);
      if (wMapped) {
        k.onmousedown = () => pianoNoteOn(idx, n);
        k.onmouseup = () => pianoNoteOff(idx, n);
        k.onmouseleave = () => pianoNoteOff(idx, n);
        k.addEventListener('touchstart', (e) => { e.preventDefault(); pianoNoteOn(idx, n); }, {passive:false});
        k.addEventListener('touchend', (e) => { e.preventDefault(); pianoNoteOff(idx, n); }, {passive:false});
        k.addEventListener('touchcancel', (e) => { e.preventDefault(); pianoNoteOff(idx, n); }, {passive:false});
      }
      piano.appendChild(k);
      wIdx++;
    }
    // Black keys — positioned relative to the white key on their left
    for (let n = startNote; n <= endNote; n++) {
      const nio = n % 12;
      if (![1,3,6,8,10].includes(nio)) continue; // skip white
      if (!notesToRender.has(n)) continue;
      // Find the white key just below this black key
      const prevWhite = n - 1; // always a white key for standard black positions
      if (!(prevWhite in whiteIdxMap)) continue;
      const wi = whiteIdxMap[prevWhite];
      const k = document.createElement('div');
      const bMapped = mappedNotes.has(n);
      k.className = 'black';
      if (!bMapped) { k.style.opacity = '0.3'; k.style.cursor = 'default'; }
      k.dataset.note = n;
      k.dataset.inst = idx;
      k.style.left = 'calc(' + (wi + 1) + ' * var(--wk) - var(--wk) * 0.325)';
      if (bMapped) {
        k.onmousedown = (e) => { e.preventDefault(); pianoNoteOn(idx, n); };
        k.onmouseup = () => pianoNoteOff(idx, n);
        k.onmouseleave = () => pianoNoteOff(idx, n);
        k.addEventListener('touchstart', (e) => { e.preventDefault(); pianoNoteOn(idx, n); }, {passive:false});
        k.addEventListener('touchend', (e) => { e.preventDefault(); pianoNoteOff(idx, n); }, {passive:false});
        k.addEventListener('touchcancel', (e) => { e.preventDefault(); pianoNoteOff(idx, n); }, {passive:false});
      }
      piano.appendChild(k);
    }

    const scrollWrap = document.createElement('div');
    scrollWrap.className = 'piano-scroll-wrap';
    const btnL = document.createElement('button');
    btnL.className = 'piano-nav';
    btnL.innerHTML = '&#9664;';
    btnL.onclick = () => { pianoWrap.scrollBy({left: -120, behavior: 'smooth'}); };
    const btnR = document.createElement('button');
    btnR.className = 'piano-nav';
    btnR.innerHTML = '&#9654;';
    btnR.onclick = () => { pianoWrap.scrollBy({left: 120, behavior: 'smooth'}); };

    pianoWrap.appendChild(piano);
    scrollWrap.appendChild(btnL);
    scrollWrap.appendChild(pianoWrap);
    scrollWrap.appendChild(btnR);
    section.appendChild(scrollWrap);
    container.appendChild(section);
  }
}

function pianoNoteOn(instIdx, note) {
  const instMap = pianoNotes[instIdx];
  if (!instMap) return;
  const actId = instMap[note];
  if (actId === undefined) return;
  // Track that this key is being held down
  pressedKeys[instIdx + '-' + note] = true;
  // Visual feedback only on this instrument's piano
  const piano = document.getElementById('piano-' + instIdx);
  if (piano) piano.querySelectorAll('[data-note="' + note + '"]').forEach(k => k.classList.add('active'));
  if (ws && wsConnected) {
    ws.send(JSON.stringify({cmd:'test', id:actId, vel:100}));
  }
}

function pianoNoteOff(instIdx, note) {
  delete pressedKeys[instIdx + '-' + note];
  const piano = document.getElementById('piano-' + instIdx);
  if (piano) piano.querySelectorAll('[data-note="' + note + '"]').forEach(k => k.classList.remove('active'));
  const instMap = pianoNotes[instIdx];
  if (!instMap) return;
  const actId = instMap[note];
  if (actId === undefined) return;
  api('/api/test/actuator', 'POST', {id: actId, velocity: 0, note_on: false});
}

function updatePianoActive(activeActuators) {
  if (currentPage !== 'instrument') return;

  // Reset active states but preserve keys currently held by user
  document.querySelectorAll('.piano .active').forEach(k => {
    const key = k.dataset.inst + '-' + k.dataset.note;
    if (!pressedKeys[key]) k.classList.remove('active');
  });

  if (!activeActuators) return;

  // Highlight the correct key on the correct instrument's piano
  const activeIds = new Set(activeActuators.map(a => a.id));
  for (const [instIdx, noteMap] of Object.entries(pianoNotes)) {
    const piano = document.getElementById('piano-' + instIdx);
    if (!piano) continue;
    for (const [note, actId] of Object.entries(noteMap)) {
      if (activeIds.has(actId)) {
        piano.querySelectorAll('[data-note="' + note + '"]').forEach(k => k.classList.add('active'));
      }
    }
  }
}

// ============================================================================
// Power
// ============================================================================
async function loadPower() {
  const d = await api('/api/power');
  if (!d) return;
  if (d.budget) {
    el('p-poly-max', d.budget.max_polyphony);
    document.getElementById('pw-poly').value = d.budget.max_polyphony;
  }
  if (d.stats) {
    el('p-rejected', d.stats.rejected);
    el('d-active', d.stats.active_count);
    const max = d.budget ? d.budget.max_polyphony : 12;
    const pct = max > 0 ? Math.min(100, Math.round(d.stats.active_count / max * 100)) : 0;
    const bar = document.getElementById('p-total-bar');
    if (bar) { bar.style.width = pct + '%'; bar.style.background = pct > 80 ? 'var(--red)' : 'var(--green)'; }
  }
}

async function savePowerBudget() {
  await api('/api/power/budget', 'POST', {
    max_polyphony: parseInt(document.getElementById('pw-poly').value)
  });
  toast('Polyphony updated', 'ok');
  loadPower();
}

// ============================================================================
// Safety
// ============================================================================
async function loadSafety() {
  const d = await api('/api/safety');
  if (!d) return;
  if (d.config) {
    document.getElementById('sf-duty').value = d.config.max_duty_pct;
    document.getElementById('sf-freq').value = d.config.max_freq_hz;
    document.getElementById('sf-watchdog').value = d.config.watchdog_ms;
  }
}

async function saveSafetyConfig() {
  await api('/api/safety', 'POST', {
    max_duty_pct: parseInt(document.getElementById('sf-duty').value),
    max_freq_hz: parseInt(document.getElementById('sf-freq').value),
    watchdog_ms: parseInt(document.getElementById('sf-watchdog').value),
    max_polyphony: parseInt(document.getElementById('pw-poly').value)
  });
  toast('Limits applied', 'ok');
}

async function toggleKillSwitch(on) {
  await api('/api/killswitch', 'POST', {active: on});
}

// ============================================================================
// Settings
// ============================================================================
async function loadWiFiConfig() {
  const d = await api('/api/wifi');
  if (!d) return;
  document.getElementById('set-ssid').value = d.ssid || '';
  document.getElementById('set-hostname').value = d.hostname || 'play-mode';
  document.getElementById('set-ap-fallback').value = d.ap_fallback ? '1' : '0';
}

async function saveWiFiConfig() {
  await api('/api/wifi', 'POST', {
    ssid: document.getElementById('set-ssid').value,
    password: document.getElementById('set-pass').value,
    hostname: document.getElementById('set-hostname').value,
    ap_fallback: document.getElementById('set-ap-fallback').value === '1',
    enabled: true
  });
  appAlert('WiFi saved', 'Restart the device to apply the new settings.', {icon:'\ud83d\udce1'});
}

async function loadBuses() {
  const buses = await api('/api/buses');
  const tbody = document.getElementById('buses-table');
  if (!buses || buses.length === 0) {
    tbody.innerHTML = '<tr><td colspan="8" style="color:var(--fg2)">No bus detected</td></tr>';
    return;
  }
  let html = '';
  for (const b of buses) {
    const pwmSel50 = b.freq_pwm <= 60 ? ' selected' : '';
    const pwmSel200 = (b.freq_pwm > 60 && b.freq_pwm <= 300) ? ' selected' : '';
    const pwmSel1k = b.freq_pwm > 300 ? ' selected' : '';
    html += '<tr>';
    html += '<td>Bus ' + b.id + '</td>';
    html += '<td>GPIO ' + b.sda + '</td>';
    html += '<td>GPIO ' + b.scl + '</td>';
    html += '<td>GPIO ' + b.oe + '</td>';
    html += '<td>' + (b.freq_i2c / 1000) + ' kHz</td>';
    html += '<td><select class="form-select" style="font-size:12px;min-width:90px" onchange="setBusPwmFreq(' + b.id + ',this.value)">';
    html += '<option value="50"' + pwmSel50 + '>50 Hz (Servo)</option>';
    html += '<option value="200"' + pwmSel200 + '>200 Hz</option>';
    html += '<option value="1000"' + pwmSel1k + '>1000 Hz (Solenoid)</option>';
    html += '</select></td>';
    html += '<td>' + b.pca_count + ' PCA</td>';
    html += '<td>' + (b.enabled ? '<span class="badge on">Active</span>' : '<span class="badge off">Inactive</span>') + '</td>';
    html += '</tr>';
  }
  tbody.innerHTML = html;
}

async function setBusPwmFreq(busId, freq) {
  await api('/api/bus/pwm', 'POST', {bus_id: busId, freq_pwm: parseInt(freq)});
  toast('PWM frequency bus ' + busId + ' set to ' + freq + ' Hz', 'ok');
}

async function scanI2C() {
  const result = await api('/api/scan/i2c', 'POST');
  if (result) {
    let msg = 'I\u00b2C scan result:\n';
    for (const bus of result) {
      msg += 'Bus ' + bus.bus + ': ' + bus.pca_count + ' PCA found';
      if (bus.addresses && bus.addresses.length > 0) {
        msg += ' (' + bus.addresses.join(', ') + ')';
      }
      msg += '\n';
    }
    appAlert('Scan I\u00b2C', msg, {icon:'\ud83d\udd0d'});
    loadBuses();
  }
}

async function saveConfig() {
  const result = await api('/api/config/save', 'POST');
  if (result && result.ok) {
    appAlert('Save successful', 'Configuration saved to flash memory.', {icon:'\u2705'});
  } else {
    appAlert('Error', 'Save failed.', {icon:'\u274c'});
  }
}

async function confirmResetDefaults() {
  if (!await appConfirm('Reset', 'Reset all configuration to default values?\nThis action is irreversible.', {danger:true, confirmText:'Reset', icon:'\u26a0\ufe0f'})) return;
  await api('/api/config/defaults', 'POST');
  await appAlert('Reset complete', 'Configuration reset. The page will reload.', {icon:'\u2705'});
  location.reload();
}

// ============================================================================
// Utils
// ============================================================================
function esc(s) {
  const d = document.createElement('div');
  d.textContent = s;
  return d.innerHTML;
}

// ============================================================================
// Acoustic Calibration (Phase 7)
// ============================================================================
let calPollInterval = null;

async function loadCalibrateStatus() {
  const d = await api('/api/calibrate/status');
  if (!d) return;

  const stateNames = {
    idle:'Inactive', ambient:'Measuring ambient…', triggering:'Triggering…',
    recording:'Recording…', pausing:'Pausing…',
    complete:'Complete \u2713', error:'Error \u2717'
  };
  el('cal-state', stateNames[d.state] || d.state);
  el('cal-progress', d.progress || 0);
  el('cal-result-count', d.result_count || 0);

  const bar = document.getElementById('cal-bar');
  if (bar) bar.style.width = (d.progress || 0) + '%';

  const stopBtn  = document.getElementById('cal-btn-stop');
  const applyBtn = document.getElementById('cal-btn-apply');
  if (stopBtn)  stopBtn.style.display  = d.running ? 'inline-block' : 'none';
  if (applyBtn) applyBtn.style.display = (d.state === 'complete' && d.result_count > 0) ? 'inline-block' : 'none';

  if (d.running) {
    el('cal-cur-act', 'Act ' + d.current_act);
  } else {
    el('cal-cur-act', d.state === 'complete' ? 'Complete' : '—');
  }

  // Stop polling when complete
  if (!d.running && calPollInterval) {
    clearInterval(calPollInterval);
    calPollInterval = null;
    loadCalibrateResults();
  }
}

async function loadCalibrateResults() {
  const data = await api('/api/calibrate/results');
  const tbody = document.getElementById('cal-results-table');
  if (!tbody || !data || !data.length) {
    if (tbody) tbody.innerHTML = '<tr><td colspan="6" style="color:var(--fg2);text-align:center">No results</td></tr>';
    return;
  }

  const typeNames = ['Servo', 'Solenoid'];
  let html = '';
  for (const r of data) {
    const hasMeasure = r.measured_ms !== null && r.measured_ms !== undefined;
    const badge = hasMeasure
      ? (r.success ? '<span class="badge on">OK</span>' : '<span class="badge off">Failed</span>')
      : '<span class="badge" style="background:var(--bg3)">—</span>';

    html += '<tr>';
    html += '<td>' + r.actuator_id + '</td>';
    html += '<td>—</td>';  // type not included in results API
    html += '<td>' + r.current_latency + ' ms</td>';
    html += '<td>' + (hasMeasure && r.success ? '<strong>' + r.measured_ms + ' ms</strong>' : '—') + '</td>';
    html += '<td>' + (hasMeasure ? (r.samples_taken || 0) + '/3' : '—') + '</td>';
    html += '<td>' + badge + '</td>';
    html += '</tr>';
  }
  tbody.innerHTML = html;
}

async function startCalibrateAll() {
  if (!await appConfirm('Calibration', 'Start calibration for all actuators?\nMake sure the microphone is positioned and the environment is quiet.', {icon:'\ud83c\udfaf', confirmText:'Start'})) return;
  api('/api/calibrate', 'POST', { all: true }).then(r => {
    if (r && r.ok) {
      toast('Calibration started', 'ok');
      startCalPoll();
    } else {
      toast('Error: ' + (r && r.error ? r.error : 'unknown'), 'error');
    }
  });
}

function startCalibrateOne() {
  const sel = document.getElementById('cal-single-sel');
  const select = document.getElementById('cal-act-select');
  if (!sel || !select) return;
  // Populate the select with known actuators
  select.innerHTML = '';
  for (const a of actuators) {
    const opt = document.createElement('option');
    opt.value = a.id;
    opt.textContent = 'Act ' + a.id + ' (' + (a.type === 0 ? 'Servo' : 'Sol') + ' ch' + a.pca_ch + ')';
    select.appendChild(opt);
  }
  sel.style.display = 'flex';
  sel.style.alignItems = 'center';
}

function confirmCalibrateOne() {
  const id = parseInt(document.getElementById('cal-act-select').value);
  document.getElementById('cal-single-sel').style.display = 'none';
  api('/api/calibrate', 'POST', { id }).then(r => {
    if (r && r.ok) {
      toast('Calibration started for act ' + id, 'ok');
      startCalPoll();
    } else {
      toast('Error: ' + (r && r.error ? r.error : 'unknown'), 'error');
    }
  });
}

function stopCalibration() {
  api('/api/calibrate/stop', 'POST', {}).then(() => {
    toast('Calibration stopped', 'warn');
    loadCalibrateStatus();
  });
}

async function applyCalibrateResults() {
  const r = await api('/api/calibrate/apply', 'POST', {});
  if (r && r.ok) {
    toast(r.applied + ' latencies applied to actuators', 'ok');
    loadActuators();
    loadCalibrateResults();
  } else {
    toast('Error during application', 'error');
  }
}

function startCalPoll() {
  if (calPollInterval) clearInterval(calPollInterval);
  loadCalibrateStatus();
  calPollInterval = setInterval(loadCalibrateStatus, 1000);
}

function toast(msg, type) {
  const z = document.getElementById('alert-zone');
  if (!z) return;
  const div = document.createElement('div');
  div.className = 'alert ' + (type === 'ok' ? 'ok' : type === 'error' ? 'danger' : 'warn');
  div.textContent = msg;
  z.appendChild(div);
  setTimeout(() => div.remove(), 4000);
}

// ============================================================================
// Log Manager (Phase 9)
// ============================================================================

function logLevelBadge(lvl) {
  const names  = ['DBG','INF','WRN','ERR','CRT'];
  const styles = [
    'color:var(--fg2)',
    'color:var(--green)',
    'color:#f59e0b',
    'color:var(--red)',
    'color:var(--red);font-weight:700'
  ];
  return '<span style="font-size:11px;font-family:monospace;'+(styles[lvl]||'')+'">'+(names[lvl]||'?')+'</span>';
}

function logCatBadge(cat) {
  const names = ['SYS','MIDI','SCHED','SAFE','PWR','CAL','TEST'];
  return '<span style="font-size:11px;color:var(--fg2)">'+(names[cat]||'?')+'</span>';
}

function formatLogTime(ms) {
  const t = Math.floor(ms / 1000);
  const h = Math.floor(t / 3600).toString().padStart(2,'0');
  const m = Math.floor((t % 3600) / 60).toString().padStart(2,'0');
  const s = (t % 60).toString().padStart(2,'0');
  return h+':'+m+':'+s;
}

function escHtml(s) {
  // AUDIT FIX: added quotes for complete XSS protection (HTML attributes)
  return String(s)
    .replace(/&/g,'&amp;')
    .replace(/</g,'&lt;')
    .replace(/>/g,'&gt;')
    .replace(/"/g,'&quot;')
    .replace(/'/g,'&#39;');
}

async function loadLogs() {
  try {
    const r = await api('/api/logs');
    if (r && r.entries) {
      logCache = r.entries;
      renderLogs();
      const info = document.getElementById('log-count-info');
      if (info) info.textContent = logCache.length + ' entry(ies) — ' + r.count + ' total';
    }
  } catch(e) {}
}

function renderLogs() {
  const minLevel  = parseInt(document.getElementById('log-level-filter')?.value  || '0');
  const catFilter = parseInt(document.getElementById('log-cat-filter')?.value     || '-1');
  const tbody = document.getElementById('log-table');
  if (!tbody) return;

  const filtered = logCache.filter(e => e.lvl >= minLevel && (catFilter < 0 || e.cat === catFilter));

  if (filtered.length === 0) {
    tbody.innerHTML = '<tr><td colspan="4" style="color:var(--fg2)">No entries</td></tr>';
    return;
  }

  let html = '';
  for (const e of filtered) {
    html += '<tr>';
    html += '<td style="font-size:11px;font-family:monospace;white-space:nowrap">' + formatLogTime(e.t) + '</td>';
    html += '<td>' + logLevelBadge(e.lvl) + '</td>';
    html += '<td>' + logCatBadge(e.cat) + '</td>';
    html += '<td style="font-size:12px">' + escHtml(e.msg) + '</td>';
    html += '</tr>';
  }
  tbody.innerHTML = html;

  // AUDIT FIX: robust selector via dedicated .log-container class
  if (document.getElementById('log-autoscroll')?.checked) {
    const wrap = tbody.closest('.log-container');
    if (wrap) wrap.scrollTop = wrap.scrollHeight;
  }
}

async function clearLogs() {
  if (!await appConfirm('Clear log', 'Delete all system log entries?', {danger:true, icon:'\ud83d\uddd1\ufe0f'})) return;
  api('/api/logs/clear', 'POST', {}).then(r => {
    if (r && r.ok) {
      logCache = [];
      renderLogs();
      toast('Log cleared', 'ok');
    } else {
      toast('Error', 'error');
    }
  });
}

// ============================================================================
// Expert collapsible toggle
// ============================================================================
function toggleExpert(btn) {
  btn.classList.toggle('open');
  const body = btn.nextElementSibling;
  if (body) body.classList.toggle('open');
}

// Collapsible sections (simplified UI)
function toggleCollapse(btn) {
  btn.classList.toggle('open');
  const body = btn.nextElementSibling;
  if (body) body.classList.toggle('open');
}

// ============================================================================
// Instrument Wizard
// ============================================================================
let wizStep = 1;

function openWizard() {
  wizStep = 1;
  document.getElementById('wiz-name').value = '';
  document.getElementById('wiz-channel').value = instruments ? Math.min(instruments.length + 1, 16) : 1;
  document.getElementById('wiz-type').value = '0';
  wizUpdateBehaviors();
  document.getElementById('wiz-count').value = '8';
  document.getElementById('wiz-start-note').value = '48';
  document.getElementById('wiz-scale').value = 'chromatic';
  document.getElementById('wiz-note-table').innerHTML = '';
  document.getElementById('wiz-pca').value = '64';
  // Auto-detect next free PCA channel
  let startCh = 0;
  if (actuators && actuators.length > 0) {
    const sorted = [...actuators].sort((a,b) =>
      a.pca_addr !== b.pca_addr ? a.pca_addr - b.pca_addr : a.pca_ch - b.pca_ch);
    const last = sorted[sorted.length - 1];
    startCh = last.pca_ch + 1;
    if (startCh > 15) { startCh = 0; }
    document.getElementById('wiz-pca').value = last.pca_addr;
  }
  document.getElementById('wiz-start-ch').value = startCh;
  wizShowStep();
  document.getElementById('modal-wizard').classList.add('show');
}

function wizUpdateBehaviors() {
  const type = document.getElementById('wiz-type').value;
  const sel = document.getElementById('wiz-behavior');
  sel.innerHTML = '';
  const behaviors = type === '0'
    ? [{v:0,t:'Strike (quick back-and-forth)'},{v:1,t:'Alternate (toggle A/B)'},{v:2,t:'Strum (continuous motion)'},{v:3,t:'Key (hold while pressed)'}]
    : [{v:0,t:'Strike (short pulse)'},{v:1,t:'Hit-and-Hold (strike then hold)'}];
  for (const b of behaviors) {
    const opt = document.createElement('option');
    opt.value = b.v;
    opt.textContent = b.t;
    sel.appendChild(opt);
  }
}

function wizBuildNoteTable() {
  const count = Math.min(parseInt(document.getElementById('wiz-count').value) || 8, 64);
  const startNote = parseInt(document.getElementById('wiz-start-note').value) || 48;
  const scaleKey = document.getElementById('wiz-scale').value;
  const container = document.getElementById('wiz-note-table');
  if (!container) return;

  const scale = SCALES[scaleKey];
  let notes = [];
  if (scale) {
    const iv = scale.intervals;
    let idx = 0, octave = 0;
    for (let i = 0; i < count; i++) {
      if (idx >= iv.length) { idx = 0; octave++; }
      notes.push(Math.min(startNote + octave * 12 + iv[idx], 127));
      idx++;
    }
  } else {
    for (let i = 0; i < count; i++) notes.push(Math.min(startNote + i, 127));
  }

  let html = '<table><thead><tr><th style="width:60px">#</th><th style="width:80px">Note</th><th>Name</th></tr></thead><tbody>';
  for (let i = 0; i < count; i++) {
    html += '<tr>';
    html += '<td>Act ' + i + '</td>';
    html += '<td><input type="number" min="0" max="127" value="' + notes[i] + '" id="wiz-note-' + i + '" class="note-input" oninput="wizUpdateNoteName(' + i + ')"></td>';
    html += '<td id="wiz-nname-' + i + '">' + noteName(notes[i]) + '</td>';
    html += '</tr>';
  }
  html += '</tbody></table>';
  container.innerHTML = html;
}

function wizUpdateNoteName(i) {
  const input = document.getElementById('wiz-note-' + i);
  const label = document.getElementById('wiz-nname-' + i);
  if (input && label) {
    const n = parseInt(input.value);
    label.textContent = (n >= 0 && n <= 127) ? noteName(n) : '?';
  }
}

function wizShowStep() {
  for (let i = 1; i <= 4; i++) {
    document.getElementById('wiz-step-' + i).style.display = i === wizStep ? 'block' : 'none';
    const dot = document.getElementById('wiz-dot-' + i);
    dot.className = 'wiz-dot' + (i === wizStep ? ' active' : i < wizStep ? ' done' : '');
    const item = document.getElementById('wiz-item-' + i);
    if (item) item.className = 'wiz-step-item' + (i === wizStep ? ' active' : '');
    const conn = document.getElementById('wiz-conn-' + i);
    if (conn) conn.className = 'wiz-connector' + (i < wizStep ? ' done' : '');
  }
  // Build note table when entering step 3 for the first time
  if (wizStep === 3 && !document.getElementById('wiz-note-table').querySelector('table')) {
    wizBuildNoteTable();
  }
  document.getElementById('wiz-prev').style.display = wizStep > 1 ? 'inline-flex' : 'none';
  const nextBtn = document.getElementById('wiz-next');
  if (wizStep === 4) {
    nextBtn.textContent = 'Create';
    nextBtn.className = 'btn primary';
    wizBuildSummary();
  } else {
    nextBtn.textContent = 'Next \u2192';
    nextBtn.className = 'btn primary';
  }
}

function wizBuildSummary() {
  const name = document.getElementById('wiz-name').value || 'Instrument';
  const ch = document.getElementById('wiz-channel').value;
  const type = document.getElementById('wiz-type').value;
  const typeName = type === '0' ? 'Servo motor' : 'Solenoid';
  const behavior = document.getElementById('wiz-behavior').selectedOptions[0]?.textContent || '';
  const count = parseInt(document.getElementById('wiz-count').value);
  const pca = document.getElementById('wiz-pca').selectedOptions[0]?.textContent || '';
  const startCh = document.getElementById('wiz-start-ch').value;

  // Read notes from table
  let noteNames = [];
  for (let i = 0; i < count; i++) {
    const inp = document.getElementById('wiz-note-' + i);
    if (inp) noteNames.push(noteName(parseInt(inp.value)));
  }
  const noteStr = noteNames.length <= 12
    ? noteNames.join(', ')
    : noteNames.slice(0, 10).join(', ') + ' ... (+' + (noteNames.length - 10) + ')';

  let html = '<strong>' + esc(name) + '</strong> — MIDI Channel ' + (ch === '0' ? 'Omni (all)' : ch) + '<br>';
  html += 'Type: <strong>' + typeName + '</strong> — ' + behavior + '<br>';
  html += count + ' actuators<br>';
  html += 'Notes: ' + noteStr + '<br>';
  html += 'PCA: ' + pca + ' — Channels ' + startCh + ' \u2192 ' + (parseInt(startCh) + count - 1) + '<br>';
  html += 'Bus: <strong>' + (type === '0' ? 'Bus 0 (Servos)' : 'Bus 1 (Solenoids)') + '</strong>';
  document.getElementById('wiz-summary').innerHTML = html;
}

function wizPrev() {
  if (wizStep > 1) { wizStep--; wizShowStep(); }
}

async function wizNext() {
  // Per-step validation before advancing
  if (wizStep === 1) {
    const name = document.getElementById('wiz-name').value.trim();
    if (!name) { toast('Please enter a name for the instrument', 'error'); return; }
  }
  if (wizStep === 3) {
    const count = parseInt(document.getElementById('wiz-count').value);
    if (isNaN(count) || count < 1 || count > 64) { toast('Invalid actuator count (1–64)', 'error'); return; }
  }
  if (wizStep < 4) { wizStep++; wizShowStep(); return; }
  // Step 4 → Create everything
  const name = document.getElementById('wiz-name').value.trim() || 'Instrument';
  const channel = parseInt(document.getElementById('wiz-channel').value);
  const type = parseInt(document.getElementById('wiz-type').value);
  const behavior = parseInt(document.getElementById('wiz-behavior').value);
  const count = parseInt(document.getElementById('wiz-count').value);
  const startNote = parseInt(document.getElementById('wiz-start-note').value);
  let pcaAddr = parseInt(document.getElementById('wiz-pca').value);
  let pcaCh = parseInt(document.getElementById('wiz-start-ch').value);
  const busId = type === 0 ? 0 : 1;

  // 1. Create instrument
  const instData = {name, channel, bus_id: busId, latency_ms: 10, auto_cal: false, enabled: true};
  await api('/api/instrument', 'POST', instData);

  // 2. Create actuators — baseId = max(existing IDs) + 1 to ensure uniqueness
  const baseId = (actuators && actuators.length > 0)
    ? Math.max(...actuators.map(a => a.id)) + 1
    : 0;
  for (let i = 0; i < count; i++) {
    const actData = {
      id: baseId + i, type, bus_id: busId, pca_addr: pcaAddr, pca_ch: pcaCh,
      latency_ms: 10, behavior, enabled: true
    };
    if (type === 0) {
      actData.angle_init = 90; actData.amplitude = 45; actData.speed_ms = 150; actData.angle_b = 120;
    } else {
      actData.pulse_min_ms = 5; actData.pulse_max_ms = 30; actData.pulse_ms = 30; actData.pwm_initial = 4095; actData.pwm_hold = 2048; actData.ramp_ms = 50;
    }
    await api('/api/actuator', 'POST', actData);
    pcaCh++;
    if (pcaCh > 15) { pcaCh = 0; pcaAddr = Math.min(pcaAddr + 1, 67); }
  }

  // 3. Create note mappings (read from editable table)
  instruments = await api('/api/instruments');
  const instIdx = instruments.length > 0 ? instruments[instruments.length - 1].index : 0;
  const notes = [];
  for (let i = 0; i < count; i++) {
    const noteInput = document.getElementById('wiz-note-' + i);
    const note = noteInput ? parseInt(noteInput.value) : (startNote + i);
    if (isNaN(note) || note < 0 || note > 127) continue;
    notes.push({note, actuator: baseId + i, enabled: true});
  }
  await api('/api/routing', 'POST', {instrument: instIdx, notes});

  closeModal('modal-wizard');
  toast(name + ' created with ' + count + ' actuators', 'ok');

  // Refresh everything
  await loadActuators();
  routing = await api('/api/routing') || [];
  loadHomeInstruments();
  loadInstrumentSelects();
  buildAllPianos();
  updateCountBadges();

  // After creation, always go to instrument page (exit welcome if needed)
  showPage('instrument');
}

// ============================================================================
// Servo angle visual preview
// ============================================================================
function updateAnglePreview() {
  const preview = document.getElementById('angle-preview');
  if (!preview || document.getElementById('servo-fields').style.display === 'none') return;
  const mode = document.getElementById('ma-servo-behavior').value;
  const isAlterne = mode === '1';
  const cx=60,cy=55,r=40;
  const toX=(deg)=>cx+r*Math.cos((180-deg)*Math.PI/180);
  const toY=(deg)=>cy-r*Math.sin((180-deg)*Math.PI/180);
  let s='<svg width="120" height="70" viewBox="0 0 120 70">';
  s+='<path d="M '+(cx-r)+' '+cy+' A '+r+' '+r+' 0 0 1 '+(cx+r)+' '+cy+'" fill="none" stroke="var(--bg3)" stroke-width="3"/>';

  if (isAlterne) {
    // Alternate: show angle A and angle B as two positions
    const angleA = parseInt(document.getElementById('ma-angle-a-alt').value) || 90;
    const angleB = parseInt(document.getElementById('ma-angle-b').value) || 120;
    const lo = Math.min(angleA, angleB), hi = Math.max(angleA, angleB);
    const la=(hi-lo)>180?1:0;
    const x1=toX(lo),y1=toY(lo),x2=toX(hi),y2=toY(hi);
    s+='<path d="M '+x1+' '+y1+' A '+r+' '+r+' 0 '+la+' 1 '+x2+' '+y2+'" fill="none" stroke="var(--yellow)" stroke-width="3"/>';
    const xa=toX(angleA),ya=toY(angleA),xb=toX(angleB),yb=toY(angleB);
    s+='<line x1="'+cx+'" y1="'+cy+'" x2="'+xa+'" y2="'+ya+'" stroke="var(--green)" stroke-width="2"/>';
    s+='<line x1="'+cx+'" y1="'+cy+'" x2="'+xb+'" y2="'+yb+'" stroke="var(--yellow)" stroke-width="2"/>';
    s+='<circle cx="'+cx+'" cy="'+cy+'" r="3" fill="var(--fg)"/>';
    s+='<text x="2" y="68" font-size="9" fill="var(--fg2)">180</text>';
    s+='<text x="104" y="68" font-size="9" fill="var(--fg2)">0</text>';
    s+='</svg>';
    s+='<div class="angle-info">';
    s+='A: <span style="color:var(--green)">'+angleA+'&deg;</span>';
    s+=' B: <span style="color:var(--yellow)">'+angleB+'&deg;</span>';
    s+='</div>';
  } else {
    // Strike / Strum / Key: show idle + amplitude arc
    const init = parseInt(document.getElementById('ma-angle-init').value) || 90;
    const amp = parseInt(document.getElementById('ma-amplitude').value) || 45;
    const reverse = document.getElementById('ma-hit-reverse').value === '1';
    let minA, maxA;
    if (mode === '2') { // Strum: bidirectional
      minA = Math.max(0, init - amp);
      maxA = Math.min(180, init + amp);
    } else if (reverse) {
      minA = Math.max(0, init - amp);
      maxA = init;
    } else {
      minA = init;
      maxA = Math.min(180, init + amp);
    }
    const la=(maxA-minA)>180?1:0;
    const x1=toX(minA),y1=toY(minA),x2=toX(maxA),y2=toY(maxA);
    const xi=toX(init),yi=toY(init);
    s+='<path d="M '+x1+' '+y1+' A '+r+' '+r+' 0 '+la+' 1 '+x2+' '+y2+'" fill="none" stroke="var(--accent)" stroke-width="3"/>';
    s+='<line x1="'+cx+'" y1="'+cy+'" x2="'+xi+'" y2="'+yi+'" stroke="var(--green)" stroke-width="2"/>';
    s+='<circle cx="'+cx+'" cy="'+cy+'" r="3" fill="var(--fg)"/>';
    s+='<text x="2" y="68" font-size="9" fill="var(--fg2)">180</text>';
    s+='<text x="104" y="68" font-size="9" fill="var(--fg2)">0</text>';
    s+='</svg>';
    s+='<div class="angle-info">';
    s+='Idle: <span>'+init+'&deg;</span>';
    s+=' Range: <span>'+minA+'&deg;&rarr;'+maxA+'&deg;</span>';
    s+='</div>';
  }
  preview.innerHTML=s;
}

// ============================================================================
// Microphone detection for calibration
// ============================================================================
async function checkMicStatus() {
  const navBtn = document.getElementById('nav-cal');
  const micDiv = document.getElementById('mic-status');
  try {
    const d = await api('/api/calibrate/status');
    if (d && d.available) {
      if (navBtn) navBtn.style.display = '';
      if (micDiv) {
        micDiv.className = 'mic-status ok';
        micDiv.innerHTML = '&#9679; Microphone detected \u2014 Ready for calibration';
      }
    } else {
      if (navBtn) navBtn.style.display = 'none';
    }
  } catch(e) {
    if (navBtn) navBtn.style.display = 'none';
  }
}

// ============================================================================
// Init
// ============================================================================
// Update count badges in collapsible sections
function updateCountBadges() {
  const actBadge = document.getElementById('act-count-badge');
  if (actBadge) actBadge.textContent = actuators ? '(' + actuators.length + ')' : '';
  const ccBadge = document.getElementById('cc-count-badge');
  if (ccBadge && routing) {
    let ccCount = 0;
    for (const r of routing) { if (r.ccs) ccCount += r.ccs.length; }
    ccBadge.textContent = ccCount > 0 ? '(' + ccCount + ')' : '';
  }
}

window.addEventListener('load', async () => {
  connectWS();
  // Preload data then decide which page to show
  await loadActuators();
  instruments = await api('/api/instruments') || [];
  routing = await api('/api/routing') || [];
  loadInstrumentSelects();
  loadHomeInstruments_render();
  buildAllPianos();
  updateCountBadges();
  checkMicStatus();

  // First-run detection: show welcome only if truly no instruments
  if (!instruments || instruments.length === 0) {
    showPage('welcome');
  } else {
    showPage('instrument');
  }
});
</script>
</body>
</html>
)rawhtml";

#endif // WEB_UI_H
