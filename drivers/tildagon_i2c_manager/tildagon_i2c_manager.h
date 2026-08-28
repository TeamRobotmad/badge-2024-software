#ifndef TILDAGON_I2C_MANAGER_H
#define TILDAGON_I2C_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

/* Fixed-size job table - keeps this simple and allocation-free. Currently
 * used by the IMU (accel/gyro, temperature, steps, compass); the remaining
 * slots are available for future hexpansion sensors. */
#define TILDAGON_I2C_MGR_MAX_JOBS   (8)

/* Sentinel period meaning "do not poll this job". */
#define TILDAGON_I2C_MGR_PERIOD_OFF (0xFFFFFFFFU)

/* Scheduling resolution of the manager's own background task, independent
 * of the system-wide FreeRTOS tick rate (CONFIG_FREERTOS_HZ). */
#define TILDAGON_I2C_MGR_TICK_MS    (1)

/* A job callback is responsible for performing its own I2C transaction(s)
 * (e.g. via tildagon_mux_i2c_transaction / tildagon_i2c_reg_read) and
 * caching the result; the manager only decides when to call it. */
typedef void (*tildagon_i2c_mgr_job_fn_t)( void );

/* Starts the manager's background task and scheduling timer. Must be called
 * once at board init, before any jobs are registered. */
extern void tildagon_i2c_mgr_init( void );

/* Registers a new job with an initial period (commonly
 * TILDAGON_I2C_MGR_PERIOD_OFF, so the job stays idle until something asks
 * for a period). Returns a handle >= 0 on success, or -1 if the job table
 * is full. */
extern int tildagon_i2c_mgr_register( tildagon_i2c_mgr_job_fn_t callback, uint32_t period_ms );

/* Requests a new period for a job. By default only requests that reduce the
 * period (poll more often) are accepted; pass force=true to also allow
 * increasing it, or to set TILDAGON_I2C_MGR_PERIOD_OFF to stop polling.
 * There is no per-caller tracking - this is a simple "reduce unless forced"
 * rule shared by every caller. Returns true if the requested period was
 * applied. */
extern bool tildagon_i2c_mgr_set_period( int handle, uint32_t period_ms, bool force );

/* Returns a job's current period, or TILDAGON_I2C_MGR_PERIOD_OFF if the
 * handle is invalid. */
extern uint32_t tildagon_i2c_mgr_get_period( int handle );

#endif /* TILDAGON_I2C_MANAGER_H */
