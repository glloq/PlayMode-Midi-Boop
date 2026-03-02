# Interface Web

> Voir aussi : [API REST](api-rest.md) — [Architecture](architecture.md) — [Calibration & Tests](calibration-tests.md)

## 1. Objectifs principaux de l'UI

* **Clarté immédiate** → l'utilisateur comprend ce qu'il peut faire dès la page principale
* **No-code / adaptable** → ajouter/modifier actionneurs, instruments, CC facilement
* **Monitoring temps réel** → notes actives, latences, bus, PCA, état sécurité
* **Guidage à la première utilisation** → page Bienvenue avec guide 3 étapes

---

# 2. Structure de l'interface (implémentée)

L'UI est une application single-page embarquée en PROGMEM (~3000 lignes HTML/CSS/JS vanilla).
Thème sombre GitHub-style (#0d1117 bg, #58a6ff accent, #3fb950 succès, #f85149 erreur).

### 2.1 Navigation

3 onglets principaux + Calibration (conditionnel) + Réglages (engrenage ⚙) :

| Page | ID | Visible par défaut | Description |
|------|----|--------------------|-------------|
| **Bienvenue** | `page-welcome` | Si 0 instruments | Guide 3 étapes : Créer → Connecter → Jouer |
| **Instrument** | `page-instrument` | Oui (défaut) | Gestion instruments + pianos virtuels |
| **MIDI** | `page-midi` | Oui | Transports MIDI + messages en temps réel |
| **Actionneurs** | `page-actuators` | Oui | Table actionneurs + CC routing |
| **Calibration** | `page-calibration` | Non (conditionnel) | Calibration acoustique + tests |
| **Réglages** | `page-settings` | Via engrenage ⚙ | Monitoring, sécurité, logs, WiFi, config |

### 2.2 Page Bienvenue (first-run)

* Affichée automatiquement si aucun instrument n'existe
* Logo Midi B∞p centré dans le header
* 3 étapes illustrées : Créer → Connecter → Jouer
* Bouton principal "Créer mon premier instrument" → lance le wizard 4 étapes
* Lien secondaire "passer à la configuration manuelle"
* Disparaît dès qu'un instrument est créé

### 2.3 Page Instrument

* **Liste instruments** : tableau (nom, canal, type, actionneurs, état, actions)
* **Création** :
  * Bouton "Wizard" → assistant 4 étapes (identité, type, notes MIDI, résumé)
  * Bouton "+ Manuel" → modal complet
* **Édition** : modal avec tous les paramètres
* **Suppression** : confirmation via `appConfirm()` thémé
* **Piano(s) virtuels** : un clavier interactif par instrument
  * Notes MIDI visuelles, scroll tactile
  * Feedback en temps réel sur notes actives

### 2.4 Page MIDI

* **Transports MIDI** : 3 cartes dédiées avec toggle + status :
  * Câble MIDI (DIN / TRS) — Serial 31250 baud, GPIO 4
  * WiFi — UDP brut (port 5004)
  * WiFi — Apple / RTP-MIDI (AppleMIDI, synchronisé)
* **Jitter buffer** : slider 10–80 ms (défaut 30 ms)
  * Aide contextuelle : "Tampon anti-gigue pour le MIDI réseau"
* **Messages MIDI reçus** : table temps réel (type, canal, note, vélocité, source, timestamp)
  * Bouton pause / effacer

### 2.5 Page Actionneurs

* **Table complète** : ID, type (servo/solénoïde), note MIDI, bus, PCA, canal, mode, état
* **Édition** : modal avec paramètres dynamiques selon type et comportement
  * Servo : angle initial, amplitude, vitesse, angle B, sens de frappe
  * Solénoïde : durée min/max, PWM initial, PWM hold, rampe
* **CC Routing** : table des Control Changes par instrument
  * CC number, actionneur cible, paramètre cible (position/amplitude/speed/PWM hold)

### 2.6 Page Calibration (conditionnelle)

* Onglet masqué par défaut (`#nav-cal` en `display:none`)
* **Calibration acoustique** : micro I²S INMP441, progression, résultats de latence
* **Tests actionneurs** :
  * Sweep : balayage séquentiel de tous les actionneurs
  * Burst : rafale sur un actionneur spécifique
  * Stress : charge maximale simultanée
* Journal d'événements des tests (64 entrées)

### 2.7 Page Réglages

* **Monitoring** : 4 cartes temps réel
  * MIDI : messages reçus/routés/rejetés
  * Polyphonie : barre de progression + actifs/rejetés
  * Scheduler : événements en queue/traités
  * WiFi : IP, mode (STA/AP), signal
* **Polyphonie & Sécurité** :
  * Polyphonie max (1 seul input partagé)
  * Kill Switch (toggle)
  * Dégradation gracieuse
  * Limites avancées (dépliable) : duty cycle, fréquence, watchdog, courant
* **Journal système** :
  * Logs filtrables par niveau (DEBUG, INFO, WARN, ERROR, CRITICAL)
  * Filtre par catégorie (Système, MIDI, Scheduler, Safety, Power, Calibration, Test)
  * Auto-scroll, bouton effacer
* **Connexion WiFi** : SSID, mot de passe, hostname, AP fallback
* **Bus I²C** (avancé, dépliable) : fréquence PWM configurable par bus
* **Configuration** : sauvegarde flash LittleFS, réinitialisation défauts

---

# 3. Modals et dialogues

* **`appConfirm()`** : remplace tous les `confirm()` natifs
  * Design intégré au thème sombre
  * Icônes contextuelles, support danger/primary
  * Textes de boutons personnalisables
* **`appAlert()`** : remplace tous les `alert()` natifs
  * Même style thémé
* **Modals d'édition** : instrument, actionneur, CC mapping
* **Wizard** : modal multi-étapes avec progression visuelle

---

# 4. Communication temps réel

* **WebSocket** (`/ws`) : broadcast état complet toutes les 200 ms
  * Scheduler : queue, événements traités
  * MIDI : compteurs serial/UDP/RTP, messages routés/rejetés
  * Safety : courant estimé, actifs, kill switch, dégradation
  * Power : budget utilisé (%), bus servo/solénoïde
  * Actionneurs : états actifs
* **Toasts** : notifications temporaires (succès vert, erreur rouge, avertissement jaune)
* **Feedback pianos** : notes actives en surbrillance

---

# 5. API REST backend

38+ endpoints organisés en 4 verbes HTTP (GET, POST, DELETE) + WebSocket.
Voir [api-rest.md](api-rest.md) pour la liste complète des endpoints.

Backend inchangé par le refactoring UI — toutes les routes API sont identiques.

---

# 6. UX / ergonomie

* **Épurée** : 3 onglets principaux, pas de menus imbriqués
* **Feedback temps réel** : piano virtuel + indicateurs actionneurs + monitoring cartes
* **Guidage** : page Bienvenue automatique au premier démarrage
* **Modularité** : ajout d'instruments / actionneurs via UI, sans recompiler
* **Responsiveness** : interface adaptative mobile / tablette / desktop
* **Couleurs et codes visuels** : bus actifs, actionneurs en marche, alertes sécurité
* **Thème sombre** : confort visuel, palette GitHub-style cohérente
