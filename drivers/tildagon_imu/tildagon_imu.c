
#include "esp_err.h"
#include "esp_log.h"
#include "py/mperrno.h"
#include "st3m_imu.h"
#include "lsm6ds3.h"

#include "tildagon_imu.h"

typedef enum
{
    ST3M,
    LSM6DS3,
    /* add new imu type here */
    MAX_DEVICES,
} which_imu_t;

typedef void (*stepfuncptr_t) ( uint32_t* steps );
typedef void (*stepresetfuncptr_t) ( void );
typedef void (*tempfuncptr_t) ( float* temperature );
typedef int  (*i2cfuncptr_t) ( uint8_t reg_addr, uint8_t *reg_data, uint8_t len );
typedef int  (*periodfuncptr_t) ( uint16_t period_ms );

i2cfuncptr_t i2c_write[MAX_DEVICES] =
{
    /* ST3M */    st3m_imu_write,
    /* LSM6DS3 */ lsm6ds3_write,
};

i2cfuncptr_t i2c_read[MAX_DEVICES] =
{
    /* ST3M */    st3m_imu_read,
    /* LSM6DS3 */ lsm6ds3_read,
};

periodfuncptr_t set_accel_gyro_period[MAX_DEVICES] =
{
    /* ST3M */    st3m_imu_set_period,
    /* LSM6DS3 */ lsm6ds3_set_period,
};

updatefuncptr_t update_acc_gyro[MAX_DEVICES] =
{
    /* ST3M */    st3m_imu_task_acc_gyro,
    /* LSM6DS3 */ lsm6ds3_task_acc_gyro,
};

updatefuncptr_t update_temperature[MAX_DEVICES] =
{
    /* ST3M */    st3m_imu_task_temperature,
    /* LSM6DS3 */ lsm6ds3_task_temperature,
};

updatefuncptr_t update_steps[MAX_DEVICES] =
{
    /* ST3M */    st3m_imu_task_steps,
    /* LSM6DS3 */ lsm6ds3_task_steps,
};

sensorfuncptr_t accel_read[MAX_DEVICES] =
{
    /* ST3M */     st3m_imu_read_acc_mps,
    /* LSM6DS3 */  lsm6ds3_read_acc_mps,
};

sensorfuncptr_t gyro_read[MAX_DEVICES] =
{
    /* ST3M */     st3m_imu_read_gyro_dps,
    /* LSM6DS3 */  lsm6ds3_read_gyro_dps,
};

stepfuncptr_t step_read[MAX_DEVICES] =
{
    /* ST3M */     st3m_imu_read_steps,
    /* LSM6DS3 */  lsm6ds3_read_steps,
};

stepresetfuncptr_t step_reset[MAX_DEVICES] =
{
    /* ST3M */     st3m_imu_reset_steps,
    /* LSM6DS3 */  NULL,  /* resets its count on each read, no explicit reset */
};

tempfuncptr_t temp_read[MAX_DEVICES] =
{
    /* ST3M */     st3m_imu_read_temperature,
    /* LSM6DS3 */  lsm6ds3_read_temperature,
};

static sensorfuncptr_t compass_readptr = NULL;

static char st3m_id[]  = "bmi270";
static char lsm6ds3_id[]  = "lsm6ds3";
static char* id_list[MAX_DEVICES] =
{
    /* ST3M */     st3m_id,
    /* LSM6DS3 */  lsm6ds3_id,
};

which_imu_t imu = MAX_DEVICES;

/* Handles into the i2c manager's job table, one per sensor group; -1 means
 * "not registered yet" (e.g. compass, before the frontboard registers it). */
static int job_handle[IMU_NUM_GROUPS] = { -1, -1, -1, -1 };

/* Auto-starts a group at a legacy default rate the first time it is read, if
 * nothing has explicitly configured a period for it yet. This preserves
 * behaviour for existing apps that just call e.g. acc_read() and expect
 * regularly-updated data, without needing any per-app bookkeeping. */
static void tildagon_imu_ensure_active( imu_group_t group, uint16_t legacy_period_ms )
{
    if ( job_handle[group] >= 0 &&
         tildagon_i2c_mgr_get_period( job_handle[group] ) == TILDAGON_I2C_MGR_PERIOD_OFF )
    {
        tildagon_i2c_mgr_set_period( job_handle[group], legacy_period_ms, false );
    }
}

void tildagon_imu_init( void )
{
    if ( st3m_imu_init() == ESP_OK )
    {
        imu = ST3M;
    }
    else if ( lsm6ds3_init() == ESP_OK )
    {
        imu = LSM6DS3;
    }

    if ( imu < MAX_DEVICES )
    {
        /* Register with the background i2c manager; every group starts 'off'
         * and only starts polling once something asks for a period (either
         * explicitly via set_period(), or implicitly on first read - see
         * tildagon_imu_ensure_active()). */
        job_handle[IMU_GROUP_ACCEL_GYRO] = tildagon_i2c_mgr_register( update_acc_gyro[imu], TILDAGON_I2C_MGR_PERIOD_OFF, true );
        job_handle[IMU_GROUP_TEMPERATURE] = tildagon_i2c_mgr_register( update_temperature[imu], TILDAGON_I2C_MGR_PERIOD_OFF, false );
        job_handle[IMU_GROUP_STEPS] = tildagon_i2c_mgr_register( update_steps[imu], TILDAGON_I2C_MGR_PERIOD_OFF, false );
    }
}

void tildagon_imu_acc_read( float* x, float*y, float*z )
{
    tildagon_imu_ensure_active( IMU_GROUP_ACCEL_GYRO, IMU_UPDATE_FAST_PERIOD_MS );
    if ( imu < MAX_DEVICES)
    {
        ( *accel_read[imu] )( x, y, z );
    }
    else
    {
        *x = 1.0F;
        *y = 1.0F;
        *z = 1.0F;
    }
}

void tildagon_imu_gyro_read( float* x, float*y, float*z )
{
    tildagon_imu_ensure_active( IMU_GROUP_ACCEL_GYRO, IMU_UPDATE_FAST_PERIOD_MS );
    if ( imu < MAX_DEVICES)
    {
        ( *gyro_read[imu] )( x, y, z );
    }
    else
    {
        *x = 1.0F;
        *y = 1.0F;
        *z = 1.0F;
    }
}

void tildagon_imu_step_counter_read( uint32_t* steps )
{
    tildagon_imu_ensure_active( IMU_GROUP_STEPS, IMU_UPDATE_SLOW_PERIOD_MS );
    if ( imu < MAX_DEVICES)
    {
        ( *step_read[imu] )( steps );
    }
    else
    {
        *steps = 0xFFFFFFFF;
    }
}

void tildagon_imu_step_counter_reset( void )
{
    if ( imu < MAX_DEVICES && step_reset[imu] != NULL )
    {
        ( *step_reset[imu] )();
    }
}

void tildagon_imu_temperature_read( float* temperature )
{
    tildagon_imu_ensure_active( IMU_GROUP_TEMPERATURE, IMU_UPDATE_SLOW_PERIOD_MS );
    if ( imu < MAX_DEVICES)
    {
        ( *temp_read[imu] )( temperature );
    }
    else
    {
        *temperature = -274.0F;
    }
}

char* tildagon_imu_get_id( void )
{
    if ( imu < MAX_DEVICES )
    {
        return id_list[imu];
    }
    else
    {
        static char no_device[] = "no device present";
        return no_device;
    }
}

int tildagon_imu_write( uint8_t address, uint8_t length, uint8_t* buffer )
{
    if ( imu < MAX_DEVICES )
    {
        return ( *i2c_write[imu] )(address, buffer, length );
    }
    else
    {
        return -MP_ENODEV;
    }
}

int tildagon_imu_read( uint8_t address, uint8_t length, uint8_t* buffer )
{
    if ( imu < MAX_DEVICES )
    {
        return ( *i2c_read[imu] )(address, buffer, length );
    }
    else
    {
        return -MP_ENODEV;
    }
}

void tildagon_imu_register_compass( int compass_job_handle, sensorfuncptr_t compass_read )
{
    compass_readptr = compass_read;
    job_handle[IMU_GROUP_COMPASS] = compass_job_handle;
}

void tildagon_imu_compass_read( float* x, float*y, float*z )
{
    tildagon_imu_ensure_active( IMU_GROUP_COMPASS, IMU_UPDATE_FAST_PERIOD_MS );
    if ( compass_readptr != NULL )
    {
        compass_readptr( x, y, z );
    }
}

bool tildagon_imu_set_period( imu_group_t group, uint16_t period_ms, bool force )
{
    if ( group >= IMU_NUM_GROUPS || job_handle[group] < 0 )
    {
        return false;
    }
    if ( !tildagon_i2c_mgr_set_period( job_handle[group], period_ms, force ) )
    {
        return false;
    }

    if ( group == IMU_GROUP_ACCEL_GYRO && imu < MAX_DEVICES )
    {
        uint16_t effective_period = tildagon_i2c_mgr_get_period( job_handle[group] );
        if ( effective_period != TILDAGON_I2C_MGR_PERIOD_OFF &&
             set_accel_gyro_period[imu]( effective_period ) != ESP_OK )
        {
            return false;
        }
    }
    return true;
}

uint16_t tildagon_imu_get_period( imu_group_t group )
{
    if ( group >= IMU_NUM_GROUPS || job_handle[group] < 0 )
    {
        return TILDAGON_I2C_MGR_PERIOD_OFF;
    }
    return tildagon_i2c_mgr_get_period( job_handle[group] );
}
