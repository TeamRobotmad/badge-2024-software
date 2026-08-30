#ifndef TILDAGON_I2C_MANAGER_H
#define TILDAGON_I2C_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Fixed-size job table - keeps this simple and allocation-free. Currently
 * used by the IMU (accel/gyro, temperature, steps, compass) plus the
 * generic step-based jobs below; the remaining slots are available for
 * future hexpansion sensors. */
#define TILDAGON_I2C_MGR_MAX_JOBS   (10)

/* Sentinel period meaning "do not poll this job". */
#define TILDAGON_I2C_MGR_PERIOD_OFF (UINT16_MAX)
#define TILDAGON_I2C_MGR_MAX_PERIOD_MS (UINT16_MAX - 1U)

/* Recurring jobs must not monopolise the shared I2C bus. */
#define TILDAGON_I2C_MGR_MIN_PERIOD_MS (10U)

/* Scheduling resolution of the manager's own background task. Relies on
 * CONFIG_FREERTOS_HZ being 1000 (1ms tick). */
#define TILDAGON_I2C_MGR_TICK_MS    (1)

/* Limits for generic step-based jobs (see tildagon_i2c_mgr_register_steps). */
#define TILDAGON_I2C_MGR_MAX_STEPS      (5)
#define TILDAGON_I2C_MGR_MAX_STEP_BYTES (4)
#define TILDAGON_I2C_MGR_MAX_JOB_CACHE  (32)

/* A job callback is responsible for performing its own I2C transaction(s)
 * (e.g. via tildagon_mux_i2c_transaction / tildagon_i2c_reg_read) and
 * caching the result; the manager only decides when to call it. */
typedef void (*tildagon_i2c_mgr_job_fn_t)( void );

typedef enum
{
    TILDAGON_I2C_MGR_STEP_READ = 0,
    TILDAGON_I2C_MGR_STEP_WRITE = 1,
    TILDAGON_I2C_MGR_STEP_CHECK = 2,
} tildagon_i2c_mgr_step_type_t;

typedef enum
{
    TILDAGON_I2C_MGR_STATUS_IDLE = 0,
    TILDAGON_I2C_MGR_STATUS_PENDING = 1,
    TILDAGON_I2C_MGR_STATUS_SUCCESS = 2,
    TILDAGON_I2C_MGR_STATUS_CHECK_ABORTED = 3,
    TILDAGON_I2C_MGR_STATUS_I2C_ERROR = 4,
} tildagon_i2c_mgr_status_t;

/* One step of a generic multi-step job (see tildagon_i2c_mgr_register_steps
 * below). Field meaning depends on `type`:
 *   READ:  a = register address, b = number of bytes to read; the bytes are
 *          appended to the job's cache after any earlier READ steps' bytes.
 *   WRITE: a = register address, b = number of bytes to write, data[0..b-1]
 *          = the bytes to write.
 *   CHECK: a = byte offset into the cache so far (from an earlier READ step
 *          in this same job), b = mask, data[0] = comparison value. If
 *          (cache[a] & b) == data[0], the rest of the job is skipped for
 *          this poll - the published cache and sequence number are left
 *          untouched (this is not treated as an error). */
typedef struct
{
    uint8_t type;
    uint8_t a;
    uint8_t b;
    uint8_t data[TILDAGON_I2C_MGR_MAX_STEP_BYTES];
} tildagon_i2c_mgr_step_t;

/* Starts the manager's background task. Must be called once at board init,
 * before any jobs are registered. */
extern void tildagon_i2c_mgr_init( void );

/* Registers a new job with an initial period. A recurring period must be at
 * least TILDAGON_I2C_MGR_MIN_PERIOD_MS; use TILDAGON_I2C_MGR_PERIOD_OFF to
 * leave it idle. Returns a handle >= 0 on success, or -1 if the period is
 * invalid or the job table is full. */
extern int tildagon_i2c_mgr_register( tildagon_i2c_mgr_job_fn_t callback, uint16_t period_ms );

/* Registers a generic multi-step job (for hexpansion sensors etc.) that the
 * manager's background task executes directly - no per-sensor C code
 * required. `steps` is copied, so the caller's array does not need to
 * outlive this call. All steps run against the same (port, i2c_addr) each
 * time the job's period elapses; if any READ/WRITE fails or a CHECK step
 * aborts the poll, the job's previously published cache and sequence number
 * are left untouched. Returns a handle >= 0 on success, or -1 if the job
 * table is full, num_steps is 0 or > TILDAGON_I2C_MGR_MAX_STEPS, or the
 * period is invalid, or the steps' total READ bytes exceed
 * TILDAGON_I2C_MGR_MAX_JOB_CACHE. */
extern int tildagon_i2c_mgr_register_steps( uint8_t port, uint8_t i2c_addr,
                                             const tildagon_i2c_mgr_step_t *steps, uint8_t num_steps,
                                             uint16_t period_ms );

/* Frees a job's slot so it can be reused by tildagon_i2c_mgr_register()/
 * tildagon_i2c_mgr_register_steps(). */
extern void tildagon_i2c_mgr_unregister( int handle );

/* Requests a new period for a job. Recurring periods below
 * TILDAGON_I2C_MGR_MIN_PERIOD_MS are rejected. By default only requests that
 * reduce the period (poll more often) are accepted; pass force=true to also
 * allow increasing it, or to set TILDAGON_I2C_MGR_PERIOD_OFF to stop polling.
 * There is no per-caller tracking - this is a simple "reduce unless forced"
 * rule shared by every caller. Returns false if the handle or period is
 * invalid; otherwise true, whether or not the period actually changed (e.g.
 * it was already running at or faster than the requested period). */
extern bool tildagon_i2c_mgr_set_period( int handle, uint16_t period_ms, bool force );

/* Returns a job's current period, or TILDAGON_I2C_MGR_PERIOD_OFF if the
 * handle is invalid. */
extern uint16_t tildagon_i2c_mgr_get_period( int handle );

/* Requests exactly one execution of an idle step-based job. The job's period
 * must be TILDAGON_I2C_MGR_PERIOD_OFF. There are no automatic retries, and
 * only one execution may be pending. Returns its wrapping uint8_t attempt
 * number, or -1 if the handle is invalid, the job is recurring, or a one-shot
 * is already pending. */
extern int16_t tildagon_i2c_mgr_run_once( int handle );

/* Reads the latest step-job attempt number and status. The attempt number is
 * incremented after every execution, including CHECK-aborted and failed I2C
 * attempts, and wraps from 255 to 0. Returns false if the handle is invalid
 * or is a callback job. */
extern bool tildagon_i2c_mgr_get_status( int handle, uint8_t *attempt,
                                         uint8_t *status );

/* Copies a step-based job's most recently published cache into dest (dest_len
 * must be >= the job's total READ byte count) and returns the sequence
 * number that data corresponds to. Returns -1 if the handle is invalid, is
 * not a step-based job, dest_len is too small, or no poll has completed
 * successfully yet - dest is left untouched in that case. The copy and the
 * returned sequence are taken under the same lock the background task uses
 * when publishing new data, so they always correspond to each other exactly
 * (comparing sequence numbers from two separate calls would not be safe). */
extern int64_t tildagon_i2c_mgr_read_into( int handle, uint8_t *dest, size_t dest_len );

#endif /* TILDAGON_I2C_MANAGER_H */
