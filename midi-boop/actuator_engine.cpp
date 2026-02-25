// Arduino IDE compatibility — wrapper de compilation pour actuators/actuator_engine.cpp
// Arduino IDE ne compile pas les .cpp des sous-dossiers du sketch;
// ce fichier à la racine importe l'implémentation réelle.
// PlatformIO ignore ce fichier (il compile directement actuators/actuator_engine.cpp).
#ifndef PLATFORMIO
#include "actuators/actuator_engine.cpp"
#endif
