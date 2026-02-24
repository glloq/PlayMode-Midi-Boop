#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "config.h"
#include "types.h"
#include "actuator_engine.h"

// Forward declaration
class SafetyManager;

// ============================================================================
// PlayMode Midi B∞p — Real-Time Scheduler (Core 1)
// ============================================================================

class Scheduler {
public:
    Scheduler(ActuatorEngine& engine);

    // Démarre la tâche scheduler sur Core 1
    bool begin();

    // Arrête le scheduler
    void stop();

    // Ajoute un événement dans la queue (thread-safe, appelé depuis Core 0)
    bool pushEvent(const SchedulerEvent& event);

    // Enregistre un actionneur dans le scheduler
    void registerActuator(ActuatorConfig* actuator);

    // Nombre d'événements en attente dans la priority queue
    uint16_t getQueuedEventCount() const;

    // Nombre d'événements traités depuis le démarrage
    uint32_t getProcessedCount() const;

    // Retourne le handle de la queue FreeRTOS (pour usage externe)
    QueueHandle_t getQueueHandle() const;

    // Vérifie si le scheduler tourne
    bool isRunning() const;

    // Enregistre le safety manager pour pré-vérification
    void setSafetyManager(SafetyManager* safety);

private:
    ActuatorEngine& _engine;
    SafetyManager* _safety_manager;  // Pointeur safety (peut être null)
    TaskHandle_t _task_handle;
    QueueHandle_t _input_queue;     // Queue FreeRTOS pour événements entrants
    bool _running;
    uint32_t _processed_count;

    // Tableau des actionneurs enregistrés
    ActuatorConfig* _actuators[MAX_ACTUATORS];
    uint8_t _actuator_count;

    // Priority queue interne (triée par timestamp)
    SchedulerEvent _event_buffer[SCHEDULER_MAX_EVENTS];
    uint16_t _event_count;

    // Tâche FreeRTOS statique (point d'entrée)
    static void schedulerTask(void* param);

    // Boucle principale du scheduler
    void run();

    // Transfère les événements de la queue FreeRTOS vers la priority queue
    void drainInputQueue();

    // Insère un événement dans la priority queue (tri par timestamp)
    void insertEvent(const SchedulerEvent& event);

    // Traite les événements dont le timestamp est atteint
    void processReadyEvents();

    // Retrouve l'actuator config par ID
    ActuatorConfig* findActuator(uint8_t id);
};

#endif // SCHEDULER_H
