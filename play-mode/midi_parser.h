#ifndef MIDI_PARSER_H
#define MIDI_PARSER_H

#include <Arduino.h>
#include "midi_types.h"

// ============================================================================
// PlayMode — Parseur MIDI byte-par-byte (Phase 3)
// ============================================================================
//
// Parseur MIDI stateful qui consomme un byte à la fois.
// Supporte le running status et ignore les messages SysEx.
// Utilisé par chaque transport (Serial, UDP) indépendamment.
//

class MidiParser {
public:
    MidiParser();

    // Réinitialise l'état du parseur
    void reset();

    // Alimente le parseur avec un byte.
    // Retourne true quand un message complet est prêt.
    bool feed(uint8_t byte);

    // Récupère le dernier message parsé.
    // Valide uniquement après que feed() a retourné true.
    MidiMessage getMessage() const;

private:
    MidiMessage _message;        // Message en cours de construction
    uint8_t _running_status;     // Dernier status byte (running status)
    uint8_t _data_index;         // Index du data byte attendu (0 ou 1)
    uint8_t _expected_length;    // Nombre de data bytes attendus (1 ou 2)
    bool _in_sysex;              // En cours de réception SysEx (ignoré)
    bool _ready;                 // Message prêt à être lu

    // Détermine le nombre de data bytes pour un status byte
    uint8_t dataLengthForStatus(uint8_t status);

    // Traite un status byte
    void handleStatusByte(uint8_t byte);

    // Traite un data byte
    bool handleDataByte(uint8_t byte);
};

#endif // MIDI_PARSER_H
