// Arduino IDE compatibility — wrapper de compilation pour scheduler/scheduler.cpp
// Arduino IDE ne compile pas les .cpp des sous-dossiers du sketch;
// ce fichier à la racine importe l'implémentation réelle.
// PlatformIO ignore ce fichier (il compile directement scheduler/scheduler.cpp).
#ifndef PLATFORMIO
#include "scheduler/scheduler.cpp"
#endif
