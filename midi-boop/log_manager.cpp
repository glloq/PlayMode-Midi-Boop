// Arduino IDE compatibility — wrapper de compilation pour log/log_manager.cpp
// Arduino IDE ne compile pas les .cpp des sous-dossiers du sketch;
// ce fichier à la racine importe l'implémentation réelle.
// PlatformIO ignore ce fichier (il compile directement log/log_manager.cpp).
#ifndef PLATFORMIO
#include "log/log_manager.cpp"
#endif
