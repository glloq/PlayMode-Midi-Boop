#ifndef WEB_UI_H
#define WEB_UI_H

#include <Arduino.h>

// ============================================================================
// PlayMode Midi B∞p — Embedded Web UI (Phase 6)
// ============================================================================
//
// Interface HTML/CSS/JS intégrée en PROGMEM.
// Single-page app avec :
//   - Dashboard temps réel (WebSocket)
//   - Gestion instruments / actionneurs / MIDI mapping
//   - Piano virtuel interactif
//   - Monitoring power / safety
//   - Configuration avancée
//

const char WEB_UI_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Midi B∞p</title>
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
  padding:12px 20px;display:flex;align-items:center;gap:16px;flex-wrap:wrap}
.header h1{font-size:18px;font-weight:600}
.header h1 span{color:var(--accent)}
.header .status{margin-left:auto;font-size:12px;color:var(--fg2);
  white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
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

/* Piano — touches plus larges pour le tactile */
.piano-container{overflow-x:auto;padding:12px 0;-webkit-overflow-scrolling:touch}
.piano{display:flex;position:relative;height:130px;user-select:none;touch-action:none}
.piano .white{width:40px;height:130px;background:#f0f0f0;border:1px solid #999;
  border-radius:0 0 4px 4px;cursor:pointer;position:relative;z-index:1;
  display:flex;align-items:flex-end;justify-content:center;padding-bottom:4px;
  font-size:9px;color:#666;transition:background .1s;
  -webkit-user-select:none;user-select:none;-webkit-touch-callout:none}
.piano .white:hover{background:#e0e8f0}
.piano .white.active{background:var(--accent);color:#fff}
.piano .white.mapped{background:#d0e8ff}
.piano .white.muted{background:#e8e8e8;color:#bbb;cursor:default;opacity:.5}
.piano .white.muted:hover{background:#e8e8e8}
.piano .black{width:26px;height:85px;background:#222;border:1px solid #000;
  border-radius:0 0 3px 3px;cursor:pointer;position:absolute;z-index:2;
  margin-left:-13px;transition:background .1s;
  -webkit-user-select:none;user-select:none;-webkit-touch-callout:none}
.piano .black:hover{background:#444}
.piano .black.active{background:var(--accent)}
.piano .black.mapped{background:#2a5a8f}
.piano .black.muted{background:#666;opacity:.4;cursor:default}
.piano .black.muted:hover{background:#666}

/* Section titles — flexbox pour alignement mobile */
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
  .header .status{font-size:10px;flex-wrap:wrap}
  .log-container{max-height:calc(100vh - 150px)}
  .modal{padding:14px;width:95%}
  .section-title{font-size:14px;flex-wrap:wrap;gap:6px}
  .btn-row{flex-wrap:wrap;gap:6px}
  .table-responsive{overflow-x:auto;-webkit-overflow-scrolling:touch}
  table{font-size:12px;min-width:500px}
  table th,table td{padding:6px 4px;white-space:nowrap}
  .piano-container{overflow-x:auto;-webkit-overflow-scrolling:touch;padding-bottom:8px}
  .piano{min-width:auto}
  .piano .white{width:28px;font-size:7px}
  .piano .black{width:20px;height:70px}
  .welcome h2{font-size:22px}
  .welcome-steps{flex-direction:column;align-items:center}
  .welcome-step{max-width:100%;min-width:auto}
  .section-collapse-toggle{font-size:13px;padding:10px 12px}
}
@media(max-width:380px){
  .cards{grid-template-columns:1fr}
  .header .status span:not(#ws-status){display:none}
  .piano .white{width:22px;font-size:6px}
  .piano .black{width:16px;height:60px}
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
  <h1>Midi <span>B&infin;p</span></h1>
  <div class="status">
    <span class="dot" id="ws-dot"></span>
    <span id="ws-status">Connexion...</span>
    &nbsp;|&nbsp; Heap: <span id="heap">-</span>
    &nbsp;|&nbsp; Uptime: <span id="uptime">-</span>
  </div>
  <button class="gear-btn" onclick="showPage('settings')" title="R&eacute;glages syst&egrave;me">&#9881;</button>
</div>

<!-- Navigation -->
<nav id="main-nav">
  <button class="active" onclick="showPage('instrument')">Instrument</button>
  <button onclick="showPage('midi')">MIDI</button>
  <button onclick="showPage('actuators')">Actionneurs</button>
  <button onclick="showPage('calibration')" id="nav-cal" style="display:none">Calibration</button>
</nav>

<!-- ============ WELCOME (First-run) ============ -->
<div class="page" id="page-welcome">
  <div class="welcome">
    <h2>Bienvenue sur Midi <span>B&infin;p</span></h2>
    <p class="subtitle">Transformez n'importe quel objet en instrument de musique MIDI.<br>
    Servos, sol&eacute;no&iuml;des, percussions... tout est possible.</p>
    <div class="welcome-steps">
      <div class="welcome-step">
        <div class="step-num">1</div>
        <h3>Cr&eacute;er</h3>
        <p>D&eacute;finissez votre instrument et ses actionneurs</p>
      </div>
      <div class="welcome-step">
        <div class="step-num">2</div>
        <h3>Connecter</h3>
        <p>Branchez vos servos ou sol&eacute;no&iuml;des via PCA9685</p>
      </div>
      <div class="welcome-step">
        <div class="step-num">3</div>
        <h3>Jouer</h3>
        <p>Envoyez du MIDI et laissez la musique op&eacute;rer</p>
      </div>
    </div>
    <button class="btn primary btn-big" onclick="openWizard()">Cr&eacute;er mon premier instrument</button>
    <p style="color:var(--fg2);font-size:12px;margin-top:16px">Ou <a href="#" onclick="showPage('instrument');return false">passer &agrave; la configuration manuelle</a></p>
  </div>
</div>

<!-- ============ INSTRUMENT (Instruments + independent pianos) ============ -->
<div class="page active" id="page-instrument">
  <div id="alert-zone"></div>

  <div class="section-title"><span>Mes instruments</span>
    <div style="display:flex;gap:8px">
      <button class="btn primary sm" onclick="openWizard()">+ Assistant</button>
      <button class="btn sm" onclick="openInstrumentModal()">+ Manuel</button>
    </div>
  </div>
  <div class="table-responsive">
  <table>
    <thead><tr><th>Nom</th><th>Canal</th><th>Type</th><th>Actionneurs</th><th>&Eacute;tat</th><th>Actions</th></tr></thead>
    <tbody id="home-instruments-table"><tr><td colspan="6" style="color:var(--fg2)">Chargement...</td></tr></tbody>
  </table>
  </div>

  <!-- One piano per instrument (generated dynamically) -->
  <div id="pianos-container"></div>
</div>

<!-- ============ SETTINGS (unified gear menu page) ============ -->
<div class="page" id="page-settings">

  <!-- Dashboard / Monitoring -->
  <div class="section-title">Monitoring</div>
  <div class="cards">
    <div class="card">
      <h3>MIDI Re&ccedil;us</h3>
      <div class="val" id="d-midi-recv">0</div>
      <div class="sub">Rout&eacute;s: <span id="d-midi-routed">0</span> | Non mapp&eacute;s: <span id="d-midi-unmapped">0</span></div>
    </div>
    <div class="card">
      <h3>Scheduler</h3>
      <div class="val" id="d-sched-queued">0</div>
      <div class="sub">en file | <span id="d-sched-processed">0</span> trait&eacute;s</div>
    </div>
    <div class="card">
      <h3>Actifs</h3>
      <div class="val" id="d-active">0</div>
      <div class="sub">actionneurs actifs</div>
    </div>
    <div class="card">
      <h3>WiFi</h3>
      <div class="val" id="d-wifi-rssi">-</div>
      <div class="sub" id="d-wifi-status">-</div>
    </div>
    <div class="card">
      <h3>Rejet&eacute;s</h3>
      <div class="val" id="d-pwr-rejected">0</div>
      <div class="sub">&eacute;v&eacute;nements rejet&eacute;s</div>
    </div>
  </div>

  <!-- Polyphonie -->
  <div class="section-title" style="margin-top:24px">Polyphonie</div>
  <div class="cards" style="grid-template-columns:repeat(auto-fit,minmax(140px,1fr))">
    <div class="card">
      <h3>Actifs / Max</h3>
      <div class="val"><span id="p-poly">0</span><span class="unit">/ <span id="p-poly-max">12</span></span></div>
      <div class="bar"><div class="bar-fill" id="p-total-bar" style="width:0%;background:var(--green)"></div></div>
    </div>
    <div class="card">
      <h3>Rejet&eacute;s</h3>
      <div class="val" id="p-rejected">0</div>
    </div>
  </div>
  <div class="form-row" style="margin-top:12px">
    <div class="form-group">
      <label>Polyphonie max</label>
      <input type="number" id="pw-poly" value="12" min="1" max="32">
      <div class="help">Nombre max d'actionneurs actifs en m&ecirc;me temps</div>
    </div>
  </div>
  <button class="btn primary sm" onclick="savePowerBudget()">Appliquer</button>

  <!-- Safety -->
  <div class="section-title" style="margin-top:24px">S&eacute;curit&eacute;</div>
  <div id="safety-alert-zone"></div>
  <div class="cards" style="grid-template-columns:repeat(auto-fit,minmax(120px,1fr))">
    <div class="card">
      <h3>Actifs</h3>
      <div class="val" id="s-active">0</div>
    </div>
    <div class="card">
      <h3>Kill Switch</h3>
      <div class="val" id="s-kill" style="color:var(--green)">OFF</div>
    </div>
    <div class="card">
      <h3>D&eacute;gradation</h3>
      <div class="val" id="s-degrad" style="color:var(--green)">Non</div>
    </div>
  </div>
  <div class="btn-row" style="margin-bottom:12px">
    <button class="btn danger sm" onclick="toggleKillSwitch(true)">KILL SWITCH ON</button>
    <button class="btn sm" onclick="toggleKillSwitch(false)">Kill Switch Off</button>
  </div>

  <div class="section-collapse">
    <button class="section-collapse-toggle" onclick="toggleCollapse(this)">
      <span>Limites de s&eacute;curit&eacute; (avanc&eacute;)</span>
    </button>
    <div class="section-collapse-body">
      <div class="form-row tri">
        <div class="form-group">
          <label>Duty cycle max (%)</label>
          <input type="number" id="sf-duty" value="80" min="10" max="100">
        </div>
        <div class="form-group">
          <label>Fr&eacute;quence max (Hz)</label>
          <input type="number" id="sf-freq" value="50" min="1" max="200">
        </div>
        <div class="form-group">
          <label>Watchdog timeout (ms)</label>
          <input type="number" id="sf-watchdog" value="5000" min="1000" max="30000">
        </div>
      </div>
      <div class="form-row">
        <div class="form-group">
          <label>Polyphonie max (s&eacute;curit&eacute;)</label>
          <input type="number" id="sf-poly" value="12" min="1" max="32">
        </div>
      </div>
      <button class="btn primary sm" onclick="saveSafetyConfig()">Appliquer limites</button>
    </div>
  </div>

  <!-- Logs -->
  <div class="section-title" style="margin-top:24px"><span>Journal syst&egrave;me</span>
    <div style="display:flex;gap:6px">
      <button class="btn sm" onclick="loadLogs()">Actualiser</button>
      <button class="btn sm" onclick="clearLogs()">Effacer</button>
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
      <option value="-1">Toutes</option>
      <option value="0">Syst&egrave;me</option>
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
      <thead><tr><th style="width:80px">Temps</th><th style="width:50px">Niveau</th><th style="width:65px">Cat</th><th>Message</th></tr></thead>
      <tbody id="log-table"><tr><td colspan="4" style="color:var(--fg2)">Chargement...</td></tr></tbody>
    </table>
    </div>
  </div>
  <div id="log-count-info" style="color:var(--fg2);font-size:12px;margin-top:6px;text-align:right"></div>

  <!-- WiFi -->
  <div class="section-title" style="margin-top:24px">Connexion WiFi</div>
  <div class="form-row">
    <div class="form-group">
      <label>SSID du r&eacute;seau</label>
      <input type="text" id="set-ssid" maxlength="32" placeholder="Nom du WiFi">
    </div>
    <div class="form-group">
      <label>Mot de passe</label>
      <input type="password" id="set-pass" maxlength="64">
    </div>
  </div>
  <div class="form-row">
    <div class="form-group">
      <label>Hostname</label>
      <input type="text" id="set-hostname" value="midi-boop" maxlength="31">
      <div class="help">Accessible via hostname.local sur le r&eacute;seau</div>
    </div>
    <div class="form-group">
      <label>AP Fallback</label>
      <select id="set-ap-fallback">
        <option value="1">Oui &mdash; cr&eacute;e un point d'acc&egrave;s si WiFi &eacute;choue</option>
        <option value="0">Non</option>
      </select>
    </div>
  </div>
  <button class="btn primary" onclick="saveWiFiConfig()">Sauvegarder WiFi</button>

  <!-- Bus I&sup2;C -->
  <div class="section-collapse" style="margin-top:24px">
    <button class="section-collapse-toggle" onclick="toggleCollapse(this)">
      <span>Bus I&sup2;C (avanc&eacute;)</span>
    </button>
    <div class="section-collapse-body">
      <div class="table-responsive">
      <table>
        <thead><tr><th>Bus</th><th>SDA</th><th>SCL</th><th>OE</th><th>Freq I&sup2;C</th><th>Freq PWM</th><th>PCA d&eacute;tect&eacute;s</th><th>&Eacute;tat</th></tr></thead>
        <tbody id="buses-table"><tr><td colspan="8" style="color:var(--fg2)">Chargement...</td></tr></tbody>
      </table>
      </div>
      <button class="btn" onclick="scanI2C()">Scanner bus I&sup2;C</button>
    </div>
  </div>

  <!-- Config -->
  <div class="section-title" style="margin-top:24px">Configuration</div>
  <div class="btn-row">
    <button class="btn primary" onclick="saveConfig()">Sauvegarder sur flash</button>
    <button class="btn danger" onclick="if(confirm('Remettre les d&eacute;fauts?'))resetDefaults()">R&eacute;initialiser</button>
  </div>
  <div class="sub" style="margin-top:8px">Version config: <span id="set-version">-</span></div>
</div>

<!-- ============ MIDI (Entr&eacute;es MIDI + Messages re&ccedil;us) ============ -->
<div class="page" id="page-midi">
  <div class="section-title">Entr&eacute;es MIDI</div>
  <p style="color:var(--fg2);font-size:12px;margin-bottom:12px">Choisissez comment Midi B&infin;p re&ccedil;oit les messages MIDI. Plusieurs entr&eacute;es peuvent &ecirc;tre actives en m&ecirc;me temps.</p>
  <div class="cards" style="margin-bottom:16px">
    <div class="card">
      <h3>C&acirc;ble MIDI (DIN / TRS)</h3>
      <label><input type="checkbox" id="midi-serial" onchange="updateMidiConfig()"> Actif</label>
      <div class="sub">Connexion filaire classique via prise MIDI 5 broches ou jack TRS.</div>
      <div class="sub" style="margin-top:4px;font-size:11px;color:var(--fg2)">GPIO <span id="midi-rx-pin">4</span> &mdash; 31250 baud</div>
    </div>
    <div class="card">
      <h3>WiFi &mdash; envoi direct (UDP)</h3>
      <label><input type="checkbox" id="midi-udp" onchange="updateMidiConfig()"> Actif</label>
      <div class="sub">Envoyer des messages MIDI depuis un logiciel vers l'IP de Midi B&infin;p. Simple mais sans synchronisation.</div>
      <div class="sub" style="margin-top:4px;font-size:11px;color:var(--fg2)">Port <span id="midi-udp-port">5004</span> &mdash; N&eacute;cessite WiFi</div>
    </div>
    <div class="card">
      <h3>WiFi &mdash; Apple / RTP-MIDI</h3>
      <label><input type="checkbox" id="midi-rtp" onchange="updateMidiConfig()"> Actif</label>
      <div class="sub">Compatible macOS, iOS, rtpMIDI (Windows). Appara&icirc;t automatiquement dans les logiciels MIDI. Synchronis&eacute;.</div>
      <div class="sub" style="margin-top:4px;font-size:11px;color:var(--fg2)">Port <span id="midi-rtp-port">5004</span> &mdash; N&eacute;cessite WiFi</div>
    </div>
    <div class="card">
      <h3>Jitter Buffer</h3>
      <div class="val"><span id="midi-jitter-val">30</span><span class="unit">ms</span></div>
      <input type="range" id="midi-jitter" min="10" max="80" value="30" style="width:100%;margin-top:8px"
        oninput="document.getElementById('midi-jitter-val').textContent=this.value"
        onchange="updateMidiConfig()">
      <div class="help">Tampon anti-gigue pour le MIDI r&eacute;seau (UDP / RTP). Augmentez si les notes arrivent dans le d&eacute;sordre.</div>
    </div>
  </div>

  <!-- Derniers messages MIDI re&ccedil;us -->
  <div class="section-title" style="margin-top:24px"><span>Messages MIDI re&ccedil;us</span>
    <div style="display:flex;gap:6px;align-items:center">
      <label style="font-size:12px;display:flex;align-items:center;gap:4px;color:var(--fg2)"><input type="checkbox" id="midi-log-pause"> Pause</label>
      <button class="btn sm" onclick="clearMidiLog()">Effacer</button>
    </div>
  </div>
  <div class="table-responsive" style="max-height:320px;overflow-y:auto" id="midi-log-scroll">
  <table>
    <thead><tr><th style="width:70px">Temps</th><th>Source</th><th>Canal</th><th>Type</th><th>Donn&eacute;es</th><th>Rout&eacute;</th></tr></thead>
    <tbody id="midi-log-table"><tr><td colspan="6" style="color:var(--fg2)">En attente de messages MIDI...</td></tr></tbody>
  </table>
  </div>
  <div id="midi-log-count" style="color:var(--fg2);font-size:11px;margin-top:4px;text-align:right"></div>
</div>

<!-- (Config is now in the unified Settings page) -->

<!-- ============ MODALS ============ -->

<!-- Modal Instrument -->
<div class="modal-overlay" id="modal-instrument">
  <div class="modal">
    <h2 id="modal-inst-title">Nouvel instrument</h2>
    <div class="form-group">
      <label>Nom de l'instrument</label>
      <input type="text" id="mi-name" maxlength="31" placeholder="Ex: Xylophone, Caisse claire...">
      <div class="help">Nom libre pour identifier cet instrument dans l'interface</div>
    </div>
    <div class="form-row">
      <div class="form-group">
        <label>Canal MIDI (0=Omni, 1-16)</label>
        <input type="number" id="mi-channel" min="0" max="16" value="0">
        <div class="help">0 = &eacute;coute tous les canaux, 1-16 = canal sp&eacute;cifique</div>
      </div>
      <div class="form-group">
        <label>Bus I&sup2;C</label>
        <select id="mi-bus"><option value="0">Bus 0 (Servos)</option><option value="1">Bus 1 (Sol&eacute;no&iuml;des)</option></select>
        <div class="help">Bus physique auquel sont connect&eacute;s les actionneurs</div>
      </div>
    </div>
    <div class="form-row">
      <div class="form-group">
        <label>Latence (ms)</label>
        <input type="number" id="mi-latency" value="10" min="0" max="500">
        <div class="help">D&eacute;lai de compensation entre r&eacute;ception MIDI et d&eacute;clenchement</div>
      </div>
      <div class="form-group">
        <label>Auto-calibration micro</label>
        <select id="mi-autocal"><option value="0">Non</option><option value="1">Oui</option></select>
        <div class="help">Mesure automatique de la latence via microphone I&sup2;S</div>
      </div>
    </div>
    <div class="btn-row">
      <button class="btn primary" onclick="saveInstrument()">Sauvegarder</button>
      <button class="btn" onclick="closeModal('modal-instrument')">Annuler</button>
    </div>
  </div>
</div>

<!-- Modal Actuator -->
<div class="modal-overlay" id="modal-actuator">
  <div class="modal">
    <h2 id="modal-act-title">Nouvel actionneur</h2>
    <div class="form-row">
      <div class="form-group">
        <label>ID actionneur</label>
        <input type="number" id="ma-id" min="0" max="31" value="0">
        <div class="help">Identifiant unique (0-31), attribu&eacute; automatiquement</div>
      </div>
      <div class="form-group">
        <label>Type d'actionneur</label>
        <select id="ma-type" onchange="toggleActuatorFields()"><option value="0">Servo-moteur</option><option value="1">Sol&eacute;no&iuml;de</option></select>
        <div class="help">Servo = mouvement rotatif | Sol&eacute;no&iuml;de = frappe lin&eacute;aire</div>
      </div>
    </div>
    <div class="form-row">
      <div class="form-group">
        <label>Bus I&sup2;C</label>
        <select id="ma-bus"><option value="0">Bus 0</option><option value="1">Bus 1</option></select>
        <div class="help">Bus I&sup2;C physique (fr&eacute;quence PWM configurable dans MIDI &gt; I&sup2;C)</div>
      </div>
      <div class="form-group">
        <label>Carte PCA9685</label>
        <select id="ma-pca"><option value="64">0x40 (carte 1)</option><option value="65">0x41 (carte 2)</option><option value="66">0x42 (carte 3)</option><option value="67">0x43 (carte 4)</option></select>
        <div class="help">Adresse I&sup2;C de la carte PWM (16 canaux chacune)</div>
      </div>
    </div>
    <div class="form-group">
      <label>Canal PCA (0-15)</label>
      <input type="number" id="ma-ch" min="0" max="15" value="0">
      <div class="help">Sortie PWM sur la carte (auto-incr&eacute;ment&eacute;)</div>
    </div>

    <div class="expert-section">
      <button type="button" class="expert-toggle" onclick="toggleExpert(this)">R&eacute;glages avanc&eacute;s</button>
      <div class="expert-body">
        <div class="form-group">
          <label>Latence (ms)</label>
          <input type="number" id="ma-latency" min="0" max="500" value="10">
          <div class="help">D&eacute;lai de compensation m&eacute;canique (d&eacute;faut : 10ms)</div>
        </div>
      </div>
    </div>

    <!-- Servo fields -->
    <div id="servo-fields">
      <div style="border-top:1px solid var(--border);margin:12px 0;padding-top:12px">
        <div style="font-size:13px;font-weight:600;margin-bottom:8px">R&eacute;glages servo</div>
      </div>
      <div class="form-row">
        <div class="form-group">
          <label>Mode de jeu</label>
          <select id="ma-servo-behavior" onchange="toggleServoDirection()"><option value="0">Frappe (aller-retour rapide)</option><option value="1">Altern&eacute; (bascule A/B)</option><option value="2">Gratter (mouvement continu)</option><option value="3">Touche (maintien appuy&eacute;)</option></select>
          <div class="help">Frappe : percussion | Altern&eacute; : bascule entre 2 positions | Gratter : va-et-vient | Touche : maintien</div>
        </div>
        <div class="form-group" id="servo-direction-group">
          <label>Sens de frappe</label>
          <select id="ma-hit-reverse">
            <option value="0">Horaire (+) &mdash; repos &rarr; repos + amplitude</option>
            <option value="1">Anti-horaire (&minus;) &mdash; repos &rarr; repos &minus; amplitude</option>
          </select>
          <div class="help">Direction du mouvement par rapport &agrave; l'angle de repos</div>
        </div>
      </div>
      <div class="form-row">
        <div class="form-group">
          <label>Angle de repos (&deg;)</label>
          <input type="number" id="ma-angle-init" min="0" max="180" value="90" oninput="updateAnglePreview()">
          <div class="help">Position du bras au repos (0&deg; &agrave; 180&deg;)</div>
        </div>
        <div class="form-group">
          <label>Amplitude (&deg;)</label>
          <input type="number" id="ma-amplitude" min="0" max="180" value="45" oninput="updateAnglePreview()">
          <div class="help">Course du mouvement en degr&eacute;s</div>
        </div>
      </div>
      <div class="form-group">
        <label>Dur&eacute;e du mouvement (ms)</label>
        <input type="number" id="ma-speed" min="10" max="2000" value="150">
        <div class="help">Temps pour un aller simple (10=rapide, 500=lent)</div>
      </div>
      <div class="expert-section">
        <button type="button" class="expert-toggle" onclick="toggleExpert(this)">Mode altern&eacute; (avanc&eacute;)</button>
        <div class="expert-body">
          <div class="form-group">
            <label>Angle B - position altern&eacute;e (&deg;)</label>
            <input type="number" id="ma-angle-b" min="0" max="180" value="120" oninput="updateAnglePreview()">
            <div class="help">2e position pour le mode Altern&eacute;</div>
          </div>
        </div>
      </div>
      <!-- Angle visual preview -->
      <div class="angle-preview" id="angle-preview"></div>
    </div>

    <!-- Solenoid fields -->
    <div id="solenoid-fields" style="display:none">
      <div style="border-top:1px solid var(--border);margin:12px 0;padding-top:12px">
        <div style="font-size:13px;font-weight:600;margin-bottom:8px">R&eacute;glages sol&eacute;no&iuml;de</div>
      </div>
      <div class="form-group">
        <label>Mode de frappe</label>
        <select id="ma-sol-behavior" onchange="toggleHitHoldFields()"><option value="0">Frappe (impulsion courte)</option><option value="1">Hit-and-Hold (frappe puis maintien)</option></select>
        <div class="help">Frappe : impulsion br&egrave;ve | Hit-and-Hold : frappe forte puis maintien doux</div>
      </div>
      <div class="form-row">
        <div class="form-group">
          <label>Impulsion min (ms) &mdash; v&eacute;locit&eacute; 1</label>
          <input type="number" id="ma-pulse-min" min="2" max="100" value="5">
          <div class="help">Dur&eacute;e la plus courte (v&eacute;locit&eacute; faible, frappe douce)</div>
        </div>
        <div class="form-group">
          <label>Impulsion max (ms) &mdash; v&eacute;locit&eacute; 127</label>
          <input type="number" id="ma-pulse-max" min="5" max="200" value="30">
          <div class="help">Dur&eacute;e la plus longue (v&eacute;locit&eacute; forte, frappe dure)</div>
        </div>
      </div>
      <div class="form-row">
        <div class="form-group">
          <label>PWM d'attaque (0-4095)</label>
          <input type="number" id="ma-pwm-init" min="0" max="4095" value="4095">
          <div class="help">Puissance initiale de frappe. 4095 = maximum</div>
        </div>
      </div>
      <div id="hit-hold-fields" class="expert-section" style="display:none">
        <button type="button" class="expert-toggle" onclick="toggleExpert(this)">Hit-and-Hold (avanc&eacute;)</button>
        <div class="expert-body">
          <div class="form-row">
            <div class="form-group">
              <label>PWM de maintien (0-4095)</label>
              <input type="number" id="ma-pwm-hold" min="0" max="4095" value="2048">
              <div class="help">Puissance r&eacute;duite apr&egrave;s la frappe</div>
            </div>
            <div class="form-group">
              <label>Rampe de transition (ms)</label>
              <input type="number" id="ma-ramp" min="10" max="500" value="50">
              <div class="help">Dur&eacute;e du passage attaque &rarr; maintien</div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <div class="btn-row">
      <button class="btn primary" onclick="saveActuator()">Sauvegarder</button>
      <button class="btn" onclick="closeModal('modal-actuator')">Annuler</button>
    </div>
  </div>
</div>

<!-- Modal Add CC Mapping -->
<div class="modal-overlay" id="modal-cc">
  <div class="modal" style="max-width:400px">
    <h2>Ajouter un CC Mapping</h2>
    <div class="form-group">
      <label>Num&eacute;ro CC (0-127)</label>
      <input type="number" id="cc-num" min="0" max="127" value="1">
    </div>
    <div class="form-group">
      <label>Actionneur (servo uniquement)</label>
      <select id="cc-actuator" class="form-select"></select>
    </div>
    <div class="form-group">
      <label>Param&egrave;tre cible</label>
      <select id="cc-target" class="form-select">
        <option value="0">Position (angle servo)</option>
        <option value="1">Fr&eacute;quence de mouvement (Hz)</option>
      </select>
    </div>
    <div class="form-row">
      <div class="form-group">
        <label>Valeur min</label>
        <input type="number" id="cc-min" min="0" max="127" value="0">
      </div>
      <div class="form-group">
        <label>Valeur max</label>
        <input type="number" id="cc-max" min="0" max="127" value="127">
      </div>
    </div>
    <div class="btn-row">
      <button class="btn primary" onclick="saveCC()">Ajouter</button>
      <button class="btn" onclick="closeModal('modal-cc')">Annuler</button>
    </div>
  </div>
</div>

<!-- Wizard Instrument -->
<div class="modal-overlay" id="modal-wizard">
  <div class="modal" style="max-width:550px">
    <h2>Assistant de cr&eacute;ation d'instrument</h2>
    <div class="wiz-steps" id="wiz-steps">
      <div class="wiz-step-item active" id="wiz-item-1"><div class="wiz-dot active" id="wiz-dot-1">1</div><div class="wiz-step-label">Identit&eacute;</div></div>
      <div class="wiz-connector" id="wiz-conn-1"></div>
      <div class="wiz-step-item" id="wiz-item-2"><div class="wiz-dot" id="wiz-dot-2">2</div><div class="wiz-step-label">Type</div></div>
      <div class="wiz-connector" id="wiz-conn-2"></div>
      <div class="wiz-step-item" id="wiz-item-3"><div class="wiz-dot" id="wiz-dot-3">3</div><div class="wiz-step-label">Notes</div></div>
      <div class="wiz-connector" id="wiz-conn-3"></div>
      <div class="wiz-step-item" id="wiz-item-4"><div class="wiz-dot" id="wiz-dot-4">4</div><div class="wiz-step-label">Cr&eacute;ation</div></div>
    </div>

    <!-- Step 1: Identity -->
    <div id="wiz-step-1" class="wiz-panel">
      <div style="font-size:14px;font-weight:600;margin-bottom:12px">Identit&eacute; de l'instrument</div>
      <div class="form-group">
        <label>Nom</label>
        <input type="text" id="wiz-name" maxlength="31" placeholder="Ex: Xylophone, Caisse claire...">
      </div>
      <div class="form-group">
        <label>Canal MIDI (0=Omni, 1-16)</label>
        <input type="number" id="wiz-channel" min="0" max="16" value="1">
        <div class="help">0 = tous les canaux, 1-16 = canal sp&eacute;cifique</div>
      </div>
    </div>

    <!-- Step 2: Actuator type -->
    <div id="wiz-step-2" class="wiz-panel" style="display:none">
      <div style="font-size:14px;font-weight:600;margin-bottom:12px">Type d'actionneurs</div>
      <div class="form-group">
        <label>Type</label>
        <select id="wiz-type" onchange="wizUpdateBehaviors()">
          <option value="0">Servo-moteur (mouvement rotatif)</option>
          <option value="1">Sol&eacute;no&iuml;de (frappe lin&eacute;aire)</option>
        </select>
        <div class="help">Tous les actionneurs utiliseront ce type</div>
      </div>
      <div class="form-group">
        <label>Mode de jeu</label>
        <select id="wiz-behavior"></select>
        <div class="help">Comportement par d&eacute;faut pour tous les actionneurs</div>
      </div>
    </div>

    <!-- Step 3: Notes + PCA -->
    <div id="wiz-step-3" class="wiz-panel" style="display:none">
      <div style="font-size:14px;font-weight:600;margin-bottom:12px">Attribution des notes MIDI</div>
      <div class="form-row tri">
        <div class="form-group">
          <label>Actionneurs</label>
          <input type="number" id="wiz-count" min="1" max="64" value="8" oninput="wizBuildNoteTable()">
        </div>
        <div class="form-group">
          <label>Gamme</label>
          <select id="wiz-scale" onchange="wizBuildNoteTable()">
            <option value="chromatic">Chromatique (demi-tons)</option>
            <option value="major">Majeur (do r&eacute; mi fa sol la si)</option>
            <option value="pentatonic">Pentatonique (5 notes)</option>
          </select>
        </div>
        <div class="form-group">
          <label>Note de d&eacute;part</label>
          <input type="number" id="wiz-start-note" min="0" max="127" value="48" oninput="wizBuildNoteTable()">
          <div class="help">C3=48, C4=60</div>
        </div>
      </div>
      <div class="help" style="margin-bottom:4px">Modifiez chaque note individuellement si n&eacute;cessaire :</div>
      <div class="wiz-note-table" id="wiz-note-table"></div>
      <div class="expert-section">
        <button type="button" class="expert-toggle" onclick="toggleExpert(this)">R&eacute;glages PCA avanc&eacute;s</button>
        <div class="expert-body">
          <div class="form-row">
            <div class="form-group">
              <label>Carte PCA de d&eacute;part</label>
              <select id="wiz-pca">
                <option value="64">0x40 (carte 1)</option>
                <option value="65">0x41 (carte 2)</option>
                <option value="66">0x42 (carte 3)</option>
                <option value="67">0x43 (carte 4)</option>
              </select>
            </div>
            <div class="form-group">
              <label>Canal PCA de d&eacute;part</label>
              <input type="number" id="wiz-start-ch" min="0" max="15" value="0">
              <div class="help">Auto-incr&eacute;ment&eacute;</div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Step 4: Review -->
    <div id="wiz-step-4" class="wiz-panel" style="display:none">
      <div style="font-size:14px;font-weight:600;margin-bottom:12px">R&eacute;sum&eacute;</div>
      <div id="wiz-summary" style="background:var(--bg);border:1px solid var(--border);border-radius:var(--radius);padding:12px;font-size:13px;line-height:1.8"></div>
    </div>

    <div class="btn-row" style="margin-top:16px">
      <button class="btn" id="wiz-prev" onclick="wizPrev()" style="display:none">&larr; Pr&eacute;c&eacute;dent</button>
      <button class="btn primary" id="wiz-next" onclick="wizNext()">Suivant &rarr;</button>
      <button class="btn" onclick="closeModal('modal-wizard')">Annuler</button>
    </div>
  </div>
</div>

<!-- ============ CALIBRATION (Actionneurs + CC + Calibration acoustique) ============ -->
<!-- ============ ACTIONNEURS (table + CC mapping) ============ -->
<div class="page" id="page-actuators">

  <!-- === Actionneurs === -->
  <div class="section-title"><span>Actionneurs</span>
    <button class="btn primary sm" onclick="openActuatorModal()">+ Ajouter</button>
  </div>
  <div class="table-responsive">
  <table>
    <thead><tr><th>ID</th><th>Type</th><th>Note MIDI</th><th>Bus</th><th>PCA</th><th>Ch</th><th>Mode</th><th>&Eacute;tat</th><th>Actions</th></tr></thead>
    <tbody id="actuators-table"><tr><td colspan="9" style="color:var(--fg2)">Chargement...</td></tr></tbody>
  </table>
  </div>

  <!-- === CC Mapping === -->
  <div class="section-title" style="margin-top:20px"><span>Control Changes (CC)</span>
    <button class="btn primary sm" onclick="openAddCCModal()">+ Ajouter CC</button>
  </div>
  <div style="margin-bottom:8px">
    <select id="cc-instrument" onchange="loadCCRouting()" class="form-select" style="max-width:250px"></select>
  </div>
  <p style="color:var(--fg2);font-size:12px;margin-bottom:8px">Contr&ocirc;le continu d'un servo : position ou fr&eacute;quence de mouvement.</p>
  <div class="table-responsive">
  <table>
    <thead><tr><th>CC#</th><th>Actionneur</th><th>Cible</th><th>Min</th><th>Max</th><th>Actif</th><th>Actions</th></tr></thead>
    <tbody id="mapping-cc-table"><tr><td colspan="7" style="color:var(--fg2)">S&eacute;lectionner un instrument</td></tr></tbody>
  </table>
  </div>
</div>

<!-- ============ CALIBRATION ACOUSTIQUE (visible uniquement si micro) ============ -->
<div class="page" id="page-calibration">
  <div class="section-title">Calibration Acoustique</div>
  <div class="mic-status" id="mic-status">V&eacute;rification du microphone...</div>
  <div id="cal-controls">
    <div class="cards" style="grid-template-columns:repeat(auto-fit,minmax(140px,1fr))">
      <div class="card">
        <h3>&Eacute;tat</h3>
        <div class="val" id="cal-state" style="font-size:18px">Inactif</div>
      </div>
      <div class="card">
        <h3>Progression</h3>
        <div class="val"><span id="cal-progress">0</span><span class="unit">%</span></div>
        <div class="bar"><div class="bar-fill" id="cal-bar" style="width:0%;background:var(--accent)"></div></div>
      </div>
      <div class="card">
        <h3>Actionneur</h3>
        <div class="val" id="cal-cur-act">&mdash;</div>
      </div>
      <div class="card">
        <h3>R&eacute;sultats</h3>
        <div class="val"><span id="cal-result-count">0</span><span class="unit">mesures</span></div>
      </div>
    </div>
    <div class="btn-row" style="margin:16px 0">
      <button class="btn primary sm" onclick="startCalibrateAll()">&#9654; Calibrer tous</button>
      <button class="btn sm" onclick="startCalibrateOne()" id="cal-btn-one">Calibrer un...</button>
      <button class="btn danger sm" onclick="stopCalibration()" id="cal-btn-stop" style="display:none">Arr&ecirc;ter</button>
      <button class="btn sm" onclick="applyCalibrateResults()" id="cal-btn-apply" style="display:none">&#10003; Appliquer</button>
      <button class="btn sm" onclick="loadCalibrateResults()" style="margin-left:auto">&#8635;</button>
    </div>
    <div id="cal-single-sel" style="display:none;margin-bottom:12px">
      <label style="font-size:13px;margin-right:8px">Actionneur :</label>
      <select id="cal-act-select" class="form-select"></select>
      <button class="btn primary sm" style="margin-left:8px" onclick="confirmCalibrateOne()">Go</button>
      <button class="btn sm" style="margin-left:4px" onclick="document.getElementById('cal-single-sel').style.display='none'">&#10005;</button>
    </div>
    <div class="table-responsive">
    <table>
      <thead><tr><th>ID</th><th>Type</th><th>Latence actuelle</th><th>Latence mesur&eacute;e</th><th>Mesures</th><th>&Eacute;tat</th></tr></thead>
      <tbody id="cal-results-table"><tr><td colspan="6" style="color:var(--fg2);text-align:center">Lancez une calibration</td></tr></tbody>
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
let editingInstrumentIdx = -1;
let editingActuatorId = -1;

const SERVO_BEHAVIORS = ['Frappe','Alterné','Gratter','Touche'];
const SOL_BEHAVIORS = ['Frappe','Hit-and-Hold'];
const CC_TARGETS = ['Position','Fr\u00e9quence (Hz)'];
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
    // AUDIT FIX : reset du compteur de logs à -1 pour forcer un refresh
    // au premier message (gère le cas redémarrage serveur + client déjà connecté)
    logLastCount = -1;
    document.getElementById('ws-dot').className = 'dot';
    document.getElementById('ws-status').textContent = 'Connecté';
  };

  ws.onclose = () => {
    wsConnected = false;
    document.getElementById('ws-dot').className = 'dot off';
    document.getElementById('ws-status').textContent = 'Déconnecté';
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
      // Refresh logs si nouvelles entrées détectées
      if (d.log_count !== undefined && d.log_count !== logLastCount) {
        logLastCount = d.log_count;
        if (currentPage === 'settings') loadLogs();
      }
    } catch(e) {}
  };
}

function updateDashboard(d) {
  // Header
  el('heap', formatBytes(d.heap || 0));
  el('uptime', formatUptime(d.uptime_s || 0));

  // MIDI Transport
  if (d.midi) {
    el('d-midi-recv', (d.midi.serial_bytes || 0) + (d.midi.udp_packets || 0) + (d.midi.rtp_packets || 0));
    el('d-midi-routed', d.dispatcher ? d.dispatcher.dispatched : 0);
    el('d-midi-unmapped', d.dispatcher ? d.dispatcher.dropped : 0);
  }
  if (d.dispatcher) {
    el('d-pwr-rejected', d.dispatcher.pwr_rejected || 0);
  }

  // Scheduler
  if (d.scheduler) {
    el('d-sched-queued', d.scheduler.queued || 0);
    el('d-sched-processed', d.scheduler.processed || 0);
  }

  // Power (polyphony only)
  if (d.power) {
    el('p-poly', d.power.active_count || d.safety?.active || 0);
    const pbar = document.getElementById('p-total-bar');
    if (pbar) {
      const max = d.power.max_polyphony || 12;
      const active = d.power.active_count || d.safety?.active || 0;
      const pct = max > 0 ? Math.min(100, Math.round(active / max * 100)) : 0;
      pbar.style.width = pct + '%';
      pbar.style.background = pct > 80 ? 'var(--red)' : 'var(--green)';
    }
  }

  // Safety
  if (d.safety) {
    el('d-active', d.safety.active || 0);
    el('s-active', d.safety.active || 0);

    const killEl = document.getElementById('s-kill');
    if (killEl) {
      killEl.textContent = d.safety.kill_switch ? 'ON' : 'OFF';
      killEl.style.color = d.safety.kill_switch ? 'var(--red)' : 'var(--green)';
    }
    const degEl = document.getElementById('s-degrad');
    if (degEl) {
      degEl.textContent = d.safety.degradation ? 'Oui' : 'Non';
      degEl.style.color = d.safety.degradation ? 'var(--yellow)' : 'var(--green)';
    }

    // Alerts
    updateAlerts(d);
  }

  // WiFi
  if (d.wifi) {
    el('d-wifi-rssi', d.wifi.rssi ? d.wifi.rssi + ' dBm' : 'N/A');
    el('d-wifi-status', d.wifi.connected ? 'Connecté' : 'Déconnecté');
  }

  // Update piano active notes
  if (d.active_actuators) {
    updatePianoActive(d.active_actuators);
  }
}

function updateAlerts(d) {
  let html = '';
  if (d.safety && d.safety.kill_switch) {
    html += '<div class="alert danger">KILL SWITCH ACTIF — Toutes les sorties sont désactivées</div>';
  }
  if (d.safety && d.safety.degradation) {
    html += '<div class="alert warn">DÉGRADATION — Approche du seuil de sécurité</div>';
  }
  if (d.power && d.power.degradation) {
    html += '<div class="alert warn">BUDGET POWER — Dégradation gracieuse active</div>';
  }
  document.getElementById('alert-zone').innerHTML = html;

  let safetyHtml = '';
  if (d.safety && d.safety.kill_switch) {
    safetyHtml += '<div class="alert danger">KILL SWITCH ACTIF</div>';
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

function formatBytes(b) {
  if (b > 1024) return (b/1024).toFixed(1) + ' KB';
  return b + ' B';
}

function formatUptime(s) {
  const h = Math.floor(s/3600);
  const m = Math.floor((s%3600)/60);
  const sec = s%60;
  return (h>0 ? h+'h ' : '') + m+'m ' + sec+'s';
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
    tbody.innerHTML = '<tr><td colspan="6" style="color:var(--fg2)">Aucun instrument &mdash; utilisez l\'assistant pour commencer</td></tr>';
    return;
  }
  let html = '';
  for (const inst of instruments) {
    const busType = inst.bus_id === 0 ? 'Servos' : 'Sol\u00e9no\u00efdes';
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
    html += '<td>' + (inst.enabled ? '<span class="badge on">Actif</span>' : '<span class="badge off">Inactif</span>') + '</td>';
    html += '<td><button class="btn sm" onclick="editInstrument(' + inst.index + ')">\\u00c9diter</button> ';
    html += '<button class="btn sm" onclick="deleteInstrument(' + inst.index + ')">Suppr</button></td>';
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
  document.getElementById('modal-inst-title').textContent = 'Nouvel instrument';
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
  document.getElementById('modal-inst-title').textContent = 'Modifier ' + inst.name;
  document.getElementById('modal-instrument').classList.add('show');
}

function closeModal(id) {
  document.getElementById(id).classList.remove('show');
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
  if (!confirm('Supprimer cet instrument?')) return;
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
    tbody.innerHTML = '<tr><td colspan="9" style="color:var(--fg2)">Aucun actionneur configur&eacute;</td></tr>';
    return;
  }
  let html = '';
  for (const act of actuators) {
    const isServo = act.type === 0;
    const behaviors = isServo ? SERVO_BEHAVIORS : SOL_BEHAVIORS;
    const nm = noteMap[act.id];
    const noteStr = nm ? noteName(nm.note) + ' (' + nm.note + ')' : '';
    html += '<tr>';
    html += '<td>' + act.id + '</td>';
    html += '<td>' + (isServo ? '<span class="badge servo">Servo</span>' : '<span class="badge sol">Sol&eacute;no&iuml;de</span>') + '</td>';
    html += '<td><input class="note-input" type="number" min="0" max="127" value="' + (nm ? nm.note : '') + '" placeholder="-" '
      + 'onchange="setActuatorNote(' + act.id + ',this.value)">';
    if (nm) html += '<span class="note-label">' + noteName(nm.note) + '</span>';
    html += '</td>';
    html += '<td>Bus ' + act.bus_id + '</td>';
    html += '<td>0x' + act.pca_addr.toString(16).toUpperCase() + '</td>';
    html += '<td>' + act.pca_ch + '</td>';
    html += '<td>' + (behaviors[act.behavior] || '?') + '</td>';
    html += '<td>' + (act.state && act.state.active ? '<span class="badge on">Actif</span>' : '<span class="badge off">Repos</span>') + '</td>';
    html += '<td><button class="btn sm" onclick="editActuator(' + act.id + ')">Éditer</button> ';
    html += '<button class="btn sm" onclick="testActuator(' + act.id + ')">Test</button> ';
    html += '<button class="btn sm" onclick="deleteActuator(' + act.id + ')">Suppr</button></td>';
    html += '</tr>';
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
  if (!el) return;
  const mode = document.getElementById('ma-servo-behavior').value;
  // Show direction only for frappe (0) and touche (3)
  el.style.display = (mode === '0' || mode === '3') ? 'block' : 'none';
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
  document.getElementById('ma-angle-b').value = '120';
  // Reset solenoid fields
  document.getElementById('ma-sol-behavior').value = '0';
  document.getElementById('ma-pulse-min').value = '5';
  document.getElementById('ma-pulse-max').value = '30';
  document.getElementById('ma-pwm-init').value = '4095';
  document.getElementById('ma-pwm-hold').value = '2048';
  document.getElementById('ma-ramp').value = '50';
  toggleActuatorFields();
  document.getElementById('modal-act-title').textContent = 'Nouvel actionneur';
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
  document.getElementById('modal-act-title').textContent = 'Modifier actionneur #' + id;
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
    data.angle_init = parseInt(document.getElementById('ma-angle-init').value);
    data.amplitude = parseInt(document.getElementById('ma-amplitude').value);
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
  if (!confirm('Désactiver cet actionneur?')) return;
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
const MIDI_SOURCE_NAMES = {serial:'C\u00e2ble', udp:'UDP', rtp:'RTP'};

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
    tbody.innerHTML = '<tr><td colspan="6" style="color:var(--fg2)">En attente de messages MIDI...</td></tr>';
    return;
  }
  let html = '';
  for (let i = midiLogEntries.length - 1; i >= 0; i--) {
    const m = midiLogEntries[i];
    const typeName = MIDI_TYPE_NAMES[m.type & 0xF0] || '0x' + m.type.toString(16);
    const ch = (m.type & 0x0F) + 1;
    const src = MIDI_SOURCE_NAMES[m.src] || m.src || '?';
    const routed = m.routed ? '<span class="badge on">Oui</span>' : '<span class="badge off">Non</span>';
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
      sel.innerHTML = '<option value="">Aucun instrument</option>';
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
    ctbody.innerHTML = '<tr><td colspan="7" style="color:var(--fg2)">Aucun mapping CC</td></tr>';
  } else {
    let html = '';
    for (let ci = 0; ci < r.ccs.length; ci++) {
      const cm = r.ccs[ci];
      html += '<tr>';
      html += '<td>CC ' + cm.cc + '</td>';
      html += '<td>Actionneur #' + cm.actuator + '</td>';
      html += '<td>' + (CC_TARGETS[cm.target] || '?') + '</td>';
      html += '<td>' + cm.min + '</td>';
      html += '<td>' + cm.max + '</td>';
      html += '<td>' + (cm.enabled ? '<span class="badge on">Oui</span>' : '<span class="badge off">Non</span>') + '</td>';
      html += '<td><button class="btn sm" onclick="deleteCC(' + instIdx + ',' + cm.cc + ')">Suppr</button></td>';
      html += '</tr>';
    }
    ctbody.innerHTML = html;
  }

  updateCountBadges();
}

function openAddCCModal() {
  // Populate actuator select with servo-only actuators
  const sel = document.getElementById('cc-actuator');
  sel.innerHTML = '';
  if (actuators) {
    for (const a of actuators) {
      if (a.type === 0) { // Only servos
        const opt = document.createElement('option');
        opt.value = a.id;
        opt.textContent = 'Actionneur #' + a.id + ' (Servo)';
        sel.appendChild(opt);
      }
    }
  }
  if (sel.options.length === 0) {
    sel.innerHTML = '<option value="">Aucun servo disponible</option>';
  }
  document.getElementById('cc-num').value = '1';
  document.getElementById('cc-min').value = '0';
  document.getElementById('cc-max').value = '127';
  document.getElementById('modal-cc').classList.add('show');
}

async function saveCC() {
  const instSel = document.getElementById('cc-instrument');
  const instIdx = parseInt(instSel ? instSel.value : '0');
  const ccNum = parseInt(document.getElementById('cc-num').value);
  const actId = parseInt(document.getElementById('cc-actuator').value);
  const target = parseInt(document.getElementById('cc-target').value);
  const min = parseInt(document.getElementById('cc-min').value);
  const max = parseInt(document.getElementById('cc-max').value);
  if (isNaN(ccNum) || isNaN(actId)) { toast('Valeurs invalides', 'error'); return; }

  // Get existing CCs and add new one
  const r = routing ? routing.find(x => x.instrument === instIdx) : null;
  let ccs = r && r.ccs ? [...r.ccs] : [];
  // Replace if same CC# exists
  ccs = ccs.filter(c => c.cc !== ccNum);
  ccs.push({cc: ccNum, actuator: actId, target, min, max, enabled: true});
  await api('/api/routing/cc', 'POST', {instrument: instIdx, ccs});
  closeModal('modal-cc');
  toast('CC ' + ccNum + ' ajout\u00e9', 'ok');
  loadCCRouting();
}

async function deleteCC(instIdx, ccNum) {
  if (!confirm('Supprimer le CC ' + ccNum + ' ?')) return;
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

  if (!instruments || instruments.length === 0) {
    container.innerHTML = '<p style="color:var(--fg2);font-size:13px;margin-top:16px">Aucun instrument. Utilisez l\'assistant pour cr\u00e9er votre premier instrument.</p>';
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

    // Compute note range
    const minNote = Math.min(...mappedNotes);
    const maxNote = Math.max(...mappedNotes);
    let startNote = Math.max(0, (Math.floor((minNote - 2) / 12)) * 12);
    let endNote = Math.min(128, (Math.ceil((maxNote + 3) / 12)) * 12);
    if (endNote - startNote < 24) {
      const mid = Math.floor((startNote + endNote) / 2);
      startNote = Math.max(0, mid - 12);
      endNote = Math.min(128, mid + 12);
    }

    // White keys
    let wCount = 0;
    for (let n = startNote; n < endNote; n++) {
      const nio = n % 12;
      if (![1,3,6,8,10].includes(nio)) {
        const k = document.createElement('div');
        k.className = 'white' + (!mappedNotes.has(n) ? ' muted' : '');
        k.dataset.note = n;
        k.dataset.inst = idx;
        k.textContent = noteName(n);
        k.onmousedown = () => pianoNoteOn(idx, n);
        k.onmouseup = () => pianoNoteOff(idx, n);
        k.onmouseleave = () => pianoNoteOff(idx, n);
        k.addEventListener('touchstart', (e) => { e.preventDefault(); pianoNoteOn(idx, n); }, {passive:false});
        k.addEventListener('touchend', (e) => { e.preventDefault(); pianoNoteOff(idx, n); }, {passive:false});
        piano.appendChild(k);
        wCount++;
      }
    }
    // Black keys
    let wIdx = 0;
    for (let n = startNote; n < endNote; n++) {
      const nio = n % 12;
      if ([1,3,6,8,10].includes(nio)) {
        const k = document.createElement('div');
        k.className = 'black' + (!mappedNotes.has(n) ? ' muted' : '');
        k.dataset.note = n;
        k.dataset.inst = idx;
        k.style.left = (wIdx * 40 - 13) + 'px';
        k.onmousedown = (e) => { e.preventDefault(); pianoNoteOn(idx, n); };
        k.onmouseup = () => pianoNoteOff(idx, n);
        k.onmouseleave = () => pianoNoteOff(idx, n);
        k.addEventListener('touchstart', (e) => { e.preventDefault(); pianoNoteOn(idx, n); }, {passive:false});
        k.addEventListener('touchend', (e) => { e.preventDefault(); pianoNoteOff(idx, n); }, {passive:false});
        piano.appendChild(k);
      } else { wIdx++; }
    }

    pianoWrap.appendChild(piano);
    section.appendChild(pianoWrap);
    container.appendChild(section);
  }
}

function pianoNoteOn(instIdx, note) {
  const instMap = pianoNotes[instIdx];
  if (!instMap) return;
  const actId = instMap[note];
  if (actId === undefined) return;
  // Visual feedback only on this instrument's piano
  const piano = document.getElementById('piano-' + instIdx);
  if (piano) piano.querySelectorAll('[data-note="' + note + '"]').forEach(k => k.classList.add('active'));
  if (ws && wsConnected) {
    ws.send(JSON.stringify({cmd:'test', id:actId, vel:100}));
  }
}

function pianoNoteOff(instIdx, note) {
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

  // Reset all active states
  document.querySelectorAll('.piano .active').forEach(k => k.classList.remove('active'));

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
    el('p-poly', d.stats.active_count);
    // Update bar
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
  toast('Polyphonie mise \u00e0 jour', 'ok');
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
    document.getElementById('sf-poly').value = d.config.max_polyphony;
  }
}

async function saveSafetyConfig() {
  await api('/api/safety', 'POST', {
    max_duty_pct: parseInt(document.getElementById('sf-duty').value),
    max_freq_hz: parseInt(document.getElementById('sf-freq').value),
    watchdog_ms: parseInt(document.getElementById('sf-watchdog').value),
    max_polyphony: parseInt(document.getElementById('sf-poly').value)
  });
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
  document.getElementById('set-hostname').value = d.hostname || 'midi-boop';
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
  alert('WiFi sauvegardé. Redémarrer pour appliquer.');
}

async function loadBuses() {
  const buses = await api('/api/buses');
  const tbody = document.getElementById('buses-table');
  if (!buses || buses.length === 0) {
    tbody.innerHTML = '<tr><td colspan="8" style="color:var(--fg2)">Aucun bus d\u00e9tect\u00e9</td></tr>';
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
    html += '<option value="1000"' + pwmSel1k + '>1000 Hz (Sol\u00e9no\u00efde)</option>';
    html += '</select></td>';
    html += '<td>' + b.pca_count + ' PCA</td>';
    html += '<td>' + (b.enabled ? '<span class="badge on">Actif</span>' : '<span class="badge off">Inactif</span>') + '</td>';
    html += '</tr>';
  }
  tbody.innerHTML = html;
}

async function setBusPwmFreq(busId, freq) {
  await api('/api/bus/pwm', 'POST', {bus_id: busId, freq_pwm: parseInt(freq)});
  toast('Fr\u00e9quence PWM bus ' + busId + ' mise \u00e0 ' + freq + ' Hz', 'ok');
}

async function scanI2C() {
  const result = await api('/api/scan/i2c', 'POST');
  if (result) {
    let msg = 'Résultat scan I²C:\n';
    for (const bus of result) {
      msg += 'Bus ' + bus.bus + ': ' + bus.pca_count + ' PCA trouvés';
      if (bus.addresses && bus.addresses.length > 0) {
        msg += ' (' + bus.addresses.join(', ') + ')';
      }
      msg += '\n';
    }
    alert(msg);
    loadBuses();
  }
}

async function saveConfig() {
  const result = await api('/api/config/save', 'POST');
  if (result && result.ok) {
    alert('Configuration sauvegardée sur flash');
  } else {
    alert('Erreur de sauvegarde');
  }
}

async function resetDefaults() {
  await api('/api/config/defaults', 'POST');
  alert('Configuration réinitialisée. Rechargement...');
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
// Calibration Acoustique (Phase 7)
// ============================================================================
let calPollInterval = null;

async function loadCalibrateStatus() {
  const d = await api('/api/calibrate/status');
  if (!d) return;

  const stateNames = {
    idle:'Inactif', ambient:'Mesure ambiant…', triggering:'Déclenchement…',
    recording:'Enregistrement…', pausing:'Pause…',
    complete:'Terminé ✓', error:'Erreur ✗'
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
    el('cal-cur-act', d.state === 'complete' ? 'Terminé' : '—');
  }

  // Arrêt du polling quand terminé
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
    if (tbody) tbody.innerHTML = '<tr><td colspan="6" style="color:var(--fg2);text-align:center">Aucun résultat</td></tr>';
    return;
  }

  const typeNames = ['Servo', 'Solénoïde'];
  let html = '';
  for (const r of data) {
    const hasMeasure = r.measured_ms !== null && r.measured_ms !== undefined;
    const badge = hasMeasure
      ? (r.success ? '<span class="badge on">OK</span>' : '<span class="badge off">Échec</span>')
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

function startCalibrateAll() {
  if (!confirm('Démarrer la calibration de tous les actionneurs ?\nAssurez-vous que le microphone est positionné et que l\'environnement est silencieux.')) return;
  api('/api/calibrate', 'POST', { all: true }).then(r => {
    if (r && r.ok) {
      toast('Calibration démarrée', 'ok');
      startCalPoll();
    } else {
      toast('Erreur : ' + (r && r.error ? r.error : 'inconnue'), 'error');
    }
  });
}

function startCalibrateOne() {
  const sel = document.getElementById('cal-single-sel');
  const select = document.getElementById('cal-act-select');
  if (!sel || !select) return;
  // Peupler le select avec les actionneurs connus
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
      toast('Calibration démarrée pour act ' + id, 'ok');
      startCalPoll();
    } else {
      toast('Erreur : ' + (r && r.error ? r.error : 'inconnue'), 'error');
    }
  });
}

function stopCalibration() {
  api('/api/calibrate/stop', 'POST', {}).then(() => {
    toast('Calibration arrêtée', 'warn');
    loadCalibrateStatus();
  });
}

async function applyCalibrateResults() {
  const r = await api('/api/calibrate/apply', 'POST', {});
  if (r && r.ok) {
    toast(r.applied + ' latences appliquées aux actionneurs', 'ok');
    loadActuators();
    loadCalibrateResults();
  } else {
    toast('Erreur lors de l\'application', 'error');
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
  // AUDIT FIX : ajout des guillemets pour une protection XSS complète (attributs HTML)
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
      if (info) info.textContent = logCache.length + ' entrée(s) — ' + r.count + ' au total';
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
    tbody.innerHTML = '<tr><td colspan="4" style="color:var(--fg2)">Aucune entrée</td></tr>';
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

  // AUDIT FIX : sélecteur robuste via classe dédiée .log-container
  if (document.getElementById('log-autoscroll')?.checked) {
    const wrap = tbody.closest('.log-container');
    if (wrap) wrap.scrollTop = wrap.scrollHeight;
  }
}

function clearLogs() {
  if (!confirm('Effacer tout le journal système ?')) return;
  api('/api/logs/clear', 'POST', {}).then(r => {
    if (r && r.ok) {
      logCache = [];
      renderLogs();
      toast('Journal effacé', 'ok');
    } else {
      toast('Erreur', 'error');
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
    ? [{v:0,t:'Frappe (aller-retour rapide)'},{v:1,t:'Alterné (bascule A/B)'},{v:2,t:'Gratter (mouvement continu)'},{v:3,t:'Touche (maintien appuyé)'}]
    : [{v:0,t:'Frappe (impulsion courte)'},{v:1,t:'Hit-and-Hold (frappe puis maintien)'}];
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

  let html = '<table><thead><tr><th style="width:60px">#</th><th style="width:80px">Note</th><th>Nom</th></tr></thead><tbody>';
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
    nextBtn.textContent = 'Créer';
    nextBtn.className = 'btn primary';
    wizBuildSummary();
  } else {
    nextBtn.textContent = 'Suivant →';
    nextBtn.className = 'btn primary';
  }
}

function wizBuildSummary() {
  const name = document.getElementById('wiz-name').value || 'Instrument';
  const ch = document.getElementById('wiz-channel').value;
  const type = document.getElementById('wiz-type').value;
  const typeName = type === '0' ? 'Servo-moteur' : 'Solénoïde';
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

  let html = '<strong>' + esc(name) + '</strong> — Canal MIDI ' + (ch === '0' ? 'Omni (tous)' : ch) + '<br>';
  html += 'Type : <strong>' + typeName + '</strong> — ' + behavior + '<br>';
  html += count + ' actionneurs<br>';
  html += 'Notes : ' + noteStr + '<br>';
  html += 'PCA : ' + pca + ' — Canaux ' + startCh + ' → ' + (parseInt(startCh) + count - 1) + '<br>';
  html += 'Bus : <strong>' + (type === '0' ? 'Bus 0 (Servos)' : 'Bus 1 (Solénoïdes)') + '</strong>';
  document.getElementById('wiz-summary').innerHTML = html;
}

function wizPrev() {
  if (wizStep > 1) { wizStep--; wizShowStep(); }
}

async function wizNext() {
  // Per-step validation before advancing
  if (wizStep === 1) {
    const name = document.getElementById('wiz-name').value.trim();
    if (!name) { toast('Veuillez entrer un nom pour l\'instrument', 'error'); return; }
  }
  if (wizStep === 3) {
    const count = parseInt(document.getElementById('wiz-count').value);
    if (isNaN(count) || count < 1 || count > 32) { toast('Nombre d\'actionneurs invalide (1–32)', 'error'); return; }
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

  // 2. Create actuators — baseId = max(existing IDs) + 1 pour garantir l'unicité
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
  toast(name + ' créé avec ' + count + ' actionneurs', 'ok');

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
  const init = parseInt(document.getElementById('ma-angle-init').value) || 90;
  const amp = parseInt(document.getElementById('ma-amplitude').value) || 45;
  const angleB = parseInt(document.getElementById('ma-angle-b').value) || 120;
  const minA = Math.max(0, init - amp);
  const maxA = Math.min(180, init + amp);
  const cx=60,cy=55,r=40;
  const toX=(deg)=>cx+r*Math.cos((180-deg)*Math.PI/180);
  const toY=(deg)=>cy-r*Math.sin((180-deg)*Math.PI/180);
  const x1=toX(minA),y1=toY(minA),x2=toX(maxA),y2=toY(maxA);
  const xi=toX(init),yi=toY(init),xb=toX(angleB),yb=toY(angleB);
  const la=(maxA-minA)>180?1:0;
  let s='<svg width="120" height="70" viewBox="0 0 120 70">';
  s+='<path d="M '+(cx-r)+' '+cy+' A '+r+' '+r+' 0 0 1 '+(cx+r)+' '+cy+'" fill="none" stroke="var(--bg3)" stroke-width="3"/>';
  s+='<path d="M '+x1+' '+y1+' A '+r+' '+r+' 0 '+la+' 1 '+x2+' '+y2+'" fill="none" stroke="var(--accent)" stroke-width="3"/>';
  s+='<line x1="'+cx+'" y1="'+cy+'" x2="'+xi+'" y2="'+yi+'" stroke="var(--green)" stroke-width="2"/>';
  s+='<line x1="'+cx+'" y1="'+cy+'" x2="'+xb+'" y2="'+yb+'" stroke="var(--yellow)" stroke-width="1.5" stroke-dasharray="4,3"/>';
  s+='<circle cx="'+cx+'" cy="'+cy+'" r="3" fill="var(--fg)"/>';
  s+='<text x="2" y="68" font-size="9" fill="var(--fg2)">180</text>';
  s+='<text x="104" y="68" font-size="9" fill="var(--fg2)">0</text>';
  s+='</svg>';
  s+='<div class="angle-info">';
  s+='Repos: <span>'+init+'&deg;</span>';
  s+=' Course: <span>'+minA+'&deg;&rarr;'+maxA+'&deg;</span>';
  s+=' Alt(B): <span>'+angleB+'&deg;</span>';
  s+='</div>';
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
        micDiv.innerHTML = '&#9679; Micro d\u00e9tect\u00e9 \u2014 Pr\u00eat pour la calibration';
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
