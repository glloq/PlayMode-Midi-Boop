// Arduino IDE compatibility — wrapper de compilation pour tools/test_manager.cpp
// Arduino IDE ne compile pas les .cpp des sous-dossiers du sketch;
// ce fichier à la racine importe l'implémentation réelle.
// PlatformIO ignore ce fichier (il compile directement tools/test_manager.cpp).
#ifndef PLATFORMIO
#include "tools/test_manager.cpp"
#endif
