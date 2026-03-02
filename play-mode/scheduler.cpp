#include "scheduler.h"
#include "safety_manager.h"

// ============================================================================
// PlayMode — Real-Time Scheduler (implementation)
// ============================================================================

// Global queue accessible by actuator_engine for scheduling return events
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
    // Create the FreeRTOS queue for incoming events
    _input_queue = xQueueCreate(SCHEDULER_QUEUE_SIZE, sizeof(SchedulerEvent));
    if (_input_queue == NULL) {
        Serial.println("[SCHED] Error creating queue");
        return false;
    }

    // Expose the queue globally for actuator_engine
    g_scheduler_queue = _input_queue;

    // Create the task on Core 1
    BaseType_t result = xTaskCreatePinnedToCore(
        schedulerTask,          // Function
        "Scheduler",            // Name
        SCHEDULER_STACK_SIZE,   // Stack
        this,                   // Parameter (pointer to instance)
        SCHEDULER_PRIORITY,     // Priority
        &_task_handle,          // Handle
        SCHEDULER_CORE          // Core 1
    );

    if (result != pdPASS) {
        Serial.println("[SCHED] Error creating Core 1 task");
        return false;
    }

    _running = true;
    Serial.printf("[SCHED] Started on Core %d (priority %d, stack %d)\n",
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
    Serial.println("[SCHED] Stopped");
}

bool Scheduler::pushEvent(const SchedulerEvent& event) {
    if (_input_queue == NULL) return false;
    // Non-blocking send to the queue
    return xQueueSend(_input_queue, &event, 0) == pdTRUE;
}

void Scheduler::registerActuator(ActuatorConfig* actuator) {
    if (_actuator_count < MAX_ACTUATORS && actuator != nullptr) {
        _actuators[_actuator_count] = actuator;
        _actuator_count++;
        Serial.printf("[SCHED] Actuator %d registered (%s)\n",
                      actuator->id,
                      actuator->type == ACT_SERVO ? "servo" : "solenoid");
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
    Serial.printf("[SCHED] Safety Manager %s\n", safety ? "registered" : "disabled");
}

// ============================================================================
// FreeRTOS Task — Static entry point
// ============================================================================
void Scheduler::schedulerTask(void* param) {
    Scheduler* self = (Scheduler*)param;
    self->run();
}

// ============================================================================
// Main scheduler loop
// ============================================================================
void Scheduler::run() {
    Serial.println("[SCHED] Main loop started");

    while (_running) {
        // 1. Drain events from the FreeRTOS queue
        drainInputQueue();

        // 2. Process events whose timestamp has been reached
        processReadyEvents();

        // 3. Periodic safety update
        if (_safety_manager != nullptr) {
            _safety_manager->update(_actuators, _actuator_count);
        }

        // 4. Wait for the next tick
        vTaskDelay(SCHEDULER_TICK_MS / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

// ============================================================================
// Transfer FreeRTOS queue -> internal priority queue
// ============================================================================
void Scheduler::drainInputQueue() {
    SchedulerEvent event;

    // Read all available events from the queue (non-blocking)
    while (xQueueReceive(_input_queue, &event, 0) == pdTRUE) {
        insertEvent(event);
    }
}

// ============================================================================
// Sorted insertion into the priority queue (ascending timestamp order)
// ============================================================================
void Scheduler::insertEvent(const SchedulerEvent& event) {
    if (_event_count >= SCHEDULER_MAX_EVENTS) {
        Serial.println("[SCHED] WARNING: priority queue full, event dropped");
        return;
    }

    // Find insertion position (ascending order by timestamp)
    uint16_t pos = _event_count;
    for (uint16_t i = 0; i < _event_count; i++) {
        if (event.trigger_time_us < _event_buffer[i].trigger_time_us ||
            (event.trigger_time_us == _event_buffer[i].trigger_time_us &&
             event.priority < _event_buffer[i].priority)) {
            pos = i;
            break;
        }
    }

    // Shift elements to make room
    for (uint16_t i = _event_count; i > pos; i--) {
        _event_buffer[i] = _event_buffer[i - 1];
    }

    // Insert the event
    _event_buffer[pos] = event;
    _event_count++;
}

// ============================================================================
// Processing ready events
// ============================================================================
void Scheduler::processReadyEvents() {
    if (_event_count == 0) return;

    uint32_t now_us = (uint32_t)esp_timer_get_time();

    // Process all events whose timestamp has passed.
    // Uses signed subtraction to correctly handle uint32_t overflow
    // (after ~71 min, esp_timer_get_time() truncated to uint32 wraps around to 0).
    while (_event_count > 0 &&
           (int32_t)(now_us - _event_buffer[0].trigger_time_us) >= 0) {
        SchedulerEvent& event = _event_buffer[0];

        // Find the target actuator
        ActuatorConfig* actuator = findActuator(event.actuator_id);
        if (actuator != nullptr) {
            // Safety check before execution
            bool safe = true;
            if (_safety_manager != nullptr) {
                safe = _safety_manager->checkEvent(*actuator, event);
            }

            if (safe) {
                _engine.processEvent(*actuator, event);
                _processed_count++;
            }
            // Event blocked by safety: silently ignored
        } else {
            Serial.printf("[SCHED] Actuator %d not found\n", event.actuator_id);
        }

        // Remove the processed event (shift)
        for (uint16_t i = 0; i < _event_count - 1; i++) {
            _event_buffer[i] = _event_buffer[i + 1];
        }
        _event_count--;
    }
}

// ============================================================================
// Find actuator by ID
// ============================================================================
ActuatorConfig* Scheduler::findActuator(uint8_t id) {
    for (uint8_t i = 0; i < _actuator_count; i++) {
        if (_actuators[i] != nullptr && _actuators[i]->id == id) {
            return _actuators[i];
        }
    }
    return nullptr;
}
