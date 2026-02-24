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
</div>

<!-- Navigation -->
<nav>
  <button class="active" onclick="showPage('dashboard')">Dashboard</button>
  <button onclick="showPage('instruments')">Instruments</button>
  <button onclick="showPage('actuators')">Actionneurs</button>
  <button onclick="showPage('midi')">MIDI / Mapping</button>
  <button onclick="showPage('piano')">Piano</button>
  <button onclick="showPage('power')">Power</button>
  <button onclick="showPage('safety')">Safety</button>
  <button onclick="showPage('calibration')">Calibration</button>
  <button onclick="showPage('test')">Test</button>
  <button onclick="showPage('logs')">Logs</button>
  <button onclick="showPage('settings')">Paramètres</button>
</nav>

<!-- ============ DASHBOARD ============ -->
<div class="page active" id="page-dashboard">
  <div id="alert-zone"></div>

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

<!-- ============ INSTRUMENTS ============ -->
<div class="page" id="page-instruments">
  <div class="section-title"><span>Instruments</span>
    <button class="btn primary sm" onclick="openInstrumentModal()">+ Ajouter</button>
  </div>
  <div class="table-responsive">
  <table>
    <thead><tr><th>Nom</th><th>Canal</th><th>Bus</th><th>Actionneurs</th><th>Latence</th><th>État</th><th>Actions</th></tr></thead>
    <tbody id="instruments-table"><tr><td colspan="7" style="color:var(--fg2)">Chargement...</td></tr></tbody>
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
    <thead><tr><th>ID</th><th>Type</th><th>Bus</th><th>PCA</th><th>Ch</th><th>Mode</th><th>Latence</th><th>État</th><th>Actions</th></tr></thead>
    <tbody id="actuators-table"><tr><td colspan="9" style="color:var(--fg2)">Chargement...</td></tr></tbody>
  </table>
  </div>
</div>

<!-- ============ MIDI / MAPPING ============ -->
<div class="page" id="page-midi">
  <div class="section-title">Configuration MIDI</div>
  <div class="cards" style="margin-bottom:20px">
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
    </div>
  </div>

  <div class="section-title"><span>Mapping MIDI</span>
    <select id="mapping-instrument" onchange="loadRouting()" class="form-select">
    </select>
  </div>
  <div class="section-title" style="font-size:14px">Notes</div>
  <div class="table-responsive">
  <table>
    <thead><tr><th>Note MIDI</th><th>Actionneur</th><th>Actif</th><th>Actions</th></tr></thead>
    <tbody id="mapping-notes-table"><tr><td colspan="4" style="color:var(--fg2)">Sélectionner un instrument</td></tr></tbody>
  </table>
  </div>

  <div class="section-title" style="font-size:14px">Control Changes</div>
  <div class="table-responsive">
  <table>
    <thead><tr><th>CC#</th><th>Actionneur</th><th>Cible</th><th>Min</th><th>Max</th><th>Actif</th></tr></thead>
    <tbody id="mapping-cc-table"><tr><td colspan="6" style="color:var(--fg2)">Sélectionner un instrument</td></tr></tbody>
  </table>
  </div>
</div>

<!-- ============ PIANO ============ -->
<div class="page" id="page-piano">
  <div class="section-title"><span>Piano virtuel</span>
    <select id="piano-instrument" onchange="updatePianoMapping()" class="form-select">
    </select>
  </div>
  <p style="color:var(--fg2);font-size:12px;margin-bottom:12px">Cliquer sur une touche pour tester l'actionneur correspondant. Les touches bleues sont mappées.</p>
  <div class="piano-container">
    <div class="piano" id="piano-keys"></div>
  </div>
  <div style="margin-top:16px">
    <div class="section-title" style="font-size:14px">Notes actives</div>
    <div id="piano-active" style="color:var(--fg2);font-size:13px">Aucune</div>
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
      <label>Global max (mA)</label>
      <input type="number" id="pw-global" value="6000">
    </div>
    <div class="form-group">
      <label>Servo bus max (mA)</label>
      <input type="number" id="pw-servo" value="3000">
    </div>
    <div class="form-group">
      <label>Solénoïde bus max (mA)</label>
      <input type="number" id="pw-sol" value="4000">
    </div>
  </div>
  <div class="form-row">
    <div class="form-group">
      <label>Polyphonie max</label>
      <input type="number" id="pw-poly" value="12" min="1" max="32">
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

  <div class="section-title" style="font-size:14px">Limites de sécurité</div>
  <div class="form-row tri">
    <div class="form-group">
      <label>Max duty cycle (%)</label>
      <input type="number" id="sf-duty" value="80" min="10" max="100">
    </div>
    <div class="form-group">
      <label>Max fréquence (Hz)</label>
      <input type="number" id="sf-freq" value="50" min="1" max="200">
    </div>
    <div class="form-group">
      <label>Watchdog (ms)</label>
      <input type="number" id="sf-watchdog" value="5000" min="1000" max="30000">
    </div>
  </div>
  <div class="form-row">
    <div class="form-group">
      <label>Max polyphonie</label>
      <input type="number" id="sf-poly" value="12" min="1" max="32">
    </div>
    <div class="form-group">
      <label>Max courant (mA)</label>
      <input type="number" id="sf-current" value="5000" min="500" max="20000">
    </div>
  </div>
  <button class="btn primary" onclick="saveSafetyConfig()">Appliquer</button>
</div>

<!-- ============ SETTINGS ============ -->
<div class="page" id="page-settings">
  <div class="section-title">WiFi</div>
  <div class="form-row">
    <div class="form-group">
      <label>SSID</label>
      <input type="text" id="set-ssid" maxlength="32">
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
    </div>
    <div class="form-group">
      <label>AP Fallback</label>
      <select id="set-ap-fallback">
        <option value="1">Oui</option>
        <option value="0">Non</option>
      </select>
    </div>
  </div>
  <button class="btn primary" onclick="saveWiFiConfig()">Sauvegarder WiFi</button>

  <div class="section-title" style="margin-top:24px">Bus I²C</div>
  <div class="table-responsive">
  <table>
    <thead><tr><th>Bus</th><th>SDA</th><th>SCL</th><th>OE</th><th>Freq I²C</th><th>Freq PWM</th><th>PCA détectés</th><th>Actions</th></tr></thead>
    <tbody id="buses-table"><tr><td colspan="8" style="color:var(--fg2)">Chargement...</td></tr></tbody>
  </table>
  </div>
  <button class="btn" onclick="scanI2C()">Scanner bus I²C</button>

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
    <div class="form-group"><label>Nom</label><input type="text" id="mi-name" maxlength="31"></div>
    <div class="form-row">
      <div class="form-group"><label>Canal MIDI (0-15)</label><input type="number" id="mi-channel" min="0" max="15" value="0"></div>
      <div class="form-group"><label>Bus I²C</label><select id="mi-bus"><option value="0">Bus 0 (Servos)</option><option value="1">Bus 1 (Solénoïdes)</option></select></div>
    </div>
    <div class="form-row">
      <div class="form-group"><label>Latence par défaut (ms)</label><input type="number" id="mi-latency" value="10" min="0" max="500"></div>
      <div class="form-group"><label>Auto-calibration</label><select id="mi-autocal"><option value="0">Non</option><option value="1">Oui</option></select></div>
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
      <div class="form-group"><label>ID</label><input type="number" id="ma-id" min="0" max="31" value="0"></div>
      <div class="form-group"><label>Type</label><select id="ma-type" onchange="toggleActuatorFields()"><option value="0">Servo</option><option value="1">Solénoïde</option></select></div>
    </div>
    <div class="form-row">
      <div class="form-group"><label>Bus I²C</label><select id="ma-bus"><option value="0">Bus 0</option><option value="1">Bus 1</option></select></div>
      <div class="form-group"><label>Adresse PCA</label><select id="ma-pca"><option value="64">0x40</option><option value="65">0x41</option><option value="66">0x42</option><option value="67">0x43</option></select></div>
    </div>
    <div class="form-row">
      <div class="form-group"><label>Canal PCA (0-15)</label><input type="number" id="ma-ch" min="0" max="15" value="0"></div>
      <div class="form-group"><label>Latence (ms)</label><input type="number" id="ma-latency" min="0" max="500" value="10"></div>
    </div>

    <!-- Servo fields -->
    <div id="servo-fields">
      <div class="form-group"><label>Comportement</label><select id="ma-servo-behavior"><option value="0">Frappe</option><option value="1">Alterné</option><option value="2">Gratter</option><option value="3">Touche</option></select></div>
      <div class="form-row">
        <div class="form-group"><label>Angle initial (°)</label><input type="number" id="ma-angle-init" min="0" max="180" value="90"></div>
        <div class="form-group"><label>Amplitude (°)</label><input type="number" id="ma-amplitude" min="0" max="180" value="45"></div>
      </div>
      <div class="form-row">
        <div class="form-group"><label>Vitesse (ms)</label><input type="number" id="ma-speed" min="10" max="2000" value="150"></div>
        <div class="form-group"><label>Angle B (°)</label><input type="number" id="ma-angle-b" min="0" max="180" value="120"></div>
      </div>
    </div>

    <!-- Solenoid fields -->
    <div id="solenoid-fields" style="display:none">
      <div class="form-group"><label>Comportement</label><select id="ma-sol-behavior"><option value="0">Frappe</option><option value="1">Hit-and-Hold</option></select></div>
      <div class="form-row">
        <div class="form-group"><label>Pulse (ms)</label><input type="number" id="ma-pulse" min="5" max="50" value="20"></div>
        <div class="form-group"><label>PWM initial</label><input type="number" id="ma-pwm-init" min="0" max="4095" value="4095"></div>
      </div>
      <div class="form-row">
        <div class="form-group"><label>PWM hold</label><input type="number" id="ma-pwm-hold" min="0" max="4095" value="2048"></div>
        <div class="form-group"><label>Rampe (ms)</label><input type="number" id="ma-ramp" min="10" max="500" value="50"></div>
      </div>
    </div>

    <div class="btn-row">
      <button class="btn primary" onclick="saveActuator()">Sauvegarder</button>
      <button class="btn" onclick="closeModal('modal-actuator')">Annuler</button>
    </div>
  </div>
</div>

<!-- ============ CALIBRATION ============ -->
<div class="page" id="page-calibration">
  <div class="section-title">Calibration Acoustique</div>
  <p style="color:var(--fg2);margin-bottom:16px;font-size:13px">
    Mesure automatique de la latence mécanique de chaque actionneur via microphone I²S (INMP441).
    Connectez un micro sur WS=GPIO15, SCK=GPIO14, SD=GPIO32 et placez-le près des actionneurs.
  </p>

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
let currentPage = 'dashboard';
let instruments = [];
let actuators = [];
let routing = [];
let pianoNotes = {}; // note -> actuator_id mapping

const SERVO_BEHAVIORS = ['Frappe','Alterné','Gratter','Touche'];
const SOL_BEHAVIORS = ['Frappe','Hit-and-Hold'];
const CC_TARGETS = ['Position','Amplitude','Vitesse','PWM Hold'];
const NOTE_NAMES = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];

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
  if (page === 'instruments') loadInstruments();
  if (page === 'actuators') loadActuators();
  if (page === 'midi') { loadMidiConfig(); loadInstrumentSelects(); }
  if (page === 'piano') { loadInstrumentSelects(); buildPiano(); }
  if (page === 'power') loadPower();
  if (page === 'safety') loadSafety();
  if (page === 'calibration') { loadCalibrateStatus(); loadCalibrateResults(); }
  if (page === 'test') { loadTestStatus(); loadTestLog(); populateBurstSelect(); }
  if (page === 'logs') loadLogs();
  if (page === 'settings') { loadWiFiConfig(); loadBuses(); }
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
// Instruments
// ============================================================================
async function loadInstruments() {
  instruments = await api('/api/instruments');
  const tbody = document.getElementById('instruments-table');
  if (!instruments || instruments.length === 0) {
    tbody.innerHTML = '<tr><td colspan="7" style="color:var(--fg2)">Aucun instrument configuré</td></tr>';
    return;
  }
  let html = '';
  for (const inst of instruments) {
    html += '<tr>';
    html += '<td><strong>' + esc(inst.name) + '</strong></td>';
    html += '<td>' + inst.channel + '</td>';
    html += '<td>Bus ' + inst.bus_id + '</td>';
    html += '<td>' + inst.actuator_count + '</td>';
    html += '<td>' + inst.latency_ms + ' ms</td>';
    html += '<td>' + (inst.enabled ? '<span class="badge on">Actif</span>' : '<span class="badge off">Inactif</span>') + '</td>';
    html += '<td><button class="btn sm" onclick="deleteInstrument(' + inst.index + ')">Suppr</button></td>';
    html += '</tr>';
  }
  tbody.innerHTML = html;
}

function openInstrumentModal() {
  document.getElementById('mi-name').value = '';
  document.getElementById('mi-channel').value = '0';
  document.getElementById('mi-bus').value = '0';
  document.getElementById('mi-latency').value = '10';
  document.getElementById('mi-autocal').value = '0';
  document.getElementById('modal-inst-title').textContent = 'Nouvel instrument';
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
  await api('/api/instrument', 'POST', data);
  closeModal('modal-instrument');
  loadInstruments();
}

async function deleteInstrument(idx) {
  if (!confirm('Supprimer cet instrument?')) return;
  await api('/api/instrument?index=' + idx, 'DELETE');
  loadInstruments();
}

// ============================================================================
// Actuators
// ============================================================================
async function loadActuators() {
  actuators = await api('/api/actuators');
  const tbody = document.getElementById('actuators-table');
  if (!actuators || actuators.length === 0) {
    tbody.innerHTML = '<tr><td colspan="9" style="color:var(--fg2)">Aucun actionneur configuré</td></tr>';
    return;
  }
  let html = '';
  for (const act of actuators) {
    const isServo = act.type === 0;
    const behaviors = isServo ? SERVO_BEHAVIORS : SOL_BEHAVIORS;
    html += '<tr>';
    html += '<td>' + act.id + '</td>';
    html += '<td>' + (isServo ? '<span class="badge servo">Servo</span>' : '<span class="badge sol">Solénoïde</span>') + '</td>';
    html += '<td>Bus ' + act.bus_id + '</td>';
    html += '<td>0x' + act.pca_addr.toString(16).toUpperCase() + '</td>';
    html += '<td>' + act.pca_ch + '</td>';
    html += '<td>' + (behaviors[act.behavior] || '?') + '</td>';
    html += '<td>' + act.latency_ms + ' ms</td>';
    html += '<td>' + (act.state && act.state.active ? '<span class="badge on">Actif</span>' : '<span class="badge off">Repos</span>') + '</td>';
    html += '<td><button class="btn sm" onclick="testActuator(' + act.id + ')">Test</button> ';
    html += '<button class="btn sm" onclick="deleteActuator(' + act.id + ')">Suppr</button></td>';
    html += '</tr>';
  }
  tbody.innerHTML = html;
}

function toggleActuatorFields() {
  const type = document.getElementById('ma-type').value;
  document.getElementById('servo-fields').style.display = type === '0' ? 'block' : 'none';
  document.getElementById('solenoid-fields').style.display = type === '1' ? 'block' : 'none';
}

function openActuatorModal() {
  document.getElementById('ma-id').value = actuators ? actuators.length : 0;
  document.getElementById('ma-type').value = '0';
  toggleActuatorFields();
  document.getElementById('modal-act-title').textContent = 'Nouvel actionneur';
  document.getElementById('modal-actuator').classList.add('show');
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
  loadActuators();
}

async function testActuator(id) {
  // Note ON puis Note OFF après 500ms
  await api('/api/test/actuator', 'POST', {id: id, velocity: 100, note_on: true});
  setTimeout(() => {
    api('/api/test/actuator', 'POST', {id: id, velocity: 0, note_on: false});
  }, 500);
}

async function deleteActuator(id) {
  if (!confirm('Désactiver cet actionneur?')) return;
  await api('/api/actuator?id=' + id, 'DELETE');
  loadActuators();
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
  const selects = ['mapping-instrument', 'piano-instrument'];
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
// Routing / Mapping
// ============================================================================
async function loadRouting() {
  routing = await api('/api/routing');
  const instIdx = parseInt(document.getElementById('mapping-instrument').value);
  const r = routing ? routing.find(x => x.instrument === instIdx) : null;

  // Notes table
  const ntbody = document.getElementById('mapping-notes-table');
  if (!r || !r.notes || r.notes.length === 0) {
    ntbody.innerHTML = '<tr><td colspan="4" style="color:var(--fg2)">Aucun mapping note</td></tr>';
  } else {
    let html = '';
    for (const nm of r.notes) {
      html += '<tr>';
      html += '<td>' + noteName(nm.note) + ' (' + nm.note + ')</td>';
      html += '<td>Actionneur #' + nm.actuator + '</td>';
      html += '<td>' + (nm.enabled ? '<span class="badge on">Oui</span>' : '<span class="badge off">Non</span>') + '</td>';
      html += '<td><button class="btn sm" onclick="removeNoteMapping(' + instIdx + ',' + nm.note + ')">Suppr</button></td>';
      html += '</tr>';
    }
    ntbody.innerHTML = html;
  }

  // CC table
  const ctbody = document.getElementById('mapping-cc-table');
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

  // Update piano mapping cache
  updatePianoMapping();
}

async function removeNoteMapping(instIdx, note) {
  // Reload routing without this note
  const r = routing ? routing.find(x => x.instrument === instIdx) : null;
  if (!r) return;
  const notes = r.notes.filter(n => n.note !== note);
  await api('/api/routing', 'POST', { instrument: instIdx, notes: notes });
  loadRouting();
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
  const instIdx = parseInt(document.getElementById('piano-instrument')?.value || '0');
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
  if (currentPage !== 'piano') return;

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
// Init
// ============================================================================
window.addEventListener('load', () => {
  connectWS();
  // Preload data
  loadActuators().then(() => { loadInstrumentSelects(); populateBurstSelect(); });
  api('/api/routing').then(r => { routing = r || []; });
  loadDashboardLog();
});
</script>
</body>
</html>
)rawhtml";

#endif // WEB_UI_H
