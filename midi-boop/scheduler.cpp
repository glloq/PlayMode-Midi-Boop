#include "scheduler.h"
#include "safety_manager.h"

// ============================================================================
// PlayMode Midi B∞p — Real-Time Scheduler (implémentation)
// ============================================================================

// Queue globale accessible par l'actuator_engine pour planifier des retours
QueueHandle_t g_scheduler_queue = NULL;

Scheduler::Scheduler(ActuatorEngine& engine)
    : _engine(engine),
      _safety_manager(nullptr),
      _task_handle(NULL),
      _input_queue(NULL),
      _running(false),
      _processed_count(0),
      _actuator_count(0),
      _event_count(0) {
    memset(_actuators, 0, sizeof(_actuators));
    memset(_event_buffer, 0, sizeof(_event_buffer));
}

bool Scheduler::begin() {
    // Créer la queue FreeRTOS pour les événements entrants
    _input_queue = xQueueCreate(SCHEDULER_QUEUE_SIZE, sizeof(SchedulerEvent));
    if (_input_queue == NULL) {
        Serial.println("[SCHED] Erreur création queue");
        return false;
    }

    // Exposer la queue globalement pour l'actuator_engine
    g_scheduler_queue = _input_queue;

    // Créer la tâche sur Core 1
    BaseType_t result = xTaskCreatePinnedToCore(
        schedulerTask,          // Fonction
        "Scheduler",            // Nom
        SCHEDULER_STACK_SIZE,   // Stack
        this,                   // Paramètre (pointeur vers instance)
        SCHEDULER_PRIORITY,     // Priorité
        &_task_handle,          // Handle
        SCHEDULER_CORE          // Core 1
    );

    if (result != pdPASS) {
        Serial.println("[SCHED] Erreur création tâche Core 1");
        return false;
    }

    _running = true;
    Serial.printf("[SCHED] Démarré sur Core %d (priorité %d, stack %d)\n",
                  SCHEDULER_CORE, SCHEDULER_PRIORITY, SCHEDULER_STACK_SIZE);
    return true;
}

void Scheduler::stop() {
    _running = false;
    if (_task_handle != NULL) {
        vTaskDelete(_task_handle);
        _task_handle = NULL;
    }
    if (_input_queue != NULL) {
        vQueueDelete(_input_queue);
        _input_queue = NULL;
        g_scheduler_queue = NULL;
    }
    Serial.println("[SCHED] Arrêté");
}

bool Scheduler::pushEvent(const SchedulerEvent& event) {
    if (_input_queue == NULL) return false;
    // Envoi non-bloquant dans la queue
    return xQueueSend(_input_queue, &event, 0) == pdTRUE;
}

void Scheduler::registerActuator(ActuatorConfig* actuator) {
    if (_actuator_count < MAX_ACTUATORS && actuator != nullptr) {
        _actuators[_actuator_count] = actuator;
        _actuator_count++;
        Serial.printf("[SCHED] Actionneur %d enregistré (%s)\n",
                      actuator->id,
                      actuator->type == ACT_SERVO ? "servo" : "solénoïde");
    }
}

uint16_t Scheduler::getQueuedEventCount() const {
    return _event_count;
}

uint32_t Scheduler::getProcessedCount() const {
    return _processed_count;
}

QueueHandle_t Scheduler::getQueueHandle() const {
    return _input_queue;
}

bool Scheduler::isRunning() const {
    return _running;
}

void Scheduler::setSafetyManager(SafetyManager* safety) {
    _safety_manager = safety;
    Serial.printf("[SCHED] Safety Manager %s\n", safety ? "enregistré" : "désactivé");
}

// ============================================================================
// Tâche FreeRTOS — Point d'entrée statique
// ============================================================================
void Scheduler::schedulerTask(void* param) {
    Scheduler* self = (Scheduler*)param;
    self->run();
}

// ============================================================================
// Boucle principale du scheduler
// ============================================================================
void Scheduler::run() {
    Serial.println("[SCHED] Boucle principale démarrée");

    while (_running) {
        // 1. Drainer les événements de la queue FreeRTOS
        drainInputQueue();

        // 2. Traiter les événements dont le timestamp est atteint
        processReadyEvents();

        // 3. Mise à jour périodique de la sécurité
        if (_safety_manager != nullptr) {
            _safety_manager->update(_actuators, _actuator_count);
        }

        // 4. Attendre le prochain tick
        vTaskDelay(SCHEDULER_TICK_MS / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

// ============================================================================
// Transfert queue FreeRTOS → priority queue interne
// ============================================================================
void Scheduler::drainInputQueue() {
    SchedulerEvent event;

    // Lire tous les événements disponibles dans la queue (non-bloquant)
    while (xQueueReceive(_input_queue, &event, 0) == pdTRUE) {
        insertEvent(event);
    }
}

// ============================================================================
// Insertion triée dans la priority queue (tri par timestamp croissant)
// ============================================================================
void Scheduler::insertEvent(const SchedulerEvent& event) {
    if (_event_count >= SCHEDULER_MAX_EVENTS) {
        Serial.println("[SCHED] ATTENTION : priority queue pleine, événement ignoré");
        return;
    }

    // Trouver la position d'insertion (tri croissant par timestamp)
    uint16_t pos = _event_count;
    for (uint16_t i = 0; i < _event_count; i++) {
        if (event.trigger_time_us < _event_buffer[i].trigger_time_us ||
            (event.trigger_time_us == _event_buffer[i].trigger_time_us &&
             event.priority < _event_buffer[i].priority)) {
            pos = i;
            break;
        }
    }

    // Décaler les éléments pour faire de la place
    for (uint16_t i = _event_count; i > pos; i--) {
        _event_buffer[i] = _event_buffer[i - 1];
    }

    // Insérer l'événement
    _event_buffer[pos] = event;
    _event_count++;
}

// ============================================================================
// Traitement des événements prêts
// ============================================================================
void Scheduler::processReadyEvents() {
    if (_event_count == 0) return;

    uint32_t now_us = (uint32_t)esp_timer_get_time();

    // Traiter tous les événements dont le timestamp est passé
    while (_event_count > 0 && _event_buffer[0].trigger_time_us <= now_us) {
        SchedulerEvent& event = _event_buffer[0];

        // Trouver l'actionneur cible
        ActuatorConfig* actuator = findActuator(event.actuator_id);
        if (actuator != nullptr) {
            // Vérification sécurité avant exécution
            bool safe = true;
            if (_safety_manager != nullptr) {
                safe = _safety_manager->checkEvent(*actuator, event);
            }

            if (safe) {
                _engine.processEvent(*actuator, event);
                _processed_count++;
            }
            // Événement bloqué par sécurité : silencieusement ignoré
        } else {
            Serial.printf("[SCHED] Actionneur %d non trouvé\n", event.actuator_id);
        }

        // Supprimer l'événement traité (décalage)
        for (uint16_t i = 0; i < _event_count - 1; i++) {
            _event_buffer[i] = _event_buffer[i + 1];
        }
        _event_count--;
    }
}

// ============================================================================
// Recherche d'actionneur par ID
// ============================================================================
ActuatorConfig* Scheduler::findActuator(uint8_t id) {
    for (uint8_t i = 0; i < _actuator_count; i++) {
        if (_actuators[i] != nullptr && _actuators[i]->id == id) {
            return _actuators[i];
        }
    }
    return nullptr;
}
