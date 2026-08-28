#ifndef TILDAGON_IMU_H
#define TILDAGON_IMU_H
#include "stdint.h"
#include "stdbool.h"

#include "tildagon_i2c_manager.h"

/* Legacy update rates, historically used by imu_run(). These are only used
 * now to auto-start a group at a sane default the first time an app reads
 * its data without having called tildagon_imu_set_period() itself - see
 * tildagon_imu_ensure_active() in tildagon_imu.c. */
#define IMU_UPDATE_FAST_PERIOD_MS   (40)
#define IMU_UPDATE_SLOW_PERIOD_MS   (1000)

typedef void (*updatefuncptr_t) ( void );
typedef void (*sensorfuncptr_t) ( float* x, float*y, float*z );

typedef enum
{
    IMU_GROUP_ACCEL_GYRO,
    IMU_GROUP_TEMPERATURE,
    IMU_GROUP_STEPS,
    IMU_GROUP_COMPASS,
    IMU_NUM_GROUPS,
} imu_group_t;

extern void tildagon_imu_init( void );
extern void tildagon_imu_acc_read( float* x, float*y, float*z );
extern void tildagon_imu_gyro_read( float* x, float*y, float*z );
extern void tildagon_imu_step_counter_read( uint32_t* steps );
extern void tildagon_imu_step_counter_reset( void );
extern void tildagon_imu_temperature_read( float* temperature );
extern char* tildagon_imu_get_id( void );
extern int tildagon_imu_write( uint8_t address, uint8_t length, uint8_t* buffer );
extern int tildagon_imu_read( uint8_t address, uint8_t length, uint8_t* buffer );
extern void tildagon_imu_register_compass( updatefuncptr_t compass_update, sensorfuncptr_t compass_read );
extern void tildagon_imu_compass_read( float* x, float*y, float*z );

/* Requests a new update period (in ms) for a sensor group. By default only
 * accepts requests that reduce the period (poll more often); pass
 * force=true to also allow increasing it, or to pass
 * TILDAGON_I2C_MGR_PERIOD_OFF to stop polling entirely. Returns true if the
 * requested period was applied. */
extern bool tildagon_imu_set_period( imu_group_t group, uint32_t period_ms, bool force );

/* Returns a group's current update period in ms, or
 * TILDAGON_I2C_MGR_PERIOD_OFF if it is not currently being polled. */
extern uint32_t tildagon_imu_get_period( imu_group_t group );

#endif
