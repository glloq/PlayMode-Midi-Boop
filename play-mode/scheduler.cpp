#include "scheduler.h"
#include "resource_manager.h"
#include <freertos/semphr.h>

// ============================================================================
// PlayMode — Real-Time Scheduler (implementation)
// ============================================================================

// Global queue accessible by actuator_engine for scheduling return events.
// AUDIT FIX (P0.1): the queue now carries SchedulerCommand elements (1-2
// events each), so an atomic pair is a single, indivisible enqueue.
QueueHandle_t g_scheduler_queue = NULL;

// AUDIT FIX (point B): defined in config_manager.cpp. Serialises this task's
// actuator dereferences against structural config edits from the web task.
extern SemaphoreHandle_t g_actuator_mutex;

Scheduler::Scheduler(ActuatorEngine& engine)
    : _engine(engine),
      _resources(nullptr),
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
    // Create the FreeRTOS queue for incoming commands (1-2 events each).
    _input_queue = xQueueCreate(SCHEDULER_QUEUE_SIZE, sizeof(SchedulerCommand));
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
    SchedulerCommand cmd;
    cmd.event_count = 1;
    cmd.events[0] = event;
    // A single queue element — xQueueSend is itself thread-safe.
    return xQueueSend(_input_queue, &cmd, 0) == pdTRUE;
}

bool Scheduler::pushPulse(const SchedulerEvent& on, const SchedulerEvent& off) {
    if (_input_queue == NULL) return false;
    // AUDIT FIX (P0.1): both events travel as ONE command, so the enqueue is
    // atomic and drainInputQueue() inserts the pair into the heap all-or-nothing.
    SchedulerCommand cmd;
    cmd.event_count = 2;
    cmd.events[0] = on;
    cmd.events[1] = off;
    return xQueueSend(_input_queue, &cmd, 0) == pdTRUE;
}

void Scheduler::setResourceManager(ResourceManager* resources) {
    _resources = resources;
}

void Scheduler::clearQueue() {
    // Drain the thread-safe FreeRTOS input queue.
    if (_input_queue != NULL) {
        SchedulerCommand scratch;
        while (xQueueReceive(_input_queue, &scratch, 0) == pdTRUE) { /* discard */ }
    }
    // Empty the internal priority buffer. A single-word store; the real-time
    // task reads _event_count at the top of each tick and simply sees an empty
    // heap. During an emergency stop this is exactly the intended effect.
    _event_count = 0;
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
    // AUDIT FIX (UI-P1): include the FreeRTOS input queue (pending commands not
    // yet drained into the heap), not just the heap, so the reported backlog is
    // truthful.
    uint16_t pending_input = 0;
    if (_input_queue != NULL) {
        pending_input = (uint16_t)uxQueueMessagesWaiting(_input_queue);
    }
    return _event_count + pending_input;
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

            // AUDIT FIX (P0.1/P0.2): the scheduler task is the sole owner of the
            // priority heap and the PCA9685 drivers. Web-initiated emergency
            // stop / re-arm / I²C rescan are only *requested* (atomic flags) and
            // are executed HERE, on Core 1, so there is never concurrent I²C
            // access or unsynchronised heap mutation. The unified ResourceManager
            // also owns admission, observation and reconciliation on this core.
            if (_resources != nullptr) {
                _resources->processPendingRequests(_actuators, _actuator_count);
            }

            processReadyEvents();

            if (_resources != nullptr) {
                _resources->update(_actuators, _actuator_count);
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
    SchedulerCommand cmd;

    // Read all available commands from the queue (non-blocking).
    while (xQueueReceive(_input_queue, &cmd, 0) == pdTRUE) {
        // AUDIT FIX (P0.1): insert the command's events all-or-nothing. If the
        // heap cannot hold every event of the command, drop the WHOLE command
        // so a NOTE_ON is never inserted without its paired NOTE_OFF.
        if ((uint16_t)(_event_count + cmd.event_count) > SCHEDULER_MAX_EVENTS) {
            Serial.printf("[SCHED] WARNING: heap full, dropping command (%d events)\n",
                          cmd.event_count);
            continue;
        }
        for (uint8_t i = 0; i < cmd.event_count; i++) {
            insertEvent(cmd.events[i]);
        }
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
            // AUDIT FIX (architecture): one unified admission gate on Core 1
            // (frequency, duty, polyphony, current budget). The dispatcher no
            // longer makes any budget decision on Core 0.
            bool admitted = true;
            if (_resources != nullptr) {
                admitted = _resources->admit(*actuator, event, event.instrument_index);
            }

            if (admitted) {
                // Bill on the REAL activation state transition (before/after),
                // not on the event type — correct for no-op NOTE_OFFs,
                // auto-returns and retriggers.
                bool was_active = actuator->state.active;
                _engine.processEvent(*actuator, event);
                bool is_active = actuator->state.active;
                _processed_count++;

                if (_resources != nullptr) {
                    _resources->observe(*actuator, was_active, is_active,
                                        event.instrument_index, event.velocity);
                }
            }
            // Event blocked by the resource gate: silently ignored
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
