// Arduino IDE compatibility — wrapper de compilation pour tools/calibrator.cpp
// Arduino IDE ne compile pas les .cpp des sous-dossiers du sketch;
// ce fichier à la racine importe l'implémentation réelle.
// PlatformIO ignore ce fichier (il compile directement tools/calibrator.cpp).
#ifndef PLATFORMIO
#include "tools/calibrator.cpp"
#endif
