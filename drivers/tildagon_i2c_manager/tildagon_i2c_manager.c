#include "tildagon_i2c_manager.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "i2c_mgr";

typedef struct
{
    tildagon_i2c_mgr_job_fn_t callback;
    uint32_t period_ms;
    int64_t accumulator_us;
    bool in_use;
} i2c_mgr_job_t;

/* Zero-initialised: every slot starts free (in_use == false). */
static i2c_mgr_job_t jobs[TILDAGON_I2C_MGR_MAX_JOBS];

static void i2c_mgr_task( void* arg )
{
    ESP_LOGI( TAG, "task started" );

    /* Guard against configTICK_RATE_HZ being lower than expected (e.g. if
     * CONFIG_FREERTOS_HZ=1000 hasn't actually taken effect) - vTaskDelay(0)
     * doesn't block at all, which would busy-loop and starve every other
     * task at this priority. */
    TickType_t delay_ticks = pdMS_TO_TICKS( TILDAGON_I2C_MGR_TICK_MS );
    if ( delay_ticks == 0 )
    {
        delay_ticks = 1;
    }

    int64_t last_wake_us = esp_timer_get_time();

    while (1)
    {
        vTaskDelay( delay_ticks );

        /* Measure actual elapsed time rather than assuming exactly
         * TILDAGON_I2C_MGR_TICK_MS passed - a busy system can delay this
         * task past its requested wake-up. */
        int64_t now_us = esp_timer_get_time();
        int64_t elapsed_us = now_us - last_wake_us;
        last_wake_us = now_us;

        for ( int i = 0; i < TILDAGON_I2C_MGR_MAX_JOBS; i++ )
        {
            if ( !jobs[i].in_use || jobs[i].period_ms == TILDAGON_I2C_MGR_PERIOD_OFF )
            {
                continue;
            }

            if ( jobs[i].period_ms == 0 )
            {
                /* 0 means "as fast as possible" - no accumulator needed. */
                jobs[i].callback();
                continue;
            }

            jobs[i].accumulator_us += elapsed_us;
            int64_t period_us = (int64_t)jobs[i].period_ms * 1000;
            if ( jobs[i].accumulator_us >= period_us )
            {
                jobs[i].accumulator_us %= period_us;
                jobs[i].callback();
            }
        }
    }
}

void tildagon_i2c_mgr_init( void )
{
    ESP_LOGI( TAG, "init" );
    xTaskCreate( i2c_mgr_task, "i2c_mgr", 4096, NULL, tskIDLE_PRIORITY + 5, NULL );
}

int tildagon_i2c_mgr_register( tildagon_i2c_mgr_job_fn_t callback, uint32_t period_ms )
{
    for ( int i = 0; i < TILDAGON_I2C_MGR_MAX_JOBS; i++ )
    {
        if ( !jobs[i].in_use )
        {
            jobs[i].callback = callback;
            jobs[i].period_ms = period_ms;
            jobs[i].accumulator_us = 0;
            jobs[i].in_use = true;
            return i;
        }
    }
    return -1;
}

void tildagon_i2c_mgr_unregister( int handle )
{
    if ( handle >= 0 && handle < TILDAGON_I2C_MGR_MAX_JOBS )
    {
        jobs[handle].in_use = false;
    }
}

bool tildagon_i2c_mgr_set_period( int handle, uint32_t period_ms, bool force )
{
    if ( handle < 0 || handle >= TILDAGON_I2C_MGR_MAX_JOBS || !jobs[handle].in_use )
    {
        return false;
    }

    if ( force || period_ms < jobs[handle].period_ms )
    {
        if ( period_ms != jobs[handle].period_ms )
        {
            jobs[handle].period_ms = period_ms;
            jobs[handle].accumulator_us = 0;
        }
    }
    /* Otherwise leave it alone - already running at least as fast as
     * requested, so the caller's requirement is still met. */

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
