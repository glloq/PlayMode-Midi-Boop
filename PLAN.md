# Plan de refactoring UI — Simplification pour débutants

## Statut : Implémenté

---

## Architecture actuelle : 3 onglets + Calibration (conditionnel) + Réglages

### Page "Bienvenue" (`page-welcome`, first-run, affichée si 0 instruments)
- Écran d'accueil avec le logo Midi B∞p centré dans le header
- 3 étapes illustrées : Créer → Connecter → Jouer
- Bouton "Créer mon premier instrument" → lance le wizard 4 étapes
- Lien secondaire vers la configuration manuelle
- Disparaît automatiquement dès qu'un instrument existe

### Page 1 : **Instrument** (`page-instrument`, onglet par défaut)
- Liste des instruments (nom, canal, type, nb actionneurs, état)
- Boutons Créer (wizard) / Manuel / Éditer / Supprimer
- **Piano(s) virtuels** : un clavier interactif par instrument
- Section dépliable "Actionneurs" inline
- Section dépliable "CC Mapping" inline

### Page 2 : **MIDI** (`page-midi`)
- Transports MIDI : Serial / UDP / RTP-MIDI (carte dédiée par transport avec toggle + status)
- Jitter buffer (slider 10–80 ms, défaut 30 ms)
- Messages MIDI reçus en temps réel (table avec pause/effacer)

### Page 3 : **Actionneurs** (`page-actuators`)
- Table complète de tous les actionneurs (ID, type, note, bus, PCA, canal, mode, état)
- CC Routing : table des Control Changes par instrument

### Page 4 : **Calibration** (`page-calibration`, onglet conditionnel, masqué par défaut)
- Calibration acoustique : micro I²S, progression, résultats
- Test actionneur : sweep, burst, stress, calibration individuelle/globale
- Onglet affiché uniquement quand pertinent (via `style="display:none"` sur le bouton nav `#nav-cal`)

### Réglages (`page-settings`, engrenage ⚙)
- **Monitoring** : 4 cartes (MIDI, Polyphonie avec barre + rejetés, Scheduler, WiFi)
- **Polyphonie & Sécurité** : polyphonie max, Kill Switch, dégradation, limites avancées dépliables
- **Journal système** : logs filtrables par niveau (DEBUG → CRITICAL) et catégorie (7 catégories incluant Test), auto-scroll
- **Connexion WiFi** : SSID, mot de passe, hostname, AP fallback
- **Bus I²C** (avancé, dépliable) : table des bus avec fréquence PWM configurable
- **Configuration** : sauvegarde flash, réinitialisation

---

## Changements implémentés

### Navigation
- 10+ pages → 3 onglets principaux + Calibration (conditionnel) + Réglages (engrenage)
- Page Welcome au premier démarrage (0 instruments)
- Wizard de création en 4 étapes
- Bouton "+ Manuel" pour création directe sans wizard

### Réglages (refonte)
- Fusion des sections Monitoring + Polyphonie + Sécurité
- Suppression doublons :
  - "Actifs" affiché 1 fois (était 3 : monitoring, polyphonie, sécurité)
  - "Rejetés" affiché 1 fois (était 2 : monitoring, polyphonie)
  - "Polyphonie max" : 1 seul input partagé entre polyphonie et sécurité
- 4 cartes Monitoring : MIDI, Polyphonie (avec barre + rejetés), Scheduler, WiFi

### Modals de confirmation/alerte thémés
- `appConfirm()` remplace tous les `confirm()` natifs
- `appAlert()` remplace tous les `alert()` natifs
- Design intégré au thème sombre avec icônes contextuelles
- Support danger/primary, textes de boutons personnalisables

### Header
- Logo B∞P centré horizontalement via positionnement absolu

### Ce qui est conservé intact
- Toutes les API REST (38+ endpoints, aucun changement backend)
- WebSocket et monitoring temps réel (200 ms broadcast)
- Wizard 4 étapes (instrument, type, notes, résumé)
- Modals d'édition (instrument, actionneur, CC)
- Piano virtuel interactif (un par instrument)
- Thème sombre GitHub-style (#0d1117 bg, #58a6ff accent)
- Design responsive (mobile/tablette/desktop)
- Toasts de notification
- Sections dépliables pour réglages avancés
