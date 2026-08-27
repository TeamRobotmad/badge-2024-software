#ifndef TILDAGON_IMU_H
#define TILDAGON_IMU_H
#include "stdint.h"


#define IMU_UPDATE_FAST_PERIOD_MS   (40)
#define IMU_UPDATE_FAST_SLOW_RATIO  (25)  // 25 * 40ms = 1 second


typedef void (*updatefuncptr_t) ( void );
typedef void (*sensorfuncptr_t) ( float* x, float*y, float*z );

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

#endif
