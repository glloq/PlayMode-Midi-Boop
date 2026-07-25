#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "config.h"
#include "types.h"
#include "actuator_engine.h"

// Forward declarations
class SafetyManager;
class PowerManager;

// ============================================================================
// PlayMode — Real-Time Scheduler (Core 1)
// ============================================================================

class Scheduler {
public:
    Scheduler(ActuatorEngine& engine);

    // Start the scheduler task on Core 1
    bool begin();

    // Stop the scheduler
    void stop();

    // Add an event to the queue (thread-safe, called from Core 0)
    bool pushEvent(const SchedulerEvent& event);

    // AUDIT FIX (P0.6): enqueue a NOTE_ON / NOTE_OFF pair atomically — either
    // both events are accepted or neither is. Prevents a lost NOTE_OFF from
    // leaving a hit-and-hold solenoid energised. Thread-safe.
    bool pushPulse(const SchedulerEvent& on, const SchedulerEvent& off);

    // AUDIT FIX (P0.6): flush every pending event — both the FreeRTOS input
    // queue and the internal priority buffer. Owner-only: must run on the
    // scheduler task (Core 1). Used by the kill switch sequence.
    void clearQueue();

    // AUDIT FIX (P1.3): register the PowerManager so activation is billed AFTER
    // an event really executes (not merely when it is queued).
    void setPowerManager(PowerManager* power);

    // Register an actuator with the scheduler
    void registerActuator(ActuatorConfig* actuator);

    // AUDIT FIX: rebuild the whole actuator pointer table from the first
    // `count` entries of the contiguous config array `base`. Used after a
    // runtime add/remove so the scheduler view stays in sync with
    // ConfigManager. registerActuator alone left stale or duplicate
    // pointers after a delete-then-add, because the config array shifts
    // its elements down on removal.
    void syncActuators(ActuatorConfig* base, uint8_t count);

    // Number of pending events in the priority queue
    uint16_t getQueuedEventCount() const;

    // Number of events processed since startup
    uint32_t getProcessedCount() const;

    // Return the FreeRTOS queue handle (for external use)
    QueueHandle_t getQueueHandle() const;

    // Check if the scheduler is running
    bool isRunning() const;

    // Register the safety manager for pre-event checks
    void setSafetyManager(SafetyManager* safety);

private:
    ActuatorEngine& _engine;
    SafetyManager* _safety_manager;  // Safety pointer (can be null)
    PowerManager*  _power_manager;   // Power pointer (can be null)
    TaskHandle_t _task_handle;
    QueueHandle_t _input_queue;     // FreeRTOS queue for incoming events
    bool _running;
    uint32_t _processed_count;

    // Array of registered actuators
    ActuatorConfig* _actuators[MAX_ACTUATORS];
    uint8_t _actuator_count;

    // Internal priority queue (sorted by timestamp)
    SchedulerEvent _event_buffer[SCHEDULER_MAX_EVENTS];
    uint16_t _event_count;

    // Static FreeRTOS task (entry point)
    static void schedulerTask(void* param);

    // Main scheduler loop
    void run();

    // Transfer events from the FreeRTOS queue to the priority queue
    void drainInputQueue();

    // Insert an event into the priority queue (sorted by timestamp)
    void insertEvent(const SchedulerEvent& event);

    // Process events whose timestamp has been reached
    void processReadyEvents();

    // Find the actuator config by ID
    ActuatorConfig* findActuator(uint8_t id);

    // AUDIT FIX: min-heap helpers — O(log n) insert/pop instead of the
    // previous O(n²) sorted-shift implementation. Wrap-safe comparator.
    static bool eventLess(const SchedulerEvent& a, const SchedulerEvent& b);
    void heapSiftUp(uint16_t idx);
    void heapSiftDown(uint16_t idx);
};

#endif // SCHEDULER_H
