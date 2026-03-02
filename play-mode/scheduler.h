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

    // Register an actuator with the scheduler
    void registerActuator(ActuatorConfig* actuator);

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
};

#endif // SCHEDULER_H
