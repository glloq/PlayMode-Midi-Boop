# Etude des Evolutions / Améliorations MIDI — PlayMode Midi B∞p

## Contexte

PlayMode Midi B∞p est un contrôleur MIDI no-code pour ESP32 qui pilote des servos et solénoïdes pour jouer de vrais instruments. Le système MIDI actuel supporte 3 transports (Serial, UDP, RTP-MIDI), gère Note On/Off et Control Change, avec jitter buffer, compensation de latence, courbes de vélocité et gestion d'énergie. Plusieurs types de messages MIDI sont **parsés mais ignorés** (Program Change, Pitch Bend, Aftertouch). Cette étude analyse le code existant en profondeur, puis identifie et priorise les évolutions possibles.

---

## Analyse du code MIDI existant

### Architecture du pipeline MIDI

Le flux MIDI suit une chaîne linéaire à 5 étages, répartie sur les 2 cores de l'ESP32 :

```
[Entrées MIDI]                    [Core 0 - loop()]                              [Core 1]
Serial (31250 baud, GPIO 4) ─┐
UDP (port 5004)              ├─► MidiTransport::poll() ─► MidiParser::feed()
RTP-MIDI (AppleMIDI v3)     ─┘          │
                                         ▼
                               JitterBuffer::insert()
                                         │  (rétention 10-100ms)
                                         ▼
                               JitterBuffer::pop()
                                         │
                                         ▼
                               MidiDispatcher::dispatch()
                                  ├─ handleNoteOn()    ─► vélocité curve ─► latency comp ─► PowerManager check
                                  ├─ handleNoteOff()
                                  ├─ handleControlChange() ─► CC mapping ─► cibles actuateur
                                  └─ default: break (★ Program Change, Pitch Bend, Aftertouch ignorés)
                                         │
                                         ▼
                               Scheduler::pushEvent()  ─────────────────────► Scheduler (Core 1, prio 5)
                                                                                    │
                                                                                    ▼
                                                                            ActuatorEngine::execute()
                                                                                    │
                                                                                    ▼
                                                                            PCA9685 I²C ─► Servos/Solénoïdes
```

### Analyse détaillée par module

#### 1. MidiParser (`midi_parser.h/.cpp` — 144 lignes)

**Points forts :**
- Machine à états propre et minimale : `handleStatusByte()` + `handleDataByte()`
- Support complet du Running Status MIDI (convention standard)
- Détection et isolation correcte des SysEx (0xF0..0xF7)
- Calcul correct de `dataLengthForStatus()` pour les 7 types de messages canal

**Points faibles identifiés :**
- **Messages système temps réel (0xF8-0xFF) intégralement ignorés** (`midi_parser.cpp:29-32`). Cela inclut le MIDI Clock (0xF8), Start (0xFA), Continue (0xFB), Stop (0xFC) — tous potentiellement utiles
- **Messages système communs (0xF1-0xF6) ignorés** (`midi_parser.cpp:71-77`). Le MTC Quarter Frame (0xF1) et Song Position Pointer (0xF2) pourraient être utiles pour la synchronisation
- **SysEx entièrement rejeté** (`midi_parser.cpp:58-68`). Aucun buffer SysEx, même minimal
- Le parser n'a aucun mécanisme de callback/notification — il fonctionne en polling pur

#### 2. MidiTransport (`midi_transport.h/.cpp` — 236 lignes)

**Points forts :**
- Architecture propre avec 3 transports indépendants, chacun avec son propre parser
- Statistiques par transport (bytes Serial, paquets UDP, paquets RTP)
- Callbacks AppleMIDI v3 correctement branchés (`setHandleNoteOn/Off/CC`)
- Méthode `deliverMessage()` centralisée pour tous les transports

**Points faibles identifiés :**
- **RTP-MIDI ne gère que 3 types de messages** (`midi_transport.cpp:172-174`). Seuls `setHandleNoteOn`, `setHandleNoteOff` et `setHandleControlChange` sont enregistrés. Les autres types (Program Change, Pitch Bend, Aftertouch) sont perdus côté RTP même s'ils sont parsés côté Serial/UDP
- **Aucune détection de déconnexion** pour Serial et UDP. Seul RTP a des callbacks `onConnected`/`onDisconnected`
- **Aucun mécanisme de sortie MIDI** — le transport est 100% input
- **Buffer UDP fixe de 256 octets** (`midi_transport.cpp:204`) — suffisant pour le MIDI standard mais non configurable
- L'overflow du jitter buffer est signalé par un simple `Serial.println` (`midi_transport.cpp:229`) sans compteur ni escalade

#### 3. JitterBuffer (`jitter_buffer.h/.cpp` — 67 lignes)

**Points forts :**
- Implémentation FIFO circulaire simple et correcte
- Pas d'allocation dynamique (tableau fixe de 64 entrées)
- Profondeur configurable à chaud via `setDepth()`

**Points faibles identifiés :**
- **Perte silencieuse quand plein** (`jitter_buffer.cpp:27-29`). Le message le plus récent est perdu sans distinction de priorité (un CC est traité comme un Note On)
- **Pas de mécanisme de priorité** — tous les messages sont égaux dans le buffer
- **Taille fixe de 64** — peut être insuffisant sous forte charge MIDI (ex: 3 transports actifs simultanément avec CC continus)
- **Pas de statistiques internes** (messages insérés, perdus, latence moyenne)
- Le calcul d'âge `now_us - timestamp_us` est sujet au wraparound du timer ESP32 (tous les ~71 minutes sur uint32_t)

#### 4. MidiDispatcher (`midi_dispatcher.h/.cpp` — 402 lignes)

**Points forts :**
- Table de lookup rapide O(1) : `_channel_to_instrument[16]` pour le routage canal→instrument
- Cache de pointeurs vers les routing configs (`_routing_cache[]`) pour éviter des recherches répétées
- Compensation de latence par actuateur avec arithmétique signée pour éviter l'underflow (`midi_dispatcher.cpp:117-122`)
- Courbes de vélocité avec interpolation linéaire et gestion des edge cases
- Intégration propre avec le PowerManager (vérification avant activation)
- Ring buffer WebSocket pour le monitoring en temps réel
- Convention MIDI respectée : Note On velocity=0 traité comme Note Off (`midi_dispatcher.cpp:29-30`)

**Points faibles identifiés :**
- **4 types de messages MIDI sur 7 sont ignorés** dans `dispatch()` (`midi_dispatcher.cpp:44`). Le switch ne gère que `MIDI_NOTE_ON`, `MIDI_NOTE_OFF` et `MIDI_CONTROL_CHANGE`
- **Recherche linéaire pour les actuateurs** : `findActuatorForNote()` (`midi_dispatcher.cpp:314-321`) itère sur tous les actuateurs de l'instrument. Avec 64 actuateurs par instrument, c'est O(n) à chaque note
- **Recherche linéaire par ID** : `findActuatorConfig()` (`midi_dispatcher.cpp:326-336`) itère sur tous les actuateurs configurés pour trouver celui avec l'ID correspondant. O(n) avec n=128 max
- **Pas de validation du canal** : aucun check `msg.channel < 16` dans `dispatch()`. Si un parser corrompait le champ, accès out-of-bounds sur `_channel_to_instrument`
- **4 cibles CC seulement** (`types.h:141-146`). Les paramètres `pulse_ms`, `angle_b`, `ramp_ms`, `pulse_min_ms` ne sont pas contrôlables par CC
- **Courbes de vélocité limitées à 5 points** (`config.h:100`). Insuffisant pour des courbes non-linéaires complexes
- **Pas de mode Learn** — toute la configuration passe par l'API REST/web UI
- **Pas de transposition** — aucun offset de note n'est appliquable

#### 5. Intégration globale (`play-mode.ino` — boucle principale)

**Points forts :**
- Boucle principale claire et séquentielle (lignes 325-431)
- Logging de status toutes les 5 secondes avec métriques complètes
- `delay(1)` minimal pour le yield (MIDI nécessite un polling fréquent)
- Détection de changements d'état (kill switch, WiFi, dégradation) avec logging

**Points faibles identifiés :**
- **Le jitter buffer est poppé dans une boucle `while`** (`play-mode.ino:337-339`). Si le buffer se remplit rapidement, cette boucle pourrait bloquer la boucle principale (WiFi, web server) pendant plusieurs millisecondes
- **Pas de limitation du nombre de dispatches par tick** — risque de starvation des autres tâches Core 0

### Synthèse des métriques de couverture MIDI

| Fonctionnalité MIDI | Standard | Implémenté | Statut |
|----------------------|----------|------------|--------|
| Note On (0x90) | Oui | Oui | Complet |
| Note Off (0x80) | Oui | Oui | Complet |
| Note On vel=0 → Off | Convention | Oui | Complet |
| Control Change (0xB0) | Oui | Oui | 4 cibles sur ~12 possibles |
| Program Change (0xC0) | Oui | Parsé uniquement | Non exploité |
| Pitch Bend (0xE0) | Oui | Parsé uniquement | Non exploité |
| Channel Pressure (0xD0) | Oui | Parsé uniquement | Non exploité |
| Poly Aftertouch (0xA0) | Oui | Parsé uniquement | Non exploité |
| Running Status | Convention | Oui | Complet |
| SysEx (0xF0-0xF7) | Oui | Détecté, ignoré | Non exploité |
| MIDI Clock (0xF8) | Oui | Ignoré | Non implémenté |
| Start/Stop/Continue | Oui | Ignoré | Non implémenté |
| MTC Quarter Frame (0xF1) | Oui | Ignoré | Non implémenté |
| Song Position (0xF2) | Oui | Ignoré | Non implémenté |
| Active Sensing (0xFE) | Optionnel | Ignoré | N/A |
| MIDI Thru | Standard | Non | Pas de sortie |
| MIDI Output | Standard | Non | Pas de sortie |
| BLE MIDI | Extension | Non | Pas de transport BLE |
| USB MIDI | Extension | Non | Hardware incompatible |
| Vélocité → actuateur | Custom | Oui | 5 points max |
| CC → paramètre | Custom | Oui | 4 cibles |
| Latence compensation | Custom | Oui | Complet |
| Jitter buffer | Custom | Oui | 64 msg, pas de priorité |

---

## Catégorie 1 — Exploitation des messages MIDI déjà parsés mais non utilisés

Le `MidiParser` parse déjà correctement Program Change, Pitch Bend, Poly Aftertouch et Channel Pressure, mais le `MidiDispatcher::dispatch()` les ignore dans son `default: break` (ligne 44).

### 1.1 Program Change → Changement de preset/configuration
- **Description** : Utiliser les messages Program Change (0xC0) pour basculer entre des presets de configuration sauvegardés (mappings notes, CC, courbes de vélocité)
- **Valeur** : Permet de changer de configuration à la volée depuis un DAW ou un contrôleur MIDI, sans passer par l'interface web
- **Complexité** : Moyenne
- **Fichiers impactés** : `midi_dispatcher.h/.cpp` (nouveau `handleProgramChange()`), `config_manager.h/.cpp` (gestion des presets), `types.h` (struct `PresetConfig`), `web_server.cpp` (API presets), `web_ui.h` (UI presets)
- **Priorité** : **Haute** — rapport valeur/effort très favorable

### 1.2 Pitch Bend → Contrôle continu de position servo
- **Description** : Utiliser le Pitch Bend (0xE0, 14 bits, -8192 à +8191) pour un contrôle continu haute résolution de la position des servos, ou comme modulation de l'amplitude de frappe
- **Valeur** : Permet des effets expressifs (vibrato mécanique, trémolo, glissando sur instruments à lames)
- **Complexité** : Moyenne — nécessite combiner data1 et data2 en valeur 14 bits
- **Fichiers impactés** : `midi_dispatcher.h/.cpp` (nouveau `handlePitchBend()`), `types.h` (nouvelle cible `PitchBendTarget`)
- **Priorité** : **Moyenne** — utile pour des cas d'usage expressifs

### 1.3 Channel Pressure (Aftertouch) → Modulation de paramètres en temps réel
- **Description** : Utiliser le Channel Pressure (0xD0) pour moduler dynamiquement un paramètre (amplitude, PWM hold, vitesse) pendant qu'une note est tenue
- **Valeur** : Contrôle expressif continu pour les comportements soutenus (SERVO_TOUCHE, SOL_HIT_AND_HOLD)
- **Complexité** : Faible — même logique que CC mapping, 1 seul octet de données
- **Fichiers impactés** : `midi_dispatcher.h/.cpp` (nouveau `handleChannelPressure()`)
- **Priorité** : **Moyenne** — complémentaire aux comportements soutenus

### 1.4 Poly Aftertouch → Modulation par note individuelle
- **Description** : Utiliser le Poly Aftertouch (0xA0) pour moduler un paramètre spécifique à un actuateur (via le mapping note→actuateur)
- **Valeur** : Contrôle individuel par note (ex: varier la pression sur chaque touche d'un piano mécanique)
- **Complexité** : Moyenne
- **Fichiers impactés** : `midi_dispatcher.h/.cpp`
- **Priorité** : **Basse** — cas d'usage de niche

---

## Catégorie 2 — Nouveaux transports MIDI

### 2.1 BLE MIDI (Bluetooth Low Energy MIDI)
- **Description** : Ajouter un transport MIDI via Bluetooth Low Energy, standard supporté nativement par iOS, macOS, Android et Windows 10+
- **Valeur** : Connexion sans fil sans nécessiter de réseau WiFi, idéal pour les performances live
- **Complexité** : Haute — nécessite BLE stack ESP32 (BLEDevice, BLEServer), service MIDI BLE UUID standard, gestion connexion/déconnexion, parsing des paquets BLE-MIDI (timestamps inclus)
- **Fichiers impactés** : nouveau `ble_midi_transport.h/.cpp`, `midi_transport.h/.cpp` (ajout source BLE), `midi_types.h` (nouveau `MIDI_SOURCE_BLE`), `config.h` (constantes BLE), `midi_types.h` (`MidiInputConfig` + `ble_enabled`)
- **Contraintes ESP32** : BLE et WiFi partagent la radio — coexistence possible mais avec impact sur les performances WiFi. RAM supplémentaire ~30KB pour le stack BLE
- **Priorité** : **Haute** — demande fréquente pour les contrôleurs MIDI embarqués

### 2.2 USB MIDI (Device)
- **Description** : Exposer l'ESP32 comme un périphérique USB MIDI via le port USB natif (ESP32-S2/S3 uniquement, pas ESP32-WROOM)
- **Valeur** : Connexion directe DAW↔ESP32 sans interface MIDI externe
- **Complexité** : Haute — nécessite ESP32-S2/S3, TinyUSB stack
- **Priorité** : **Basse** — incompatible avec le hardware actuel (ESP32-WROOM-32D)

---

## Catégorie 3 — Traitement et transformation MIDI

### 3.1 MIDI Learn (auto-mapping)
- **Description** : Mode d'apprentissage où l'utilisateur joue une note/CC sur son contrôleur et l'app l'associe automatiquement à un actuateur sélectionné dans l'UI
- **Valeur** : Simplifie drastiquement la configuration — plus besoin de connaître les numéros MIDI
- **Complexité** : Moyenne — nécessite un mode "learn" dans le dispatcher qui capture le prochain message reçu et l'associe
- **Fichiers impactés** : `midi_dispatcher.h/.cpp` (mode learn, callback), `web_server.cpp` (endpoint learn), `web_ui.h` (boutons Learn dans l'UI actuateurs/instruments)
- **Priorité** : **Haute** — amélioration majeure de l'expérience utilisateur

### 3.2 Transposition de notes
- **Description** : Décalage de notes configurable par instrument (+/- N demi-tons)
- **Valeur** : Adapter un mapping existant à un autre octave sans reconfigurer
- **Complexité** : Faible — un `int8_t transpose` par instrument, appliqué dans `handleNoteOn/Off()` avant le lookup
- **Fichiers impactés** : `types.h` (`InstrumentConfig` + champ `transpose`), `midi_dispatcher.cpp` (2 lignes), `config_manager.cpp` (sérialisation), `web_ui.h`
- **Priorité** : **Haute** — très simple, très utile

### 3.3 Filtrage de messages MIDI par canal/type
- **Description** : Possibilité de filtrer (accepter/rejeter) certains types de messages par canal MIDI
- **Valeur** : Ignorer les messages indésirables (clock, aftertouch non voulu) pour réduire la charge
- **Complexité** : Faible — bitmask par canal dans le dispatcher
- **Fichiers impactés** : `midi_dispatcher.h/.cpp`, `types.h` (config filtre), `web_ui.h`
- **Priorité** : **Moyenne**

### 3.4 Remapping de canaux MIDI
- **Description** : Table de remapping canal entrant → canal interne (ex: canal 10 → canal 1)
- **Valeur** : Flexibilité pour travailler avec des configurations MIDI existantes
- **Complexité** : Faible — LUT de 16 entrées, appliquée avant le dispatch
- **Fichiers impactés** : `midi_dispatcher.h/.cpp`, `types.h`, `config_manager.cpp`
- **Priorité** : **Basse** — cas d'usage limité

### 3.5 Zones de clavier / Splits
- **Description** : Diviser une plage de notes en zones, chaque zone routée vers un instrument différent
- **Valeur** : Jouer plusieurs instruments depuis un seul clavier MIDI
- **Complexité** : Moyenne — modifier le routing pour ajouter des plages note_min/note_max par zone
- **Fichiers impactés** : `midi_dispatcher.h/.cpp`, `types.h` (struct `KeyZone`), `config_manager.cpp`, `web_ui.h`
- **Priorité** : **Moyenne** — utile pour les configurations multi-instruments

---

## Catégorie 4 — Synchronisation et tempo

### 4.1 MIDI Clock reception (Timing Clock 0xF8)
- **Description** : Recevoir et exploiter les messages MIDI Clock (24 PPQN) pour synchroniser le tempo. Actuellement ignorés dans `midi_parser.cpp` ligne 29-32
- **Valeur** : Synchronisation avec un DAW pour des effets rythmiques, quantisation de notes, ou séquences internes
- **Complexité** : Moyenne — nécessite un compteur PPQN, calcul BPM, et exposition du tempo courant
- **Fichiers impactés** : `midi_parser.cpp` (ne plus ignorer 0xF8), `midi_transport.cpp`, nouveau module `midi_clock.h/.cpp`, `web_ui.h` (affichage BPM)
- **Priorité** : **Moyenne** — prérequis pour la quantisation et le séquenceur interne

### 4.2 Quantisation de notes
- **Description** : Aligner automatiquement les notes reçues sur la grille rythmique la plus proche (1/4, 1/8, 1/16)
- **Valeur** : Correction rythmique en temps réel, compensation du jeu humain imprécis
- **Complexité** : Haute — nécessite MIDI Clock, calcul de position dans la mesure, ajout de délai aux événements
- **Dépendances** : Catégorie 4.1 (MIDI Clock)
- **Priorité** : **Basse** — fonctionnalité avancée

### 4.3 Start/Stop/Continue (0xFA, 0xFC, 0xFB)
- **Description** : Réagir aux messages MIDI Transport (Start, Stop, Continue) pour déclencher/arrêter des séquences ou patterns internes
- **Valeur** : Intégration DAW complète pour les modes séquencés
- **Complexité** : Faible (réception) à Haute (si séquenceur interne)
- **Dépendances** : Catégorie 4.1
- **Priorité** : **Basse**

---

## Catégorie 5 — MIDI Output

### 5.1 MIDI Thru (forwarding)
- **Description** : Retransmettre les messages MIDI reçus sur un ou plusieurs transports de sortie (Serial TX, UDP, RTP)
- **Valeur** : Chaînage de plusieurs modules PlayMode, intégration dans un setup MIDI complexe
- **Complexité** : Moyenne — nécessite Serial TX (ajout GPIO), UDP send, et/ou RTP send
- **Fichiers impactés** : `midi_transport.h/.cpp` (fonctions send), `config.h` (GPIO TX), `midi_types.h` (`MidiOutputConfig`)
- **Priorité** : **Moyenne** — essentiel pour les setups multi-modules

### 5.2 MIDI Feedback (état des actuateurs)
- **Description** : Envoyer des messages MIDI (Note On/Off, CC) reflétant l'état réel des actuateurs vers le contrôleur source
- **Valeur** : LEDs de feedback sur les contrôleurs MIDI (Novation Launchpad, etc.), monitoring bidirectionnel
- **Complexité** : Haute — nécessite tracking de l'état actuateur + envoi MIDI depuis Core 0
- **Dépendances** : Catégorie 5.1 (infrastructure de sortie)
- **Priorité** : **Basse**

---

## Catégorie 6 — Robustesse et fiabilité

### 6.1 Détection de déconnexion Serial MIDI
- **Description** : Détecter l'absence de données Serial sur une période configurable et signaler la perte de connexion
- **Valeur** : Feedback utilisateur, possibilité de couper les actuateurs actifs en cas de perte
- **Complexité** : Faible — watchdog sur le dernier octet reçu
- **Fichiers impactés** : `midi_transport.h/.cpp` (ajout `_last_serial_byte_us`)
- **Priorité** : **Haute** — amélioration de sécurité importante

### 6.2 Gestion améliorée du jitter buffer overflow
- **Description** : Au lieu de perdre silencieusement les messages quand le buffer est plein (ligne 228-229 de `midi_transport.cpp`), implémenter une stratégie intelligente : priorité aux Note On/Off vs CC, compteur exposé dans l'UI, alarme
- **Valeur** : Moins de notes perdues en situation de charge, meilleur diagnostic
- **Complexité** : Faible
- **Fichiers impactés** : `jitter_buffer.h/.cpp` (politique de drop), `midi_transport.cpp`, `web_ui.h` (compteur de drops)
- **Priorité** : **Moyenne**

### 6.3 Heartbeat UDP / Keep-alive
- **Description** : Mécanisme de ping/pong pour détecter la perte de connectivité UDP
- **Valeur** : Détection rapide de la perte de connexion réseau
- **Complexité** : Faible
- **Fichiers impactés** : `midi_transport.h/.cpp`
- **Priorité** : **Basse**

### 6.4 Statistiques MIDI avancées
- **Description** : Enrichir les métriques MIDI : latence moyenne par transport, débit de messages/sec, histogramme de vélocité, distribution par canal
- **Valeur** : Diagnostic avancé, optimisation des performances
- **Complexité** : Faible
- **Fichiers impactés** : `midi_transport.h/.cpp`, `midi_dispatcher.h/.cpp`, `web_server.cpp`, `web_ui.h`
- **Priorité** : **Moyenne**

---

## Catégorie 7 — Configuration avancée

### 7.1 SysEx pour transfert de configuration
- **Description** : Utiliser les messages SysEx (actuellement ignorés dans `midi_parser.cpp` ligne 57-68) pour importer/exporter la configuration complète via MIDI
- **Valeur** : Backup/restore sans WiFi, partage de configurations entre modules
- **Complexité** : Haute — nécessite buffer SysEx (potentiellement gros), parsing custom, manufacturer ID
- **Fichiers impactés** : `midi_parser.h/.cpp` (ne plus ignorer SysEx), `midi_transport.cpp`, nouveau module `sysex_handler.h/.cpp`, `config_manager.h/.cpp`
- **Contraintes** : RAM limitée sur ESP32, les messages SysEx peuvent être très longs
- **Priorité** : **Basse** — la configuration via web UI est plus pratique

### 7.2 Courbes de vélocité avancées (presets + plus de points)
- **Description** : Augmenter `VELOCITY_CURVE_POINTS` de 5 à 10, ajouter des presets mathématiques (linéaire, exponentielle, logarithmique, S-curve, hard/soft)
- **Valeur** : Meilleur contrôle de la dynamique sans configuration manuelle point par point
- **Complexité** : Faible — formules mathématiques simples, génération côté UI
- **Fichiers impactés** : `config.h` (`VELOCITY_CURVE_POINTS`), `types.h` (`MidiRoutingConfig`), `midi_dispatcher.cpp` (`applyVelocityCurve()`), `web_ui.h` (sélecteur de courbe)
- **Priorité** : **Haute** — très simple, grand impact UX

### 7.3 Nouvelles cibles CC
- **Description** : Ajouter des cibles CC supplémentaires : `CC_TARGET_PULSE_MS` (durée pulse solénoïde), `CC_TARGET_ANGLE_B` (angle B pour alternate), `CC_TARGET_RAMP_MS` (durée de rampe)
- **Valeur** : Contrôle en temps réel de tous les paramètres d'actuateurs via MIDI
- **Complexité** : Faible — extension directe du switch dans `handleControlChange()`
- **Fichiers impactés** : `types.h` (enum `CCTarget`), `midi_dispatcher.cpp` (3 nouveaux cases)
- **Priorité** : **Haute** — extension naturelle du système existant

---

## Catégorie 8 — Interface utilisateur MIDI

### 8.1 Visualisation MIDI améliorée
- **Description** : Améliorer le moniteur MIDI WebSocket : affichage en temps réel avec filtre par type/canal, code couleur, historique scrollable (au lieu de 16 messages max)
- **Valeur** : Meilleur diagnostic et compréhension du flux MIDI
- **Complexité** : Faible (côté UI principalement)
- **Fichiers impactés** : `web_ui.h` (section MIDI monitor), `midi_dispatcher.h/.cpp` (augmenter `MIDI_WS_LOG_SIZE`)
- **Priorité** : **Moyenne**

### 8.2 Piano virtuel multi-instrument
- **Description** : Afficher un piano virtuel par instrument configuré (actuellement un seul), avec indication visuelle des notes mappées vs non-mappées
- **Valeur** : Vue d'ensemble de la configuration MIDI, test rapide multi-instruments
- **Complexité** : Moyenne (UI)
- **Fichiers impactés** : `web_ui.h`
- **Priorité** : **Moyenne**

### 8.3 Éditeur de courbe de vélocité graphique
- **Description** : Interface graphique (canvas/SVG) pour dessiner la courbe de vélocité avec des points draggables, au lieu de la saisie manuelle de valeurs
- **Valeur** : Configuration intuitive et visuelle des courbes de vélocité
- **Complexité** : Moyenne (UI Canvas/SVG)
- **Fichiers impactés** : `web_ui.h`
- **Priorité** : **Moyenne**

---

## Feuille de route recommandée

### Phase A — Quick wins (complexité faible, impact élevé)
1. **3.2 Transposition de notes** — 1 champ `int8_t`, 2 lignes dans le dispatcher
2. **7.3 Nouvelles cibles CC** — 3 nouveaux cases dans le switch existant
3. **7.2 Courbes de vélocité avancées** — augmenter les points + presets mathématiques
4. **6.1 Détection déconnexion Serial** — watchdog simple

### Phase B — Fonctionnalités à forte valeur ajoutée (complexité moyenne)
5. **3.1 MIDI Learn** — mode apprentissage automatique
6. **1.1 Program Change → Presets** — changement de configuration à la volée
7. **1.2 Pitch Bend → Contrôle continu** — expression haute résolution
8. **1.3 Channel Pressure** — modulation de paramètres soutenus
9. **3.3 Filtrage MIDI** — bitmask par canal/type

### Phase C — Extensions structurantes (complexité haute)
10. **2.1 BLE MIDI** — nouveau transport Bluetooth
11. **4.1 MIDI Clock** — synchronisation tempo
12. **5.1 MIDI Thru** — sortie MIDI
13. **6.2 Jitter buffer intelligent** — gestion prioritaire des overflows

### Phase D — Fonctionnalités avancées (long terme)
14. **3.5 Zones de clavier** — splits multi-instruments
15. **5.2 MIDI Feedback** — retour d'état bidirectionnel
16. **7.1 SysEx config** — transfert de configuration
17. **4.2 Quantisation** — correction rythmique
18. **8.1-8.3 Améliorations UI** — visualisation et éditeurs graphiques

---

## Fichiers critiques à modifier

| Fichier | Rôle | Modifications fréquentes |
|---------|------|--------------------------|
| `play-mode/midi_dispatcher.h/.cpp` | Routage MIDI | Presque toutes les améliorations |
| `play-mode/types.h` | Structures de données | Nouvelles configs, enums, champs |
| `play-mode/midi_types.h` | Types MIDI | Nouveaux transports, configs |
| `play-mode/midi_transport.h/.cpp` | Transports d'entrée | BLE, détection déconnexion, output |
| `play-mode/midi_parser.h/.cpp` | Parseur MIDI | SysEx, Clock, messages système |
| `play-mode/config.h` | Constantes globales | Nouvelles limites, GPIO |
| `play-mode/config_manager.h/.cpp` | Persistance JSON | Presets, nouveaux champs |
| `play-mode/web_ui.h` | Interface web | Toutes les améliorations UI |
| `play-mode/web_server.h/.cpp` | API REST | Nouveaux endpoints |

---

## Vérification

Pour chaque amélioration implémentée :
1. **Compilation** : `pio run` doit compiler sans erreur ni warning
2. **Test fonctionnel** : Envoyer les messages MIDI concernés via le piano virtuel ou un contrôleur externe
3. **Test de régression** : Vérifier que Note On/Off et CC existants fonctionnent toujours
4. **Monitoring** : Vérifier les compteurs dans l'UI web (dispatched, dropped, power_rejected)
5. **Persistance** : Sauvegarder/recharger la config et vérifier que les nouveaux champs sont conservés
6. **RAM** : Vérifier `ESP.getFreeHeap()` pour s'assurer que la mémoire reste suffisante (>50KB libre)
