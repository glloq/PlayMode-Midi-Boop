#include "calibrator.h"
#include <esp_timer.h>
#include <string.h>
#include <math.h>

// ============================================================================
// PlayMode — Acoustic Calibrator (implementation)
// ============================================================================

static const i2s_port_t I2S_PORT = (i2s_port_t)CAL_I2S_PORT;

// ============================================================================
// Constructor / Lifecycle
// ============================================================================

Calibrator::Calibrator(Scheduler& scheduler, ConfigManager& config)
    : _scheduler(scheduler)
    , _config(config)
    , _state(CAL_IDLE)
    , _i2s_ready(false)
    , _cur_act_id(0)
    , _cur_try(0)
    , _state_enter_us(0)
    , _trigger_time_us(0)
    , _onset_threshold(CAL_ONSET_MIN_THRESHOLD)
    , _samples_after_trigger(0)
    , _ambient_abs_sum(0)
    , _ambient_sample_count(0)
    , _latency_sum_us(0)
    , _latency_count(0)
    , _queue_count(0)
    , _queue_idx(0)
    , _result_count(0)
    , _chunk_samples(0)
{
    memset(_results, 0, sizeof(_results));
    memset(_queue, 0, sizeof(_queue));
    memset(_i2s_buf, 0, sizeof(_i2s_buf));
}

bool Calibrator::begin() {
    return initI2S();
}

void Calibrator::stop() {
    _state = CAL_IDLE;
    _queue_count = 0;
    _queue_idx = 0;
    Serial.println("[CAL] Calibration stopped");
}

// ============================================================================
// Start
// ============================================================================

bool Calibrator::startCalibration(uint8_t actuator_id) {
    if (_state != CAL_IDLE) return false;
    if (!_i2s_ready && !initI2S()) return false;

    _queue[0]    = actuator_id;
    _queue_count = 1;
    _queue_idx   = 0;

    _cur_act_id          = actuator_id;
    _cur_try             = 0;
    _latency_sum_us      = 0;
    _latency_count       = 0;
    _ambient_abs_sum     = 0;
    _ambient_sample_count = 0;
    _samples_after_trigger = 0;

    enterState(CAL_AMBIENT);
    Serial.printf("[CAL] Calibration started — actuator %d\n", actuator_id);
    return true;
}

bool Calibrator::startCalibrationAll() {
    if (_state != CAL_IDLE) return false;
    if (!_i2s_ready && !initI2S()) return false;

    ActuatorConfig* acts  = _config.getActuators();
    uint8_t         count = _config.getActuatorCount();

    _queue_count = 0;
    for (uint8_t i = 0; i < count && _queue_count < MAX_ACTUATORS; i++) {
        if (acts[i].enabled) {
            _queue[_queue_count++] = acts[i].id;
        }
    }
    if (_queue_count == 0) return false;

    _queue_idx   = 0;
    _cur_act_id  = _queue[0];
    _cur_try     = 0;
    _latency_sum_us       = 0;
    _latency_count        = 0;
    _ambient_abs_sum      = 0;
    _ambient_sample_count = 0;
    _samples_after_trigger = 0;

    enterState(CAL_AMBIENT);
    Serial.printf("[CAL] Calibration all started — %d actuators\n", _queue_count);
    return true;
}

// ============================================================================
// update() — state machine, call each loop()
// ============================================================================

void Calibrator::update() {
    if (_state == CAL_IDLE || _state == CAL_COMPLETE || _state == CAL_ERROR) {
        return;
    }

    uint32_t now_us = (uint32_t)esp_timer_get_time();

    switch (_state) {

    // -------------------------------------------------------------------------
    case CAL_AMBIENT: {
        // Read samples to measure ambient noise (mean |sample|)
        if (!readChunk() || _chunk_samples == 0) break;

        for (uint32_t i = 0; i < _chunk_samples; i++) {
            // INMP441: 24-bit left-justified in 32-bit word → shift >>8 for 24 bits
            int32_t s = _i2s_buf[i] >> 8;
            _ambient_abs_sum += (uint32_t)(s < 0 ? -s : s);
        }
        _ambient_sample_count += _chunk_samples;

        // Target number of samples for CAL_AMBIENT_MS
        const uint32_t needed = (uint32_t)CAL_AMBIENT_MS * CAL_SAMPLE_RATE / 1000;
        if (_ambient_sample_count >= needed) {
            uint32_t mean_abs = _ambient_abs_sum / _ambient_sample_count;
            _onset_threshold  = (int32_t)(mean_abs * CAL_ONSET_MULTIPLIER);
            if (_onset_threshold < CAL_ONSET_MIN_THRESHOLD) {
                _onset_threshold = CAL_ONSET_MIN_THRESHOLD;
            }
            Serial.printf("[CAL] Ambient mean_abs=%lu, onset threshold=%ld\n",
                          (unsigned long)mean_abs, (long)_onset_threshold);
            enterState(CAL_TRIGGERING);
        }
        break;
    }

    // -------------------------------------------------------------------------
    case CAL_TRIGGERING: {
        // Flush DMA, then trigger the actuator
        flushI2S();
        _samples_after_trigger = 0;
        triggerActuator();
        enterState(CAL_RECORDING);
        break;
    }

    // -------------------------------------------------------------------------
    case CAL_RECORDING: {
        if (!readChunk()) break;

        if (_chunk_samples > 0) {
            int32_t onset_idx = detectOnset();
            if (onset_idx >= 0) {
                // Onset detected — calculate latency in samples since trigger
                uint32_t onset_sample = _samples_after_trigger
                                        - _chunk_samples
                                        + (uint32_t)onset_idx;
                uint32_t latency_us   = onset_sample * 1000000UL / CAL_SAMPLE_RATE;

                Serial.printf("[CAL] Act %d attempt %d/%d: onset sample=%lu -> %lu us (%lu ms)\n",
                              _cur_act_id, _cur_try + 1, CAL_RETRIES,
                              (unsigned long)onset_sample,
                              (unsigned long)latency_us,
                              (unsigned long)(latency_us / 1000));

                _latency_sum_us += latency_us;
                _latency_count++;
                _cur_try++;

                if (_cur_try >= CAL_RETRIES) {
                    finishCurrent();
                } else {
                    enterState(CAL_PAUSING);
                }
                break;
            }
        }

        // Check for timeout
        uint32_t elapsed_us = now_us - _trigger_time_us;
        if (elapsed_us > (uint32_t)CAL_MEASURE_WINDOW_MS * 1000UL) {
            Serial.printf("[CAL] Act %d attempt %d/%d: timeout (%lu ms)\n",
                          _cur_act_id, _cur_try + 1, CAL_RETRIES,
                          (unsigned long)(elapsed_us / 1000));
            _cur_try++;
            if (_cur_try >= CAL_RETRIES) {
                finishCurrent();
            } else {
                enterState(CAL_PAUSING);
            }
        }
        break;
    }

    // -------------------------------------------------------------------------
    case CAL_PAUSING: {
        uint32_t elapsed_ms = (now_us - _state_enter_us) / 1000;
        if (elapsed_ms >= CAL_INTER_RETRY_MS) {
            // AUDIT FIX: if we just changed actuator (advanceToNext
            // resets _ambient_sample_count to 0), re-measure ambient noise
            // to recalculate the onset threshold. Between retries of the same actuator,
            // _ambient_sample_count > 0 so we go directly to trigger.
            if (_ambient_sample_count == 0) {
                enterState(CAL_AMBIENT);
            } else {
                enterState(CAL_TRIGGERING);
            }
        }
        break;
    }

    default:
        break;
    }
}

// ============================================================================
// finishCurrent / advanceToNext
// ============================================================================

void Calibrator::finishCurrent() {
    bool     success = (_latency_count > 0);
    uint16_t avg_ms  = 0;

    if (success) {
        uint32_t avg_us = _latency_sum_us / _latency_count;
        avg_ms = (uint16_t)(avg_us / 1000);
    }

    // Update or create the result entry
    CalibrationResult* res = findResult(_cur_act_id);
    if (!res) {
        // AUDIT FIX: check that the buffer is not full
        if (_result_count >= MAX_ACTUATORS) {
            Serial.printf("[CAL] Results buffer full (%d)\n", _result_count);
            return;
        }
        res = &_results[_result_count++];
    }
    res->actuator_id         = _cur_act_id;
    res->measured_latency_ms = avg_ms;
    res->samples_taken       = _latency_count;
    res->success             = success;
    res->timestamp_ms        = millis();

    Serial.printf("[CAL] Act %d finished: %s, latency=%d ms (%d/%d measurements)\n",
                  _cur_act_id,
                  success ? "OK" : "FAILED",
                  avg_ms,
                  _latency_count,
                  CAL_RETRIES);

    if (!advanceToNext()) {
        enterState(CAL_COMPLETE);
        Serial.printf("[CAL] Calibration complete (%d/%d actuators)\n",
                      _queue_idx, _queue_count);
    }
}

bool Calibrator::advanceToNext() {
    _queue_idx++;
    if (_queue_idx >= _queue_count) return false;

    _cur_act_id           = _queue[_queue_idx];
    _cur_try              = 0;
    _latency_sum_us       = 0;
    _latency_count        = 0;
    _ambient_abs_sum      = 0;
    _ambient_sample_count = 0;
    _samples_after_trigger = 0;

    // Short pause before the next actuator, then re-measure ambient
    enterState(CAL_PAUSING);
    return true;
}

// ============================================================================
// applyResults
// ============================================================================

uint8_t Calibrator::applyResults() {
    ActuatorConfig* acts  = _config.getActuators();
    uint8_t         count = _config.getActuatorCount();
    uint8_t         applied = 0;

    for (uint8_t r = 0; r < _result_count; r++) {
        if (!_results[r].success) continue;
        for (uint8_t i = 0; i < count; i++) {
            if (acts[i].id == _results[r].actuator_id) {
                acts[i].latency_ms = _results[r].measured_latency_ms;
                applied++;
                Serial.printf("[CAL] Act %d latency_ms updated: %d ms\n",
                              acts[i].id, acts[i].latency_ms);
                break;
            }
        }
    }
    Serial.printf("[CAL] %d latencies applied\n", applied);
    return applied;
}

// ============================================================================
// I²S
// ============================================================================

bool Calibrator::initI2S() {
    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    cfg.sample_rate          = CAL_SAMPLE_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = 0;
    cfg.dma_buf_count        = CAL_DMA_BUF_COUNT;
    cfg.dma_buf_len          = CAL_DMA_BUF_LEN;
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = false;
    cfg.fixed_mclk           = 0;

    esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, nullptr);
    if (err != ESP_OK) {
        Serial.printf("[CAL] I2S install error: %d\n", (int)err);
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.bck_io_num   = CAL_I2S_SCK_PIN;
    pins.ws_io_num    = CAL_I2S_WS_PIN;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num  = CAL_I2S_SD_PIN;

    err = i2s_set_pin(I2S_PORT, &pins);
    if (err != ESP_OK) {
        i2s_driver_uninstall(I2S_PORT);
        Serial.printf("[CAL] I2S pin config error: %d\n", (int)err);
        return false;
    }

    i2s_zero_dma_buffer(I2S_PORT);

    // Physical check: read samples to detect an actual microphone
    if (!probeMic()) {
        i2s_driver_uninstall(I2S_PORT);
        Serial.println("[CAL] No microphone detected (signal absent)");
        return false;
    }

    _i2s_ready = true;
    Serial.printf("[CAL] I2S microphone ready (port %d, WS=%d, SCK=%d, SD=%d, %dHz)\n",
                  CAL_I2S_PORT, CAL_I2S_WS_PIN,
                  CAL_I2S_SCK_PIN, CAL_I2S_SD_PIN,
                  CAL_SAMPLE_RATE);
    return true;
}

bool Calibrator::probeMic() {
    // Read a few blocks of samples and verify there is a non-zero signal.
    // Without a mic connected, the I2S bus returns zeros or identical saturated values.
    const int PROBE_READS = 3;
    uint32_t non_zero = 0;
    uint32_t total = 0;
    int32_t first_val = 0;
    uint32_t identical = 0;

    // Short wait for DMA buffers to fill
    vTaskDelay(pdMS_TO_TICKS(20));

    for (int r = 0; r < PROBE_READS; r++) {
        size_t bytes_read = 0;
        esp_err_t err = i2s_read(I2S_PORT, _i2s_buf, sizeof(_i2s_buf),
                                  &bytes_read, pdMS_TO_TICKS(50));
        if (err != ESP_OK || bytes_read == 0) continue;

        uint32_t samples = bytes_read / sizeof(int32_t);
        for (uint32_t i = 0; i < samples; i++) {
            total++;
            if (_i2s_buf[i] != 0) non_zero++;
            if (total == 1) {
                first_val = _i2s_buf[i];
            } else if (_i2s_buf[i] == first_val) {
                identical++;
            }
        }
    }

    if (total == 0) {
        Serial.println("[CAL] Probe: no samples read");
        return false;
    }

    // No non-zero samples → no mic (floating bus = zeros)
    if (non_zero == 0) {
        Serial.printf("[CAL] Probe: %lu samples all zero\n", (unsigned long)total);
        return false;
    }

    // All samples identical → no mic (stuck value)
    if (identical >= total - 1) {
        Serial.printf("[CAL] Probe: %lu identical samples (0x%08lX)\n",
                      (unsigned long)total, (unsigned long)(uint32_t)first_val);
        return false;
    }

    Serial.printf("[CAL] Probe OK: %lu/%lu non-zero samples\n",
                  (unsigned long)non_zero, (unsigned long)total);
    return true;
}

void Calibrator::deinitI2S() {
    if (_i2s_ready) {
        i2s_driver_uninstall(I2S_PORT);
        _i2s_ready = false;
    }
}

void Calibrator::flushI2S() {
    // Drain DMA buffers by reading and discarding all available content
    size_t bytes = 0;
    do {
        i2s_read(I2S_PORT, _i2s_buf, sizeof(_i2s_buf), &bytes, 0);
    } while (bytes > 0);
    i2s_zero_dma_buffer(I2S_PORT);
}

bool Calibrator::readChunk() {
    size_t bytes_read = 0;
    // Timeout 5ms: wait a bit to let DMA buffers fill
    esp_err_t err = i2s_read(I2S_PORT,
                              _i2s_buf, sizeof(_i2s_buf),
                              &bytes_read,
                              pdMS_TO_TICKS(5));
    if (err != ESP_OK) {
        _chunk_samples = 0;
        return false;
    }
    _chunk_samples = bytes_read / sizeof(int32_t);
    _samples_after_trigger += _chunk_samples;
    return true;
}

int32_t Calibrator::detectOnset() const {
    // Finds the first sample whose |24-bit value| exceeds the threshold
    for (uint32_t i = 0; i < _chunk_samples; i++) {
        int32_t s    = _i2s_buf[i] >> 8;   // 32 → 24 bits
        int32_t abs_s = (s < 0) ? -s : s;
        if (abs_s > _onset_threshold) {
            return (int32_t)i;
        }
    }
    return -1;
}

void Calibrator::triggerActuator() {
    ActuatorConfig* acts  = _config.getActuators();
    uint8_t         count = _config.getActuatorCount();

    ActuatorConfig* act_cfg = nullptr;
    for (uint8_t i = 0; i < count; i++) {
        if (acts[i].id == _cur_act_id) { act_cfg = &acts[i]; break; }
    }
    if (!act_cfg) {
        Serial.printf("[CAL] Actuator %d not found!\n", _cur_act_id);
        _state = CAL_ERROR;
        return;
    }

    _trigger_time_us = (uint32_t)esp_timer_get_time();

    SchedulerEvent on_evt = {};
    on_evt.trigger_time_us = _trigger_time_us;
    on_evt.actuator_id     = _cur_act_id;
    on_evt.action          = ACTION_NOTE_ON;
    on_evt.velocity        = CAL_TRIGGER_VELOCITY;
    on_evt.priority        = 0;
    _scheduler.pushEvent(on_evt);

    // For solenoids: schedule automatic NOTE_OFF
    if (act_cfg->type == ACT_SOLENOID) {
        SchedulerEvent off_evt = on_evt;
        off_evt.action          = ACTION_NOTE_OFF;
        off_evt.velocity        = 0;
        off_evt.trigger_time_us = _trigger_time_us
                                  + (uint32_t)act_cfg->pulse_ms * 1000UL
                                  + 50000UL;  // +50ms margin
        _scheduler.pushEvent(off_evt);
    }

    Serial.printf("[CAL] Trigger act %d (attempt %d/%d) @ %lu us\n",
                  _cur_act_id, _cur_try + 1, CAL_RETRIES,
                  (unsigned long)_trigger_time_us);
}

// ============================================================================
// Helpers
// ============================================================================

void Calibrator::enterState(CalibrationState s) {
    _state         = s;
    _state_enter_us = (uint32_t)esp_timer_get_time();
}

CalibrationResult* Calibrator::findResult(uint8_t actuator_id) {
    for (uint8_t i = 0; i < _result_count; i++) {
        if (_results[i].actuator_id == actuator_id) return &_results[i];
    }
    return nullptr;
}

const CalibrationResult* Calibrator::findResult(uint8_t actuator_id) const {
    for (uint8_t i = 0; i < _result_count; i++) {
        if (_results[i].actuator_id == actuator_id) return &_results[i];
    }
    return nullptr;
}

// ============================================================================
// Accessors
// ============================================================================

CalibrationState Calibrator::getState()             const { return _state; }
bool             Calibrator::isRunning()            const {
    return _state != CAL_IDLE
        && _state != CAL_COMPLETE
        && _state != CAL_ERROR;
}
uint8_t          Calibrator::getCurrentActuatorId() const { return _cur_act_id; }

uint8_t Calibrator::getProgress() const {
    if (_queue_count == 0)          return 0;
    if (_state == CAL_COMPLETE)     return 100;
    return (uint8_t)((uint16_t)_queue_idx * 100 / _queue_count);
}

const CalibrationResult* Calibrator::getResult(uint8_t actuator_id) const {
    return findResult(actuator_id);
}

uint8_t Calibrator::getResultCount() const { return _result_count; }
