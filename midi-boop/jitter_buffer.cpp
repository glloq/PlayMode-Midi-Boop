// Arduino IDE compatibility — wrapper de compilation pour midi/jitter_buffer.cpp
// Arduino IDE ne compile pas les .cpp des sous-dossiers du sketch;
// ce fichier à la racine importe l'implémentation réelle.
// PlatformIO ignore ce fichier (il compile directement midi/jitter_buffer.cpp).
#ifndef PLATFORMIO
#include "midi/jitter_buffer.cpp"
#endif
