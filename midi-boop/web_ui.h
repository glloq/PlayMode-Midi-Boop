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
  font-size:9px;color:#666;transition:background .1s}
.piano .white:hover{background:#e0e8f0}
.piano .white.active{background:var(--accent);color:#fff}
.piano .white.mapped{background:#d0e8ff}
.piano .black{width:26px;height:85px;background:#222;border:1px solid #000;
  border-radius:0 0 3px 3px;cursor:pointer;position:absolute;z-index:2;
  margin-left:-13px;transition:background .1s}
.piano .black:hover{background:#444}
.piano .black.active{background:var(--accent)}
.piano .black.mapped{background:#2a5a8f}

/* Section titles — flexbox pour alignement mobile */
.section-title{font-size:16px;font-weight:600;margin-bottom:16px;
  padding-bottom:8px;border-bottom:1px solid var(--border);
  display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px}

/* Log container */
.log-container{overflow-y:auto;max-height:calc(100vh - 280px)}

/* Responsive — mobile (<600px) */
@media(max-width:600px){
  .page{padding:12px}
  .cards{grid-template-columns:1fr 1fr}
  .form-row{grid-template-columns:1fr}
  .form-row.tri{grid-template-columns:1fr}
  nav button{padding:8px 12px;font-size:12px}
  /* Prévention auto-zoom iOS (font-size < 16px déclenche le zoom) */
  .form-group input,.form-group select{font-size:16px}
  .header .status{font-size:11px}
  .log-container{max-height:calc(100vh - 150px)}
  .modal{padding:16px}
}
/* Très petits écrans (<380px) */
@media(max-width:380px){
  .cards{grid-template-columns:1fr}
  .header .status span:not(#ws-status){display:none}
}
/* Tablette (601px–768px) */
@media(min-width:601px) and (max-width:768px){
  .cards{grid-template-columns:repeat(auto-fit,minmax(160px,1fr))}
  .form-row.tri{grid-template-columns:1fr 1fr}
}
/* Mode paysage sur mobile */
@media(max-width:768px) and (orientation:landscape){
  .log-container{max-height:calc(100vh - 120px)}
  .modal{max-height:95vh}
}
/* Gear settings dropdown */
.gear-btn{background:none;border:none;color:var(--fg2);font-size:20px;cursor:pointer;
  padding:6px 10px;border-radius:6px;transition:all .15s;min-width:44px;min-height:44px;
  display:flex;align-items:center;justify-content:center}
.gear-btn:hover{color:var(--fg);background:var(--bg3)}
.settings-dropdown{display:none;position:absolute;top:100%;right:0;background:var(--bg2);
  border:1px solid var(--border);border-radius:var(--radius);min-width:200px;
  box-shadow:0 8px 24px rgba(0,0,0,.4);z-index:50;overflow:hidden;margin-top:4px}
.settings-dropdown.show{display:block}
.settings-dropdown button{display:block;width:100%;text-align:left;background:none;border:none;
  color:var(--fg);padding:10px 16px;cursor:pointer;font-size:13px;transition:background .1s;
  border-bottom:1px solid var(--border)}
.settings-dropdown button:last-child{border-bottom:none}
.settings-dropdown button:hover{background:var(--bg3)}
.settings-dropdown .sdesc{font-size:11px;color:var(--fg2);display:block;margin-top:1px}

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
  <div style="position:relative">
    <button class="gear-btn" onclick="toggleSettingsMenu()" title="R&eacute;glages syst&egrave;me">&#9881;</button>
    <div class="settings-dropdown" id="settings-dropdown">
      <button onclick="showPage('dashboard');closeSettingsMenu()">Dashboard<span class="sdesc">Monitoring temps r&eacute;el</span></button>
      <button onclick="showPage('power');closeSettingsMenu()">Power<span class="sdesc">Budget et consommation</span></button>
      <button onclick="showPage('safety');closeSettingsMenu()">Safety<span class="sdesc">Limites de s&eacute;curit&eacute;</span></button>
      <button onclick="showPage('logs');closeSettingsMenu()">Logs<span class="sdesc">Journal syst&egrave;me</span></button>
      <button onclick="showPage('settings');closeSettingsMenu()">Param&egrave;tres<span class="sdesc">WiFi, MIDI, bus I&sup2;C, config</span></button>
    </div>
  </div>
</div>

<!-- Navigation -->
<nav>
  <button class="active" onclick="showPage('home')">Accueil</button>
  <button onclick="showPage('actuators')">Actionneurs</button>
  <button onclick="showPage('cc')">CC Mapping</button>
  <button onclick="showPage('calibration')">Calibration</button>
  <button onclick="showPage('test')">Test</button>
</nav>

<!-- ============ HOME (Instruments + Piano) ============ -->
<div class="page active" id="page-home">
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

  <div class="section-title" style="margin-top:12px"><span>Piano virtuel</span>
    <select id="home-piano-instrument" onchange="updatePianoMapping()" class="form-select"></select>
  </div>
  <p style="color:var(--fg2);font-size:12px;margin-bottom:8px">Cliquer sur une touche pour tester. Les touches bleues sont mapp&eacute;es.</p>
  <div class="piano-container">
    <div class="piano" id="piano-keys"></div>
  </div>
  <div style="margin-top:8px">
    <div id="piano-active" style="color:var(--fg2);font-size:13px">Notes actives : aucune</div>
  </div>
</div>

<!-- ============ DASHBOARD ============ -->
<div class="page" id="page-dashboard">
  <div class="cards">
    <div class="card">
      <h3>MIDI Reçus</h3>
      <div class="val" id="d-midi-recv">0</div>
      <div class="sub">Routés: <span id="d-midi-routed">0</span> | Non mappés: <span id="d-midi-unmapped">0</span></div>
    </div>
    <div class="card">
      <h3>Scheduler</h3>
      <div class="val" id="d-sched-queued">0</div>
      <div class="sub">en file | <span id="d-sched-processed">0</span> traités</div>
    </div>
    <div class="card">
      <h3>Consommation</h3>
      <div class="val"><span id="d-power-ma">0</span><span class="unit">mA</span></div>
      <div class="bar"><div class="bar-fill" id="d-power-bar" style="width:0%;background:var(--green)"></div></div>
      <div class="sub"><span id="d-power-pct">0</span>% du budget</div>
    </div>
    <div class="card">
      <h3>Actifs</h3>
      <div class="val" id="d-active">0</div>
      <div class="sub">actionneurs actifs</div>
    </div>
    <div class="card">
      <h3>Bus Servos</h3>
      <div class="val"><span id="d-servo-ma">0</span><span class="unit">mA</span></div>
    </div>
    <div class="card">
      <h3>Bus Solénoïdes</h3>
      <div class="val"><span id="d-sol-ma">0</span><span class="unit">mA</span></div>
    </div>
    <div class="card">
      <h3>WiFi</h3>
      <div class="val" id="d-wifi-rssi">-</div>
      <div class="sub" id="d-wifi-status">-</div>
    </div>
    <div class="card">
      <h3>Power Rejetés</h3>
      <div class="val" id="d-pwr-rejected">0</div>
      <div class="sub">événements rejetés (budget)</div>
    </div>
  </div>

  <div class="section-title">Actionneurs actifs</div>
  <div class="table-responsive">
  <table>
    <thead><tr><th>ID</th><th>Type</th><th>État</th><th>Position</th></tr></thead>
    <tbody id="d-active-table"><tr><td colspan="4" style="color:var(--fg2)">Aucun actionneur actif</td></tr></tbody>
  </table>
  </div>

  <div class="section-title" style="margin-top:20px"><span>Derniers événements</span>
    <a onclick="showPage('logs')" style="font-size:12px;font-weight:400;color:var(--accent);cursor:pointer">Voir tous →</a>
  </div>
  <div class="table-responsive">
  <table>
    <thead><tr><th style="width:80px">Temps</th><th style="width:60px">Niveau</th><th>Message</th></tr></thead>
    <tbody id="d-log-table"><tr><td colspan="3" style="color:var(--fg2)">Aucun événement récent</td></tr></tbody>
  </table>
  </div>
</div>

<!-- ============ ACTUATORS ============ -->
<div class="page" id="page-actuators">
  <div class="section-title"><span>Actionneurs</span>
    <button class="btn primary sm" onclick="openActuatorModal()">+ Ajouter</button>
  </div>
  <div class="table-responsive">
  <table>
    <thead><tr><th>ID</th><th>Type</th><th>Note MIDI</th><th>Bus</th><th>PCA</th><th>Ch</th><th>Mode</th><th>&Eacute;tat</th><th>Actions</th></tr></thead>
    <tbody id="actuators-table"><tr><td colspan="9" style="color:var(--fg2)">Chargement...</td></tr></tbody>
  </table>
  </div>
</div>

<!-- ============ CC MAPPING ============ -->
<div class="page" id="page-cc">
  <div class="section-title"><span>Control Changes (CC)</span>
    <select id="cc-instrument" onchange="loadCCRouting()" class="form-select"></select>
  </div>
  <p style="color:var(--fg2);font-size:12px;margin-bottom:12px">Les CC permettent de contr&ocirc;ler en continu un param&egrave;tre d'un actionneur (position, amplitude, vitesse...).</p>
  <div class="table-responsive">
  <table>
    <thead><tr><th>CC#</th><th>Actionneur</th><th>Cible</th><th>Min</th><th>Max</th><th>Actif</th></tr></thead>
    <tbody id="mapping-cc-table"><tr><td colspan="6" style="color:var(--fg2)">S&eacute;lectionner un instrument</td></tr></tbody>
  </table>
  </div>
</div>

<!-- ============ POWER ============ -->
<div class="page" id="page-power">
  <div class="section-title">Power Manager</div>
  <div class="cards">
    <div class="card">
      <h3>Budget Global</h3>
      <div class="val"><span id="p-total">0</span><span class="unit">mA</span></div>
      <div class="bar"><div class="bar-fill" id="p-total-bar" style="width:0%;background:var(--green)"></div></div>
      <div class="sub">Max: <span id="p-total-max">6000</span> mA</div>
    </div>
    <div class="card">
      <h3>Bus Servos</h3>
      <div class="val"><span id="p-servo">0</span><span class="unit">mA</span></div>
      <div class="sub">Max: <span id="p-servo-max">3000</span> mA</div>
    </div>
    <div class="card">
      <h3>Bus Solénoïdes</h3>
      <div class="val"><span id="p-sol">0</span><span class="unit">mA</span></div>
      <div class="sub">Max: <span id="p-sol-max">4000</span> mA</div>
    </div>
    <div class="card">
      <h3>Polyphonie</h3>
      <div class="val"><span id="p-poly">0</span><span class="unit">/ <span id="p-poly-max">12</span></span></div>
    </div>
  </div>

  <div class="section-title" style="font-size:14px">Modifier le budget</div>
  <div class="form-row tri">
    <div class="form-group">
      <label>Budget global max (mA)</label>
      <input type="number" id="pw-global" value="6000">
      <div class="help">Courant max total pour tous les actionneurs</div>
    </div>
    <div class="form-group">
      <label>Bus servos max (mA)</label>
      <input type="number" id="pw-servo" value="3000">
      <div class="help">Limite courant pour le bus servo-moteurs</div>
    </div>
    <div class="form-group">
      <label>Bus sol&eacute;no&iuml;des max (mA)</label>
      <input type="number" id="pw-sol" value="4000">
      <div class="help">Limite courant pour le bus sol&eacute;no&iuml;des</div>
    </div>
  </div>
  <div class="form-row">
    <div class="form-group">
      <label>Polyphonie max</label>
      <input type="number" id="pw-poly" value="12" min="1" max="32">
      <div class="help">Nombre max d'actionneurs actifs en m&ecirc;me temps</div>
    </div>
  </div>
  <button class="btn primary" onclick="savePowerBudget()">Appliquer</button>

  <div style="margin-top:20px">
    <div class="sub">Rejetés: <span id="p-rejected">0</span> | Dégradation: <span id="p-degrad">Non</span></div>
  </div>
</div>

<!-- ============ SAFETY ============ -->
<div class="page" id="page-safety">
  <div class="section-title">Safety Manager</div>

  <div id="safety-alert-zone"></div>

  <div class="cards">
    <div class="card">
      <h3>Courant estimé</h3>
      <div class="val"><span id="s-current">0</span><span class="unit">mA</span></div>
    </div>
    <div class="card">
      <h3>Actifs</h3>
      <div class="val" id="s-active">0</div>
    </div>
    <div class="card">
      <h3>Kill Switch</h3>
      <div class="val" id="s-kill" style="color:var(--green)">OFF</div>
    </div>
    <div class="card">
      <h3>Dégradation</h3>
      <div class="val" id="s-degrad" style="color:var(--green)">Non</div>
    </div>
  </div>

  <div class="btn-row">
    <button class="btn danger" onclick="toggleKillSwitch(true)">KILL SWITCH ON</button>
    <button class="btn" onclick="toggleKillSwitch(false)">Kill Switch Off</button>
  </div>

  <div class="section-title" style="font-size:14px">Limites de s&eacute;curit&eacute;</div>
  <div class="form-row tri">
    <div class="form-group">
      <label>Duty cycle max (%)</label>
      <input type="number" id="sf-duty" value="80" min="10" max="100">
      <div class="help">Ratio max activation/repos pour chaque actionneur</div>
    </div>
    <div class="form-group">
      <label>Fr&eacute;quence max (Hz)</label>
      <input type="number" id="sf-freq" value="50" min="1" max="200">
      <div class="help">Frappes max par seconde par actionneur</div>
    </div>
    <div class="form-group">
      <label>Watchdog timeout (ms)</label>
      <input type="number" id="sf-watchdog" value="5000" min="1000" max="30000">
      <div class="help">Coupe les actionneurs si aucun signal pendant ce d&eacute;lai</div>
    </div>
  </div>
  <div class="form-row">
    <div class="form-group">
      <label>Polyphonie max</label>
      <input type="number" id="sf-poly" value="12" min="1" max="32">
      <div class="help">Limite absolue d'actionneurs simultan&eacute;s (s&eacute;curit&eacute;)</div>
    </div>
    <div class="form-group">
      <label>Courant max (mA)</label>
      <input type="number" id="sf-current" value="5000" min="500" max="20000">
      <div class="help">Seuil de surintensit&eacute; d&eacute;clenchant le kill switch</div>
    </div>
  </div>
  <button class="btn primary" onclick="saveSafetyConfig()">Appliquer</button>
</div>

<!-- ============ SETTINGS ============ -->
<div class="page" id="page-settings">
  <div class="section-title">WiFi</div>
  <div class="form-row">
    <div class="form-group">
      <label>SSID du r&eacute;seau</label>
      <input type="text" id="set-ssid" maxlength="32" placeholder="Nom du WiFi">
      <div class="help">R&eacute;seau WiFi auquel l'ESP32 se connecte</div>
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
      <div class="help">Nom r&eacute;seau de l'appareil (accessible via hostname.local)</div>
    </div>
    <div class="form-group">
      <label>AP Fallback</label>
      <select id="set-ap-fallback">
        <option value="1">Oui</option>
        <option value="0">Non</option>
      </select>
      <div class="help">Cr&eacute;e un point d'acc&egrave;s si le WiFi &eacute;choue</div>
    </div>
  </div>
  <button class="btn primary" onclick="saveWiFiConfig()">Sauvegarder WiFi</button>

  <div class="expert-section" style="margin-top:24px">
    <button type="button" class="expert-toggle" onclick="toggleExpert(this)">Transport MIDI &amp; Bus I&sup2;C (avanc&eacute;)</button>
    <div class="expert-body">
  <div class="section-title">Transport MIDI</div>
  <div class="cards" style="margin-bottom:16px">
    <div class="card">
      <h3>Serial MIDI</h3>
      <label><input type="checkbox" id="midi-serial" onchange="updateMidiConfig()"> Actif</label>
      <div class="sub">GPIO <span id="midi-rx-pin">4</span> @ 31250 baud</div>
    </div>
    <div class="card">
      <h3>UDP MIDI</h3>
      <label><input type="checkbox" id="midi-udp" onchange="updateMidiConfig()"> Actif</label>
      <div class="sub">Port: <span id="midi-udp-port">5004</span></div>
    </div>
    <div class="card">
      <h3>RTP-MIDI</h3>
      <label><input type="checkbox" id="midi-rtp" onchange="updateMidiConfig()"> Actif</label>
      <div class="sub">Port: <span id="midi-rtp-port">5004</span></div>
    </div>
    <div class="card">
      <h3>Jitter Buffer</h3>
      <div class="val"><span id="midi-jitter-val">30</span><span class="unit">ms</span></div>
      <input type="range" id="midi-jitter" min="10" max="80" value="30" style="width:100%;margin-top:8px"
        oninput="document.getElementById('midi-jitter-val').textContent=this.value"
        onchange="updateMidiConfig()">
      <div class="help">Tampon anti-gigue pour le r&eacute;seau</div>
    </div>
  </div>

  <div class="section-title" style="margin-top:24px">Bus I&sup2;C</div>
  <div class="table-responsive">
  <table>
    <thead><tr><th>Bus</th><th>SDA</th><th>SCL</th><th>OE</th><th>Freq I²C</th><th>Freq PWM</th><th>PCA détectés</th><th>Actions</th></tr></thead>
    <tbody id="buses-table"><tr><td colspan="8" style="color:var(--fg2)">Chargement...</td></tr></tbody>
  </table>
  </div>
  <button class="btn" onclick="scanI2C()">Scanner bus I²C</button>
    </div>
  </div>

  <div class="section-title" style="margin-top:24px">Configuration</div>
  <div class="btn-row">
    <button class="btn primary" onclick="saveConfig()">Sauvegarder sur flash</button>
    <button class="btn danger" onclick="if(confirm('Remettre les défauts?'))resetDefaults()">Réinitialiser</button>
  </div>
  <div class="sub" style="margin-top:8px">Version config: <span id="set-version">-</span></div>
</div>

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
        <label>Canal MIDI (0-15)</label>
        <input type="number" id="mi-channel" min="0" max="15" value="0">
        <div class="help">Canal sur lequel l'instrument recevra les messages MIDI</div>
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
        <select id="ma-bus"><option value="0">Bus 0 (Servos)</option><option value="1">Bus 1 (Sol&eacute;no&iuml;des)</option></select>
        <div class="help">Bus physique : 0 pour servos, 1 pour sol&eacute;no&iuml;des</div>
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
      <div class="form-group">
        <label>Mode de jeu</label>
        <select id="ma-servo-behavior"><option value="0">Frappe (aller-retour rapide)</option><option value="1">Altern&eacute; (bascule A/B)</option><option value="2">Gratter (mouvement continu)</option><option value="3">Touche (maintien appuy&eacute;)</option></select>
        <div class="help">Frappe : percussion | Altern&eacute; : bascule entre 2 positions | Gratter : va-et-vient | Touche : maintien</div>
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
        <select id="ma-sol-behavior"><option value="0">Frappe (impulsion courte)</option><option value="1">Hit-and-Hold (frappe puis maintien)</option></select>
        <div class="help">Frappe : impulsion br&egrave;ve | Hit-and-Hold : frappe forte puis maintien doux</div>
      </div>
      <div class="form-row">
        <div class="form-group">
          <label>Dur&eacute;e d'impulsion (ms)</label>
          <input type="number" id="ma-pulse" min="5" max="50" value="20">
          <div class="help">Dur&eacute;e de l'activation (5-50ms). Plus court = frappe s&egrave;che</div>
        </div>
        <div class="form-group">
          <label>PWM d'attaque (0-4095)</label>
          <input type="number" id="ma-pwm-init" min="0" max="4095" value="4095">
          <div class="help">Puissance initiale de frappe. 4095 = maximum</div>
        </div>
      </div>
      <div class="expert-section">
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
        <label>Canal MIDI (0-15)</label>
        <input type="number" id="wiz-channel" min="0" max="15" value="0">
        <div class="help">Chaque instrument doit &ecirc;tre sur un canal diff&eacute;rent</div>
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
          <input type="number" id="wiz-count" min="1" max="32" value="8" oninput="wizBuildNoteTable()">
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

<!-- ============ CALIBRATION ============ -->
<div class="page" id="page-calibration">
  <div class="section-title">Calibration Acoustique</div>
  <div class="mic-status no" id="mic-status">Microphone I&sup2;S non d&eacute;tect&eacute; &mdash; Connectez un INMP441 (WS:15, SCK:14, SD:32)</div>
  <div id="cal-controls">

  <!-- État courant -->
  <div class="cards" style="grid-template-columns:repeat(auto-fit,minmax(160px,1fr))">
    <div class="card">
      <h3>État</h3>
      <div class="val" id="cal-state" style="font-size:18px">Inactif</div>
    </div>
    <div class="card">
      <h3>Progression</h3>
      <div class="val"><span id="cal-progress">0</span><span class="unit">%</span></div>
      <div class="bar"><div class="bar-fill" id="cal-bar" style="width:0%;background:var(--accent)"></div></div>
    </div>
    <div class="card">
      <h3>Actionneur en cours</h3>
      <div class="val" id="cal-cur-act">—</div>
    </div>
    <div class="card">
      <h3>Résultats</h3>
      <div class="val"><span id="cal-result-count">0</span><span class="unit">mesures</span></div>
    </div>
  </div>

  <!-- Boutons de contrôle -->
  <div class="btn-row" style="margin-bottom:20px">
    <button class="btn primary" onclick="startCalibrateAll()">▶ Calibrer tous</button>
    <button class="btn" onclick="startCalibrateOne()" id="cal-btn-one">Calibrer un...</button>
    <button class="btn danger" onclick="stopCalibration()" id="cal-btn-stop" style="display:none">⬛ Arrêter</button>
    <button class="btn" onclick="applyCalibrateResults()" id="cal-btn-apply" style="display:none">✓ Appliquer résultats</button>
    <button class="btn" onclick="loadCalibrateResults()" style="margin-left:auto">↻ Rafraîchir</button>
  </div>

  <!-- Sélecteur actionneur unique -->
  <div id="cal-single-sel" style="display:none;margin-bottom:16px">
    <label style="font-size:13px;margin-right:8px">Actionneur :</label>
    <select id="cal-act-select" class="form-select"></select>
    <button class="btn primary" style="margin-left:8px" onclick="confirmCalibrateOne()">Démarrer</button>
    <button class="btn" style="margin-left:4px" onclick="document.getElementById('cal-single-sel').style.display='none'">✕</button>
  </div>

  <!-- Tableau des résultats -->
  <div class="section-title">Résultats de calibration</div>
  <div class="table-responsive">
  <table>
    <thead>
      <tr>
        <th>ID</th>
        <th>Type</th>
        <th>Latence actuelle (ms)</th>
        <th>Latence mesurée (ms)</th>
        <th>Mesures</th>
        <th>État</th>
      </tr>
    </thead>
    <tbody id="cal-results-table">
      <tr><td colspan="6" style="color:var(--fg2);text-align:center">Aucun résultat — lancez une calibration</td></tr>
    </tbody>
  </table>
  </div>

  <!-- Note config I²S -->
  <div class="section-title" style="margin-top:24px">Configuration microphone</div>
  <div class="cards" style="grid-template-columns:repeat(auto-fit,minmax(160px,1fr))">
    <div class="card"><h3>Pins</h3><div class="val" style="font-size:14px">WS:15 SCK:14 SD:32</div></div>
    <div class="card"><h3>Sample Rate</h3><div class="val" style="font-size:18px">16 kHz</div></div>
    <div class="card"><h3>Retries / actionneur</h3><div class="val" style="font-size:18px">3</div></div>
    <div class="card"><h3>Fenêtre détection</h3><div class="val" style="font-size:18px">350 ms</div></div>
  </div>
  </div><!-- /cal-controls -->
</div>

<!-- ============ TEST & MAINTENANCE ============ -->
<div class="page" id="page-test">
  <div class="section-title">Test Industriel &amp; Maintenance</div>

  <!-- État courant -->
  <div class="cards" style="grid-template-columns:repeat(auto-fit,minmax(150px,1fr));margin-bottom:20px">
    <div class="card">
      <h3>Mode</h3>
      <div class="val" id="test-mode" style="font-size:18px">Inactif</div>
    </div>
    <div class="card">
      <h3>Progression</h3>
      <div class="val"><span id="test-progress">0</span><span class="unit">%</span></div>
      <div class="bar"><div class="bar-fill" id="test-bar" style="width:0%;background:var(--accent)"></div></div>
    </div>
    <div class="card">
      <h3>Actionneur</h3>
      <div class="val" id="test-cur-act" style="font-size:18px">—</div>
    </div>
    <div class="card">
      <h3>Événements envoyés</h3>
      <div class="val" id="test-events-sent">0</div>
    </div>
    <div class="card">
      <h3>Tests complétés</h3>
      <div class="val" id="test-tests-run">0</div>
    </div>
  </div>

  <!-- Contrôles globaux -->
  <div class="btn-row" style="margin-bottom:20px">
    <button class="btn danger" id="test-stop-btn" style="display:none" onclick="stopTest()">⬛ Arrêter</button>
    <button class="btn" onclick="api('/api/test/log/clear','POST',{}).then(loadTestLog)" style="margin-left:auto">🗑 Effacer journal</button>
    <button class="btn" onclick="loadTestStatus();loadTestLog()">↻</button>
  </div>

  <!-- === Sweep === -->
  <div class="section-title">Sweep — Test séquentiel</div>
  <div style="background:var(--bg2);border:1px solid var(--border);border-radius:var(--radius);padding:16px;margin-bottom:16px">
    <div class="form-row">
      <div class="form-group">
        <label>Vélocité</label>
        <input type="number" id="sw-vel" min="1" max="127" value="100">
      </div>
      <div class="form-group">
        <label>Intervalle (ms)</label>
        <input type="number" id="sw-interval" min="50" max="5000" value="400">
      </div>
      <div class="form-group">
        <label>Durée frappe (ms)</label>
        <input type="number" id="sw-hold" min="20" max="2000" value="120">
      </div>
      <div class="form-group" style="justify-content:flex-end;align-items:flex-end">
        <label><input type="checkbox" id="sw-loop"> Boucle infinie</label>
      </div>
    </div>
    <button class="btn primary" onclick="startSweep()">▶ Lancer sweep</button>
  </div>

  <!-- === Burst === -->
  <div class="section-title">Burst — Rafale sur un actionneur</div>
  <div style="background:var(--bg2);border:1px solid var(--border);border-radius:var(--radius);padding:16px;margin-bottom:16px">
    <div class="form-row">
      <div class="form-group">
        <label>Actionneur</label>
        <select id="burst-act" class="form-select"></select>
      </div>
      <div class="form-group">
        <label>Frappes</label>
        <input type="number" id="burst-count" min="1" max="50" value="5">
      </div>
      <div class="form-group">
        <label>Vélocité</label>
        <input type="number" id="burst-vel" min="1" max="127" value="100">
      </div>
      <div class="form-group">
        <label>Intervalle (ms)</label>
        <input type="number" id="burst-interval" min="30" max="2000" value="150">
      </div>
    </div>
    <button class="btn primary" onclick="startBurst()">▶ Lancer burst</button>
  </div>

  <!-- === Stress === -->
  <div class="section-title">Stress — Tous actionneurs simultanés</div>
  <div style="background:var(--bg2);border:1px solid var(--border);border-radius:var(--radius);padding:16px;margin-bottom:20px">
    <p style="color:var(--fg2);font-size:13px;margin-bottom:12px">Déclenche tous les actionneurs actifs en même temps. Le Power Manager et le Safety Manager filtrent selon les budgets.</p>
    <div class="form-row">
      <div class="form-group">
        <label>Vélocité</label>
        <input type="number" id="stress-vel" min="1" max="127" value="80">
      </div>
      <div class="form-group">
        <label>Durée frappe (ms)</label>
        <input type="number" id="stress-hold" min="20" max="2000" value="120">
      </div>
    </div>
    <button class="btn danger" onclick="startStress()">⚡ Stress test</button>
  </div>

  <!-- Journal -->
  <div class="section-title">Journal des événements
    <span style="font-size:12px;font-weight:400;color:var(--fg2);margin-left:8px">(32 derniers)</span>
  </div>
  <div class="table-responsive">
  <table>
    <thead>
      <tr><th>Temps</th><th>Actionneur</th><th>Vélocité</th><th>Mode</th><th>État</th></tr>
    </thead>
    <tbody id="test-log-table">
      <tr><td colspan="5" style="color:var(--fg2);text-align:center">Aucun événement</td></tr>
    </tbody>
  </table>
  </div>
</div>

<!-- ============ LOGS ============ -->
<div class="page" id="page-logs">
  <div class="section-title"><span>Journal système</span>
    <div style="display:flex;gap:6px">
      <button class="btn sm" onclick="loadLogs()">Actualiser</button>
      <button class="btn sm" onclick="clearLogs()">Effacer</button>
    </div>
  </div>
  <div style="display:flex;gap:12px;margin-bottom:14px;flex-wrap:wrap;align-items:center">
    <div>
      <label style="font-size:12px;color:var(--fg2)">Niveau min</label>
      <select id="log-level-filter" onchange="renderLogs()" class="form-select" style="margin-left:6px">
        <option value="0">Tout (DEBUG+)</option>
        <option value="1">INFO+</option>
        <option value="2">WARN+</option>
        <option value="3">ERROR+</option>
        <option value="4">CRITICAL</option>
      </select>
    </div>
    <div>
      <label style="font-size:12px;color:var(--fg2)">Catégorie</label>
      <select id="log-cat-filter" onchange="renderLogs()" class="form-select" style="margin-left:6px">
        <option value="-1">Toutes</option>
        <option value="0">Système</option>
        <option value="1">MIDI</option>
        <option value="2">Scheduler</option>
        <option value="3">Safety</option>
        <option value="4">Power</option>
        <option value="5">Calibration</option>
        <option value="6">Test</option>
      </select>
    </div>
    <label style="font-size:12px;color:var(--fg2);margin-left:auto;display:flex;align-items:center;gap:6px">
      <input type="checkbox" id="log-autoscroll" checked> Auto-scroll
    </label>
  </div>
  <div class="log-container">
    <div class="table-responsive">
    <table>
      <thead><tr><th style="width:80px">Temps</th><th style="width:60px">Niveau</th><th style="width:75px">Catégorie</th><th>Message</th></tr></thead>
      <tbody id="log-table"><tr><td colspan="4" style="color:var(--fg2)">Chargement…</td></tr></tbody>
    </table>
    </div>
  </div>
  <div id="log-count-info" style="color:var(--fg2);font-size:12px;margin-top:8px;text-align:right"></div>
</div>

<script>
// ============================================================================
// State
// ============================================================================
let ws = null;
let wsConnected = false;
let currentPage = 'home';
let instruments = [];
let actuators = [];
let routing = [];
let pianoNotes = {}; // note -> actuator_id mapping
let editingInstrumentIdx = -1;
let editingActuatorId = -1;

const SERVO_BEHAVIORS = ['Frappe','Alterné','Gratter','Touche'];
const SOL_BEHAVIORS = ['Frappe','Hit-and-Hold'];
const CC_TARGETS = ['Position','Amplitude','Vitesse','PWM Hold'];
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
      // Refresh logs si nouvelles entrées détectées
      if (d.log_count !== undefined && d.log_count !== logLastCount) {
        logLastCount = d.log_count;
        loadDashboardLog();
        if (currentPage === 'logs') loadLogs();
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

  // Power
  if (d.power) {
    el('d-power-ma', d.power.total_ma || 0);
    el('d-power-pct', d.power.budget_pct || 0);
    el('d-servo-ma', d.power.servo_ma || 0);
    el('d-sol-ma', d.power.sol_ma || 0);

    const pct = d.power.budget_pct || 0;
    const bar = document.getElementById('d-power-bar');
    bar.style.width = pct + '%';
    bar.style.background = pct > 80 ? 'var(--red)' : pct > 50 ? 'var(--yellow)' : 'var(--green)';

    // Power page
    el('p-total', d.power.total_ma || 0);
    el('p-servo', d.power.servo_ma || 0);
    el('p-sol', d.power.sol_ma || 0);
    const pbar = document.getElementById('p-total-bar');
    if (pbar) {
      pbar.style.width = pct + '%';
      pbar.style.background = pct > 80 ? 'var(--red)' : pct > 50 ? 'var(--yellow)' : 'var(--green)';
    }
    el('p-degrad', d.power.degradation ? 'Oui' : 'Non');
  }

  // Safety
  if (d.safety) {
    el('d-active', d.safety.active || 0);
    el('s-current', d.safety.current_ma || 0);
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

  // Active actuators table
  if (d.active_actuators) {
    const tbody = document.getElementById('d-active-table');
    if (d.active_actuators.length === 0) {
      tbody.innerHTML = '<tr><td colspan="4" style="color:var(--fg2)">Aucun actionneur actif</td></tr>';
    } else {
      let html = '';
      for (const a of d.active_actuators) {
        const act = actuators.find(x => x.id === a.id);
        html += '<tr><td>' + a.id + '</td>';
        html += '<td>' + (act ? (act.type === 0 ? '<span class="badge servo">Servo</span>' : '<span class="badge sol">Solénoïde</span>') : '-') + '</td>';
        html += '<td><span class="badge on">Actif</span></td>';
        html += '<td>' + a.pos + '</td></tr>';
      }
      tbody.innerHTML = html;
    }

    // Update piano active notes
    updatePianoActive(d.active_actuators);
  }
}

function updateAlerts(d) {
  let html = '';
  if (d.safety && d.safety.kill_switch) {
    html += '<div class="alert danger">KILL SWITCH ACTIF — Toutes les sorties sont désactivées</div>';
  }
  if (d.safety && d.safety.over_current) {
    html += '<div class="alert danger">SURINTENSITÉ — Courant estimé dépasse la limite</div>';
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
  if (d.safety && d.safety.over_current) {
    safetyHtml += '<div class="alert danger">SURINTENSITÉ DÉTECTÉE</div>';
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
  document.querySelectorAll('nav button').forEach(b => b.classList.remove('active'));
  document.getElementById('page-' + page).classList.add('active');
  document.querySelectorAll('nav button').forEach(b => {
    if (b.textContent.toLowerCase().includes(page) || b.onclick.toString().includes(page))
      b.classList.add('active');
  });

  // Load data for specific pages
  if (page === 'home') { loadHomeInstruments(); loadInstrumentSelects(); buildPiano(); }
  if (page === 'actuators') { loadActuatorsWithNotes(); }
  if (page === 'cc') { loadInstrumentSelects(); loadCCRouting(); }
  if (page === 'dashboard') loadDashboardLog();
  if (page === 'power') loadPower();
  if (page === 'safety') loadSafety();
  if (page === 'calibration') { checkMicStatus(); loadCalibrateStatus(); loadCalibrateResults(); }
  if (page === 'test') { loadTestStatus(); loadTestLog(); populateBurstSelect(); }
  if (page === 'logs') loadLogs();
  if (page === 'settings') { loadWiFiConfig(); loadMidiConfig(); loadBuses(); }
}

// ============================================================================
// Settings gear menu
// ============================================================================
function toggleSettingsMenu() {
  document.getElementById('settings-dropdown').classList.toggle('show');
}
function closeSettingsMenu() {
  document.getElementById('settings-dropdown').classList.remove('show');
}
document.addEventListener('click', (e) => {
  if (!e.target.closest('.gear-btn') && !e.target.closest('.settings-dropdown')) {
    closeSettingsMenu();
  }
});

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
  instruments = await api('/api/instruments');
  const tbody = document.getElementById('home-instruments-table');
  if (!tbody) return;
  if (!instruments || instruments.length === 0) {
    tbody.innerHTML = '<tr><td colspan="6" style="color:var(--fg2)">Aucun instrument &mdash; utilisez l\'assistant pour commencer</td></tr>';
    return;
  }
  let html = '';
  for (const inst of instruments) {
    const busType = inst.bus_id === 0 ? 'Servos' : 'Solénoïdes';
    html += '<tr>';
    html += '<td><strong>' + esc(inst.name) + '</strong></td>';
    html += '<td>Ch. ' + inst.channel + '</td>';
    html += '<td>' + busType + '</td>';
    html += '<td>' + (inst.actuator_count || 0) + '</td>';
    html += '<td>' + (inst.enabled ? '<span class="badge on">Actif</span>' : '<span class="badge off">Inactif</span>') + '</td>';
    html += '<td><button class="btn sm" onclick="editInstrument(' + inst.index + ')">Éditer</button> ';
    html += '<button class="btn sm" onclick="deleteInstrument(' + inst.index + ')">Suppr</button></td>';
    html += '</tr>';
  }
  tbody.innerHTML = html;
}

function openInstrumentModal() {
  editingInstrumentIdx = -1;
  document.getElementById('mi-name').value = '';
  document.getElementById('mi-channel').value = instruments ? instruments.length : 0;
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
  buildPiano();
}

async function deleteInstrument(idx) {
  if (!confirm('Supprimer cet instrument?')) return;
  await api('/api/instrument?index=' + idx, 'DELETE');
  instruments = [];  // Force refresh
  loadHomeInstruments();
  loadInstrumentSelects();
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
  updateAnglePreview();
}

function openActuatorModal() {
  editingActuatorId = -1;
  document.getElementById('ma-id').value = actuators ? actuators.length : 0;
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
  document.getElementById('ma-pulse').value = '20';
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
    document.getElementById('ma-angle-init').value = act.angle_init !== undefined ? act.angle_init : 90;
    document.getElementById('ma-amplitude').value = act.amplitude !== undefined ? act.amplitude : 45;
    document.getElementById('ma-speed').value = act.speed_ms || 150;
    document.getElementById('ma-angle-b').value = act.angle_b !== undefined ? act.angle_b : 120;
  } else {
    document.getElementById('ma-sol-behavior').value = act.behavior || 0;
    document.getElementById('ma-pulse').value = act.pulse_ms || 20;
    document.getElementById('ma-pwm-init').value = act.pwm_initial !== undefined ? act.pwm_initial : 4095;
    document.getElementById('ma-pwm-hold').value = act.pwm_hold !== undefined ? act.pwm_hold : 2048;
    document.getElementById('ma-ramp').value = act.ramp_ms || 50;
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
    data.angle_init = parseInt(document.getElementById('ma-angle-init').value);
    data.amplitude = parseInt(document.getElementById('ma-amplitude').value);
    data.speed_ms = parseInt(document.getElementById('ma-speed').value);
    data.angle_b = parseInt(document.getElementById('ma-angle-b').value);
  } else { // Solenoid
    data.behavior = parseInt(document.getElementById('ma-sol-behavior').value);
    data.pulse_ms = parseInt(document.getElementById('ma-pulse').value);
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

async function loadInstrumentSelects() {
  if (!instruments || instruments.length === 0) {
    instruments = await api('/api/instruments');
  }
  const selects = ['home-piano-instrument', 'cc-instrument'];
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
      opt.textContent = inst.name + ' (ch.' + inst.channel + ')';
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
    ctbody.innerHTML = '<tr><td colspan="6" style="color:var(--fg2)">Aucun mapping CC</td></tr>';
  } else {
    let html = '';
    for (const cm of r.ccs) {
      html += '<tr>';
      html += '<td>CC ' + cm.cc + '</td>';
      html += '<td>Actionneur #' + cm.actuator + '</td>';
      html += '<td>' + (CC_TARGETS[cm.target] || '?') + '</td>';
      html += '<td>' + cm.min + '</td>';
      html += '<td>' + cm.max + '</td>';
      html += '<td>' + (cm.enabled ? '<span class="badge on">Oui</span>' : '<span class="badge off">Non</span>') + '</td>';
      html += '</tr>';
    }
    ctbody.innerHTML = html;
  }

  updatePianoMapping();
}

// ============================================================================
// Piano
// ============================================================================
function buildPiano() {
  const container = document.getElementById('piano-keys');
  container.innerHTML = '';

  // Build 3 octaves (C3 to B5 = notes 48-83)
  const startNote = 48;
  const endNote = 84;
  let whiteIndex = 0;

  for (let n = startNote; n < endNote; n++) {
    const noteInOctave = n % 12;
    const isBlack = [1,3,6,8,10].includes(noteInOctave);

    if (!isBlack) {
      const key = document.createElement('div');
      key.className = 'white';
      key.dataset.note = n;
      key.textContent = noteName(n);
      key.onmousedown = () => pianoNoteOn(n);
      key.onmouseup = () => pianoNoteOff(n);
      key.onmouseleave = () => pianoNoteOff(n);
      key.addEventListener('touchstart', (e) => { e.preventDefault(); pianoNoteOn(n); }, {passive:false});
      key.addEventListener('touchend',   (e) => { e.preventDefault(); pianoNoteOff(n); }, {passive:false});
      container.appendChild(key);
      whiteIndex++;
    }
  }

  // Add black keys (absolute positioned)
  let wIdx = 0;
  for (let n = startNote; n < endNote; n++) {
    const noteInOctave = n % 12;
    const isBlack = [1,3,6,8,10].includes(noteInOctave);

    if (isBlack) {
      const key = document.createElement('div');
      key.className = 'black';
      key.dataset.note = n;
      key.style.left = (wIdx * 40 - 13) + 'px';
      key.onmousedown = (e) => { e.preventDefault(); pianoNoteOn(n); };
      key.onmouseup = () => pianoNoteOff(n);
      key.onmouseleave = () => pianoNoteOff(n);
      key.addEventListener('touchstart', (e) => { e.preventDefault(); pianoNoteOn(n); }, {passive:false});
      key.addEventListener('touchend',   (e) => { e.preventDefault(); pianoNoteOff(n); }, {passive:false});
      container.appendChild(key);
    } else {
      wIdx++;
    }
  }

  updatePianoMapping();
}

function updatePianoMapping() {
  pianoNotes = {};
  const instIdx = parseInt(document.getElementById('home-piano-instrument')?.value || '0');
  const r = routing ? routing.find(x => x.instrument === instIdx) : null;
  if (r && r.notes) {
    for (const nm of r.notes) {
      if (nm.enabled) pianoNotes[nm.note] = nm.actuator;
    }
  }

  // Highlight mapped keys
  document.querySelectorAll('.piano .white, .piano .black').forEach(key => {
    const n = parseInt(key.dataset.note);
    key.classList.toggle('mapped', n in pianoNotes);
  });
}

function pianoNoteOn(note) {
  const actId = pianoNotes[note];
  if (actId === undefined) return;

  // Visual feedback
  const key = document.querySelector('[data-note="' + note + '"]');
  if (key) key.classList.add('active');

  // Send test via WebSocket for speed
  if (ws && wsConnected) {
    ws.send(JSON.stringify({cmd:'test', id:actId, vel:100}));
  }
}

function pianoNoteOff(note) {
  const key = document.querySelector('[data-note="' + note + '"]');
  if (key) key.classList.remove('active');

  const actId = pianoNotes[note];
  if (actId === undefined) return;

  // Send note off
  api('/api/test/actuator', 'POST', {id: actId, velocity: 0, note_on: false});
}

function updatePianoActive(activeActuators) {
  if (currentPage !== 'home') return;

  // Reset all active states
  document.querySelectorAll('.piano .active').forEach(k => k.classList.remove('active'));

  if (!activeActuators) return;

  // Find notes for active actuators
  const activeIds = new Set(activeActuators.map(a => a.id));
  let activeNotes = [];
  for (const [note, actId] of Object.entries(pianoNotes)) {
    if (activeIds.has(actId)) {
      const key = document.querySelector('[data-note="' + note + '"]');
      if (key) key.classList.add('active');
      activeNotes.push(noteName(parseInt(note)));
    }
  }

  el('piano-active', activeNotes.length > 0 ? activeNotes.join(', ') : 'Aucune');
}

// ============================================================================
// Power
// ============================================================================
async function loadPower() {
  const d = await api('/api/power');
  if (!d) return;

  if (d.budget) {
    el('p-total-max', d.budget.global_max_ma);
    el('p-servo-max', d.budget.servo_max_ma);
    el('p-sol-max', d.budget.sol_max_ma);
    el('p-poly-max', d.budget.max_polyphony);
    document.getElementById('pw-global').value = d.budget.global_max_ma;
    document.getElementById('pw-servo').value = d.budget.servo_max_ma;
    document.getElementById('pw-sol').value = d.budget.sol_max_ma;
    document.getElementById('pw-poly').value = d.budget.max_polyphony;
  }
  if (d.stats) {
    el('p-rejected', d.stats.rejected);
    el('p-poly', d.stats.active_count);
  }
}

async function savePowerBudget() {
  await api('/api/power/budget', 'POST', {
    global_max_ma: parseInt(document.getElementById('pw-global').value),
    servo_max_ma: parseInt(document.getElementById('pw-servo').value),
    sol_max_ma: parseInt(document.getElementById('pw-sol').value),
    max_polyphony: parseInt(document.getElementById('pw-poly').value)
  });
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
    document.getElementById('sf-current').value = d.config.max_current_ma;
  }
}

async function saveSafetyConfig() {
  await api('/api/safety', 'POST', {
    max_duty_pct: parseInt(document.getElementById('sf-duty').value),
    max_freq_hz: parseInt(document.getElementById('sf-freq').value),
    watchdog_ms: parseInt(document.getElementById('sf-watchdog').value),
    max_polyphony: parseInt(document.getElementById('sf-poly').value),
    max_current_ma: parseInt(document.getElementById('sf-current').value)
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
    tbody.innerHTML = '<tr><td colspan="8" style="color:var(--fg2)">Aucun bus détecté</td></tr>';
    return;
  }
  let html = '';
  for (const b of buses) {
    html += '<tr>';
    html += '<td>Bus ' + b.id + '</td>';
    html += '<td>GPIO ' + b.sda + '</td>';
    html += '<td>GPIO ' + b.scl + '</td>';
    html += '<td>GPIO ' + b.oe + '</td>';
    html += '<td>' + (b.freq_i2c / 1000) + ' kHz</td>';
    html += '<td>' + b.freq_pwm + ' Hz</td>';
    html += '<td>' + b.pca_count + ' PCA</td>';
    html += '<td>' + (b.enabled ? '<span class="badge on">Actif</span>' : '<span class="badge off">Inactif</span>') + '</td>';
    html += '</tr>';
  }
  tbody.innerHTML = html;
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
// Test Manager (Phase 8)
// ============================================================================
let testPollInterval = null;

const TEST_MODE_NAMES = {
  idle:'Inactif', sweep:'Sweep ▶', burst:'Burst ▶', stress:'Stress ▶'
};

async function loadTestStatus() {
  const d = await api('/api/test/status');
  if (!d) return;

  el('test-mode', TEST_MODE_NAMES[d.mode] || d.mode);
  el('test-progress', d.progress || 0);
  el('test-events-sent', d.events_sent || 0);
  el('test-tests-run', d.tests_run || 0);

  const bar = document.getElementById('test-bar');
  if (bar) bar.style.width = (d.progress || 0) + '%';

  const stopBtn = document.getElementById('test-stop-btn');
  if (stopBtn) stopBtn.style.display = d.running ? 'inline-block' : 'none';

  if (d.running) {
    el('test-cur-act', 'Act ' + d.current_act);
    if (!testPollInterval) testPollInterval = setInterval(loadTestStatus, 800);
  } else {
    el('test-cur-act', '—');
    if (testPollInterval) { clearInterval(testPollInterval); testPollInterval = null; }
    loadTestLog();
  }
}

async function loadTestLog() {
  const data = await api('/api/test/log');
  const tbody = document.getElementById('test-log-table');
  if (!tbody || !data || !data.length) {
    if (tbody) tbody.innerHTML = '<tr><td colspan="5" style="color:var(--fg2);text-align:center">Aucun événement</td></tr>';
    return;
  }
  let html = '';
  for (const e of data) {
    const badge = e.ok
      ? '<span class="badge on">OK</span>'
      : '<span class="badge off">Queue pleine</span>';
    html += '<tr>';
    html += '<td>' + e.t + ' ms</td>';
    html += '<td>Act ' + e.act + '</td>';
    html += '<td>' + e.vel + '</td>';
    html += '<td>' + (TEST_MODE_NAMES[e.mode] || e.mode) + '</td>';
    html += '<td>' + badge + '</td>';
    html += '</tr>';
  }
  tbody.innerHTML = html;
}

function startSweep() {
  if (document.getElementById('test-stop-btn').style.display !== 'none') {
    toast('Un test est déjà en cours', 'warn'); return;
  }
  const body = {
    velocity:    parseInt(document.getElementById('sw-vel').value),
    interval_ms: parseInt(document.getElementById('sw-interval').value),
    hold_ms:     parseInt(document.getElementById('sw-hold').value),
    loop:        document.getElementById('sw-loop').checked
  };
  api('/api/test/sweep', 'POST', body).then(r => {
    if (r && r.ok) {
      toast('Sweep démarré', 'ok');
      loadTestStatus();
      testPollInterval = testPollInterval || setInterval(loadTestStatus, 800);
    } else {
      toast('Erreur : ' + (r && r.error ? r.error : 'inconnue'), 'error');
    }
  });
}

function startBurst() {
  if (document.getElementById('test-stop-btn').style.display !== 'none') {
    toast('Un test est déjà en cours', 'warn'); return;
  }
  const sel = document.getElementById('burst-act');
  const body = {
    id:          parseInt(sel ? sel.value : 0),
    count:       parseInt(document.getElementById('burst-count').value),
    velocity:    parseInt(document.getElementById('burst-vel').value),
    interval_ms: parseInt(document.getElementById('burst-interval').value)
  };
  api('/api/test/burst', 'POST', body).then(r => {
    if (r && r.ok) {
      toast('Burst démarré (act ' + body.id + ')', 'ok');
      loadTestStatus();
      testPollInterval = testPollInterval || setInterval(loadTestStatus, 800);
    } else {
      toast('Erreur : ' + (r && r.error ? r.error : 'inconnue'), 'error');
    }
  });
}

function startStress() {
  if (!confirm('Lancer le stress test ?\nTous les actionneurs vont se déclencher simultanément.')) return;
  const body = {
    velocity: parseInt(document.getElementById('stress-vel').value),
    hold_ms:  parseInt(document.getElementById('stress-hold').value)
  };
  api('/api/test/stress', 'POST', body).then(r => {
    if (r && r.ok) {
      toast('Stress test exécuté', 'ok');
      setTimeout(loadTestLog, 500);
    } else {
      toast('Erreur : ' + (r && r.error ? r.error : 'inconnue'), 'error');
    }
  });
}

function stopTest() {
  api('/api/test/stop', 'POST', {}).then(r => {
    if (r && r.ok) toast('Test arrêté', 'warn');
    loadTestStatus();
  });
}

function populateBurstSelect() {
  const sel = document.getElementById('burst-act');
  if (!sel) return;
  sel.innerHTML = '';
  for (const a of actuators) {
    const opt = document.createElement('option');
    opt.value = a.id;
    opt.textContent = 'Act ' + a.id + ' (' + (a.type === 0 ? 'Servo' : 'Sol') + ' ch' + a.pca_ch + ')';
    sel.appendChild(opt);
  }
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
      loadDashboardLog();
    } else {
      toast('Erreur', 'error');
    }
  });
}

// Mini-log pour le Dashboard (5 dernières entrées INFO+)
async function loadDashboardLog() {
  try {
    const r = await api('/api/logs');
    if (!r || !r.entries) return;
    const recent = r.entries.filter(e => e.lvl >= 1).slice(0, 5);
    const tbody = document.getElementById('d-log-table');
    if (!tbody) return;
    if (recent.length === 0) {
      tbody.innerHTML = '<tr><td colspan="3" style="color:var(--fg2)">Aucun événement récent</td></tr>';
      return;
    }
    let html = '';
    for (const e of recent) {
      html += '<tr>';
      html += '<td style="font-size:11px;font-family:monospace">' + formatLogTime(e.t) + '</td>';
      html += '<td>' + logLevelBadge(e.lvl) + '</td>';
      html += '<td style="font-size:12px">' + escHtml(e.msg) + '</td>';
      html += '</tr>';
    }
    tbody.innerHTML = html;
  } catch(e) {}
}

// ============================================================================
// Expert collapsible toggle
// ============================================================================
function toggleExpert(btn) {
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
  document.getElementById('wiz-channel').value = instruments ? instruments.length : 0;
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
  const count = Math.min(parseInt(document.getElementById('wiz-count').value) || 8, 32);
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

  let html = '<strong>' + esc(name) + '</strong> — Canal MIDI ' + ch + '<br>';
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

  // 2. Create actuators
  const baseId = actuators ? actuators.length : 0;
  for (let i = 0; i < count; i++) {
    const actData = {
      id: baseId + i, type, bus_id: busId, pca_addr: pcaAddr, pca_ch: pcaCh,
      latency_ms: 10, behavior, enabled: true
    };
    if (type === 0) {
      actData.angle_init = 90; actData.amplitude = 45; actData.speed_ms = 150; actData.angle_b = 120;
    } else {
      actData.pulse_ms = 20; actData.pwm_initial = 4095; actData.pwm_hold = 2048; actData.ramp_ms = 50;
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
  buildPiano();
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
  const micDiv = document.getElementById('mic-status');
  const ctrlDiv = document.getElementById('cal-controls');
  if (!micDiv) return;
  try {
    const d = await api('/api/calibrate/status');
    // Bug fix: backend returns 'available' field, not 'mic_detected'
    if (d && d.available) {
      micDiv.className = 'mic-status ok';
      micDiv.innerHTML = '&#9679; Calibrateur actif &mdash; Pr&ecirc;t pour la calibration acoustique';
      if (ctrlDiv) ctrlDiv.style.display = 'block';
    } else {
      micDiv.className = 'mic-status no';
      micDiv.innerHTML = '&#9679; Calibrateur non disponible &mdash; Microphone INMP441 requis (WS:15, SCK:14, SD:32)';
      if (ctrlDiv) ctrlDiv.style.display = 'none';
    }
  } catch(e) {
    // API unavailable, show controls anyway
    micDiv.style.display = 'none';
    if (ctrlDiv) ctrlDiv.style.display = 'block';
  }
}

// ============================================================================
// Init
// ============================================================================
window.addEventListener('load', () => {
  connectWS();
  // Preload data then show home page
  loadActuators().then(() => {
    loadInstrumentSelects();
    populateBurstSelect();
    api('/api/routing').then(r => {
      routing = r || [];
      // Show home page with instruments + piano
      loadHomeInstruments();
      buildPiano();
    });
  });
});
</script>
</body>
</html>
)rawhtml";

#endif // WEB_UI_H
