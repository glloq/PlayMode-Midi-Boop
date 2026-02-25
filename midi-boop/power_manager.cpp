// Arduino IDE compatibility — wrapper de compilation pour managers/power_manager.cpp
// Arduino IDE ne compile pas les .cpp des sous-dossiers du sketch;
// ce fichier à la racine importe l'implémentation réelle.
// PlatformIO ignore ce fichier (il compile directement managers/power_manager.cpp).
#ifndef PLATFORMIO
#include "managers/power_manager.cpp"
#endif
