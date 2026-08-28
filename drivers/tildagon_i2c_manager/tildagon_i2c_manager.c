#include "tildagon_i2c_manager.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct
{
    tildagon_i2c_mgr_job_fn_t callback;
    uint32_t period_ms;
    uint32_t accumulator_ms;
    bool in_use;
} i2c_mgr_job_t;

/* Zero-initialised: every slot starts free (in_use == false). */
static i2c_mgr_job_t jobs[TILDAGON_I2C_MGR_MAX_JOBS];

static TaskHandle_t mgr_task_handle;

/* Runs in the esp_timer service task; kept minimal - just wakes the manager
 * task, all the actual I2C work happens there. */
static void tick_timer_cb( void* arg )
{
    xTaskNotifyGive( mgr_task_handle );
}

static void i2c_mgr_task( void* arg )
{
    while (1)
    {
        ulTaskNotifyTake( pdTRUE, portMAX_DELAY );

        for ( int i = 0; i < TILDAGON_I2C_MGR_MAX_JOBS; i++ )
        {
            if ( !jobs[i].in_use || jobs[i].period_ms == TILDAGON_I2C_MGR_PERIOD_OFF )
            {
                continue;
            }

            jobs[i].accumulator_ms += TILDAGON_I2C_MGR_TICK_MS;
            if ( jobs[i].accumulator_ms >= jobs[i].period_ms )
            {
                jobs[i].accumulator_ms = 0;
                jobs[i].callback();
            }
        }
    }
}

void tildagon_i2c_mgr_init( void )
{
    xTaskCreate( i2c_mgr_task, "i2c_mgr", 4096, NULL, configMAX_PRIORITIES - 2, &mgr_task_handle );

    /* esp_timer gives 1ms scheduling resolution for this task alone, without
     * changing the system-wide FreeRTOS tick rate (CONFIG_FREERTOS_HZ, which
     * defaults to 100Hz/10ms and affects every other task in the system). */
    const esp_timer_create_args_t timer_args =
    {
        .callback = &tick_timer_cb,
        .name = "i2c_mgr_tick",
    };
    esp_timer_handle_t tick_timer;
    esp_timer_create( &timer_args, &tick_timer );
    esp_timer_start_periodic( tick_timer, TILDAGON_I2C_MGR_TICK_MS * 1000ULL );
}

int tildagon_i2c_mgr_register( tildagon_i2c_mgr_job_fn_t callback, uint32_t period_ms )
{
    for ( int i = 0; i < TILDAGON_I2C_MGR_MAX_JOBS; i++ )
    {
        if ( !jobs[i].in_use )
        {
            jobs[i].callback = callback;
            jobs[i].period_ms = period_ms;
            jobs[i].accumulator_ms = 0;
            jobs[i].in_use = true;
            return i;
        }
    }
    return -1;
}

bool tildagon_i2c_mgr_set_period( int handle, uint32_t period_ms, bool force )
{
    if ( handle < 0 || handle >= TILDAGON_I2C_MGR_MAX_JOBS || !jobs[handle].in_use )
    {
        return false;
    }

    if ( !force && period_ms > jobs[handle].period_ms )
    {
        /* Only reducing the period is allowed without force. */
        return false;
    }

    if ( period_ms != jobs[handle].period_ms )
    {
        jobs[handle].period_ms = period_ms;
        jobs[handle].accumulator_ms = 0;
    }
    return true;
}

uint32_t tildagon_i2c_mgr_get_period( int handle )
{
    if ( handle < 0 || handle >= TILDAGON_I2C_MGR_MAX_JOBS || !jobs[handle].in_use )
    {
        return TILDAGON_I2C_MGR_PERIOD_OFF;
    }
    return jobs[handle].period_ms;
}
