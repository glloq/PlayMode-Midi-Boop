# Plan de refactoring UI — Simplification pour débutants

## Analyse de l'existant

L'interface actuelle comporte **10+ pages** avec beaucoup de sous-menus :
- Accueil (instruments + piano)
- Actionneurs (table séparée)
- CC Mapping (page dédiée)
- Calibration acoustique
- Test industriel (sweep/burst/stress)
- Dashboard, Power, Safety, Logs, Paramètres (gear menu)

**Problème** : Trop de pages, navigation complexe, un débutant ne sait pas par où commencer.

---

## Nouvelle architecture : 3 pages + Welcome + Settings

### Page "Bienvenue" (first-run, affichée si 0 instruments)
- Écran d'accueil chaleureux avec le logo Midi B∞p
- Explication en 3 étapes illustrées : Créer → Connecter → Jouer
- Bouton unique "Créer mon premier instrument" qui lance le wizard
- Disparaît une fois qu'un instrument existe (et ne réapparaît plus)

### Page 1 : **Instrument** (onglet par défaut)
- Liste des instruments (nom, canal, type, nb actionneurs, état)
- Boutons Créer (wizard) / Éditer / Supprimer
- **Piano virtuel** intégré en bas (sélecteur d'instrument)
- Section dépliable "Actionneurs" : table complète des actionneurs avec test inline
- Section dépliable "CC Mapping" : table des Control Changes

### Page 2 : **MIDI**
- Transports MIDI : Serial / UDP / RTP-MIDI (toggle + status)
- Jitter buffer (slider)
- Routage MIDI : table des notes mappées par instrument
- WiFi : SSID, password, hostname (car le MIDI réseau en dépend)

### Page 3 : **Calibration**
- **Tests actionneurs** : Test individuel (bouton par actionneur), Sweep, Burst
- **Calibration acoustique** : micro I²S, calibrer tous / un seul, résultats
- **Réglages servo** : angle preview, ajustement rapide
- **Réglages solénoïde** : durée impulsion, PWM

### Gear menu (inchangé mais réorganisé)
- Dashboard (monitoring temps réel)
- Power (budget énergie)
- Safety (limites sécurité + kill switch)
- Logs (journal système)
- Sauvegarde/Reset config

---

## Changements techniques

### Fichier `web_ui.h` (refonte complète du HTML/CSS/JS)

1. **Welcome page** : nouveau `<div id="page-welcome">` avec détection auto `instruments.length === 0`
2. **Nav simplifiée** : 3 boutons au lieu de 5 (`Instrument` / `MIDI` / `Calibration`)
3. **Page Instrument** : fusionne Home + Actuators + CC Mapping avec sections collapsibles
4. **Page MIDI** : fusionne transport MIDI + WiFi + routage (ex-Settings partiel)
5. **Page Calibration** : fusionne Calibration acoustique + Test industriel
6. **Gear menu** : conservé tel quel (Dashboard, Power, Safety, Logs + config save)
7. **Modals** : conservés (instrument, actuator, wizard) — aucun changement fonctionnel
8. **Init JS** : détection first-run, redirection auto vers welcome/wizard

### Fichier `web_server.cpp` — AUCUN changement
Toutes les API REST restent identiques. Seul le frontend change.

---

## Ce qui est conservé intact
- Toutes les API REST (pas de changement backend)
- WebSocket et monitoring temps réel
- Wizard de création d'instrument (4 étapes)
- Modals d'édition (instrument + actionneur)
- Piano virtuel interactif
- Gear menu avancé (Dashboard, Power, Safety, Logs)
- Design responsive (mobile/tablette/desktop)
- Dark theme GitHub-style

## Ce qui change
- Navigation : 5 onglets → 3 onglets
- Page d'accueil : welcome screen au premier démarrage
- Regroupement logique : les actionneurs et CC sont sous "Instrument"
- Tests et calibration : fusionnés en une seule page "Calibration"
- MIDI et WiFi : fusionnés car liés (MIDI réseau = WiFi)
- Bus I²C : déplacé dans gear menu (Paramètres avancés)
