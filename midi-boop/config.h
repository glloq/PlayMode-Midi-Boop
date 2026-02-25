// Arduino IDE compatibility — proxy vers config/config.h
// Ce fichier permet à #include "config.h" de fonctionner depuis n'importe
// quel sous-dossier du sketch, car Arduino IDE ajoute la racine du sketch
// au chemin d'inclusion pour tous les fichiers compilés.
#pragma once
#include "config/config.h"
