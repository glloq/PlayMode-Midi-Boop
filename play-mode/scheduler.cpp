#include "scheduler.h"
#include "safety_manager.h"
#include <freertos/semphr.h>

// ============================================================================
// PlayMode — Real-Time Scheduler (implementation)
// ============================================================================

// Global queue accessible by actuator_engine for scheduling return events
QueueHandle_t g_scheduler_queue = NULL;

// AUDIT FIX (point B): defined in config_manager.cpp. Serialises this task's
// actuator dereferences against structural config edits from the web task.
extern SemaphoreHandle_t g_actuator_mutex;

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

// AUDIT FIX: rebuild the actuator pointer table so it exactly mirrors the
// ConfigManager array. Ordering matters for the concurrent Core 1 reader:
// shrink the count first (so it never iterates past a valid pointer), then
// repoint every slot, then publish the final count last.
void Scheduler::syncActuators(ActuatorConfig* base, uint8_t count) {
    if (base == nullptr) {
        _actuator_count = 0;
        return;
    }
    if (count > MAX_ACTUATORS) count = MAX_ACTUATORS;

    if (count < _actuator_count) _actuator_count = count;
    for (uint8_t i = 0; i < count; i++) {
        _actuators[i] = &base[i];
    }
    _actuator_count = count;
    Serial.printf("[SCHED] Actuator table synced (%d actuators)\n", count);
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
        // 1. Drain events from the FreeRTOS queue into the (scheduler-private)
        //    priority buffer. This touches no shared config state, so it runs
        //    unguarded every tick.
        drainInputQueue();

        // 2+3. AUDIT FIX (point B): serialise the sections that dereference
        //    actuator pointers (processReadyEvents + safety update) against
        //    structural config edits from the web task. If a rare edit briefly
        //    holds the lock, skip this tick's processing — the events simply
        //    fire on the next tick. The short timeout guarantees the real-time
        //    task never blocks. If the mutex could not be created, fall back to
        //    the previous unguarded behaviour.
        if (g_actuator_mutex == nullptr ||
            xSemaphoreTake(g_actuator_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {

            processReadyEvents();

            if (_safety_manager != nullptr) {
                _safety_manager->update(_actuators, _actuator_count);
            }

            if (g_actuator_mutex != nullptr) xSemaphoreGive(g_actuator_mutex);
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
// AUDIT FIX: priority queue as a min-heap — O(log n) insertion / pop
// (was O(n²) due to linear search + array shift). Comparator uses signed
// subtraction so it stays correct across the uint32_t wrap of
// `esp_timer_get_time()` (every ~71 min).
// ============================================================================

bool Scheduler::eventLess(const SchedulerEvent& a, const SchedulerEvent& b) {
    int32_t dt = (int32_t)(a.trigger_time_us - b.trigger_time_us);
    if (dt != 0) return dt < 0;
    return a.priority < b.priority;
}

void Scheduler::heapSiftUp(uint16_t idx) {
    while (idx > 0) {
        uint16_t parent = (idx - 1) >> 1;
        if (!eventLess(_event_buffer[idx], _event_buffer[parent])) break;
        SchedulerEvent tmp = _event_buffer[idx];
        _event_buffer[idx] = _event_buffer[parent];
        _event_buffer[parent] = tmp;
        idx = parent;
    }
}

void Scheduler::heapSiftDown(uint16_t idx) {
    for (;;) {
        uint16_t left  = idx * 2 + 1;
        uint16_t right = idx * 2 + 2;
        uint16_t smallest = idx;
        if (left  < _event_count && eventLess(_event_buffer[left],  _event_buffer[smallest])) smallest = left;
        if (right < _event_count && eventLess(_event_buffer[right], _event_buffer[smallest])) smallest = right;
        if (smallest == idx) break;
        SchedulerEvent tmp = _event_buffer[idx];
        _event_buffer[idx] = _event_buffer[smallest];
        _event_buffer[smallest] = tmp;
        idx = smallest;
    }
}

void Scheduler::insertEvent(const SchedulerEvent& event) {
    if (_event_count >= SCHEDULER_MAX_EVENTS) {
        Serial.println("[SCHED] WARNING: priority queue full, event dropped");
        return;
    }
    _event_buffer[_event_count] = event;
    heapSiftUp(_event_count);
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
        // AUDIT FIX: copy the root event by value so the heap can be
        // mutated below without aliasing.
        SchedulerEvent event = _event_buffer[0];

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

        // AUDIT FIX: O(log n) pop from the min-heap (was O(n) array shift).
        _event_count--;
        if (_event_count > 0) {
            _event_buffer[0] = _event_buffer[_event_count];
            heapSiftDown(0);
        }
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
