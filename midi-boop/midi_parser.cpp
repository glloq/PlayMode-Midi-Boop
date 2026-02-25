// Arduino IDE compatibility — wrapper de compilation pour midi/midi_parser.cpp
// Arduino IDE ne compile pas les .cpp des sous-dossiers du sketch;
// ce fichier à la racine importe l'implémentation réelle.
// PlatformIO ignore ce fichier (il compile directement midi/midi_parser.cpp).
#ifndef PLATFORMIO
#include "midi/midi_parser.cpp"
#endif
