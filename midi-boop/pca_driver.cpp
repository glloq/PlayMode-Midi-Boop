// Arduino IDE compatibility — wrapper de compilation pour drivers/pca_driver.cpp
// Arduino IDE ne compile pas les .cpp des sous-dossiers du sketch;
// ce fichier à la racine importe l'implémentation réelle.
// PlatformIO ignore ce fichier (il compile directement drivers/pca_driver.cpp).
#ifndef PLATFORMIO
#include "drivers/pca_driver.cpp"
#endif
