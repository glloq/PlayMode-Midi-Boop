#ifndef JITTER_BUFFER_H
#define JITTER_BUFFER_H

#include <Arduino.h>
#include "config.h"
#include "midi_types.h"

// ============================================================================
// PlayMode — Jitter Buffer MIDI (Phase 3)
// ============================================================================
//
// Buffer circulaire FIFO qui retient les messages MIDI pendant une durée
// configurable (0-100 ms) pour absorber la gigue réseau.
// Pas d'allocation dynamique — taille fixe JITTER_BUFFER_SIZE.
//

class JitterBuffer {
public:
    JitterBuffer();

    // Configure la profondeur du buffer en millisecondes
    void setDepth(uint16_t depth_ms);

    // Retourne la profondeur actuelle (ms)
    uint16_t getDepth() const;

    // Insère un message dans le buffer
    // Retourne false si le buffer est plein
    bool insert(const MidiMessage& msg);

    // Retire le message le plus ancien si sa durée de rétention est écoulée
    // Retourne true si un message a été extrait
    bool pop(MidiMessage& msg);

    // Nombre de messages dans le buffer
    uint8_t count() const;

    // Vide le buffer
    void clear();

private:
    MidiMessage _buffer[JITTER_BUFFER_SIZE];
    uint8_t _head;          // Index d'écriture (prochain insert)
    uint8_t _tail;          // Index de lecture (prochain pop)
    uint8_t _count;         // Nombre d'éléments dans le buffer
    uint16_t _depth_ms;     // Profondeur en millisecondes
};

#endif // JITTER_BUFFER_H
