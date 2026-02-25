// Arduino IDE compatibility — wrapper de compilation pour network/wifi_manager.cpp
// Arduino IDE ne compile pas les .cpp des sous-dossiers du sketch;
// ce fichier à la racine importe l'implémentation réelle.
// PlatformIO ignore ce fichier (il compile directement network/wifi_manager.cpp).
#ifndef PLATFORMIO
#include "network/wifi_manager.cpp"
#endif
