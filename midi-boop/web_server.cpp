// Arduino IDE compatibility — wrapper de compilation pour web/web_server.cpp
// Arduino IDE ne compile pas les .cpp des sous-dossiers du sketch;
// ce fichier à la racine importe l'implémentation réelle.
// PlatformIO ignore ce fichier (il compile directement web/web_server.cpp).
#ifndef PLATFORMIO
#include "web/web_server.cpp"
#endif
