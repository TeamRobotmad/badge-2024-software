#include "tildagon_i2c_manager.h"
#include "tildagon_i2c_mpless.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "i2c_mgr";

typedef struct
{
    bool in_use;
    uint32_t period_ms;
    int64_t accumulator_us;

    /* Simple legacy callback job (used by the IMU). NULL if this slot holds
     * a generic step-based job instead. */
    tildagon_i2c_mgr_job_fn_t callback;

    /* Generic step-based job fields (only used when callback == NULL). */
    uint8_t port;
    uint16_t i2c_addr;
    uint8_t num_steps;
    tildagon_i2c_mgr_step_t steps[TILDAGON_I2C_MGR_MAX_STEPS];
    uint8_t cache[TILDAGON_I2C_MGR_MAX_JOB_CACHE];
    uint8_t cache_len;
    uint32_t sequence;
    bool valid; /* has at least one poll ever published data? */
} i2c_mgr_job_t;

/* Zero-initialised: every slot starts free (in_use == false). */
static i2c_mgr_job_t jobs[TILDAGON_I2C_MGR_MAX_JOBS];

/* Protects cache/cache_len/sequence/valid publication so read_into() always
 * sees a consistent (cache, sequence) pair - matches the LOCK/UNLOCK
 * convention used by lsm6ds3.c / st3m_imu.c. */
static SemaphoreHandle_t cache_mu;
#define CACHE_LOCK   xSemaphoreTake( cache_mu, portMAX_DELAY )
#define CACHE_UNLOCK xSemaphoreGive( cache_mu )

/* Runs every step of a generic job in order. On success (every step
 * completes and no CHECK step aborts), publishes the new cache and bumps
 * the sequence number; otherwise leaves the previously published data
 * untouched. */
static void i2c_mgr_run_step_job( int handle, i2c_mgr_job_t *job )
{
    uint8_t local_cache[TILDAGON_I2C_MGR_MAX_JOB_CACHE];
    uint8_t offset = 0;

    for ( int s = 0; s < job->num_steps; s++ )
    {
        const tildagon_i2c_mgr_step_t *step = &job->steps[s];

        switch ( (tildagon_i2c_mgr_step_type_t)step->type )
        {
            case TILDAGON_I2C_MGR_STEP_READ:
            {
                esp_err_t err = tildagon_i2c_reg_read( job->port, job->i2c_addr, step->a,
                                                        &local_cache[offset], step->b );
                if ( err != ESP_OK )
                {
                    ESP_LOGI( TAG, "job %d step %d READ reg 0x%02X len %d failed: 0x%x",
                              handle, s, step->a, step->b, err );
                    return;
                }
                offset += step->b;
                break;
            }
            case TILDAGON_I2C_MGR_STEP_WRITE:
            {
                esp_err_t err = tildagon_i2c_reg_write( job->port, job->i2c_addr, step->a,
                                                         step->data, step->b );
                if ( err != ESP_OK )
                {
                    ESP_LOGI( TAG, "job %d step %d WRITE reg 0x%02X len %d failed: 0x%x",
                              handle, s, step->a, step->b, err );
                    return;
                }
                break;
            }
            case TILDAGON_I2C_MGR_STEP_CHECK:
            {
                uint8_t val = local_cache[step->a];
                if ( (val & step->b) == step->data[0] )
                {
                    ESP_LOGI( TAG, "job %d step %d CHECK not ready (byte=0x%02X mask=0x%02X val=0x%02X) - skipping poll",
                              handle, s, val, step->b, step->data[0] );
                    return;
                }
                break;
            }
        }
    }

    CACHE_LOCK;
    memcpy( job->cache, local_cache, offset );
    job->cache_len = offset;
    job->sequence++;
    job->valid = true;
    CACHE_UNLOCK;

    ESP_LOGI( TAG, "job %d poll ok, seq=%u, %d bytes", handle, (unsigned)job->sequence, offset );
}

static inline void i2c_mgr_run_job( int handle )
{
    i2c_mgr_job_t *job = &jobs[handle];
    if ( job->callback != NULL )
    {
        job->callback();
    }
    else
    {
        i2c_mgr_run_step_job( handle, job );
    }
}

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
                i2c_mgr_run_job( i );
                continue;
            }

            jobs[i].accumulator_us += elapsed_us;
            int64_t period_us = (int64_t)jobs[i].period_ms * 1000;
            if ( jobs[i].accumulator_us >= period_us )
            {
                jobs[i].accumulator_us %= period_us;
                i2c_mgr_run_job( i );
            }
        }
    }
}

void tildagon_i2c_mgr_init( void )
{
    ESP_LOGI( TAG, "init" );
    cache_mu = xSemaphoreCreateMutex();
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
            ESP_LOGI( TAG, "job %d registered: callback, period=%ums", i, (unsigned)period_ms );
            return i;
        }
    }
    ESP_LOGW( TAG, "register: job table full" );
    return -1;
}

int tildagon_i2c_mgr_register_steps( uint8_t port, uint16_t i2c_addr,
                                      const tildagon_i2c_mgr_step_t *steps, uint8_t num_steps,
                                      uint32_t period_ms )
{
    if ( num_steps == 0 || num_steps > TILDAGON_I2C_MGR_MAX_STEPS )
    {
        ESP_LOGW( TAG, "register_steps: invalid num_steps %d", num_steps );
        return -1;
    }

    uint8_t cache_len = 0;
    for ( int s = 0; s < num_steps; s++ )
    {
        if ( steps[s].type == TILDAGON_I2C_MGR_STEP_READ )
        {
            cache_len += steps[s].b;
        }
    }
    if ( cache_len > TILDAGON_I2C_MGR_MAX_JOB_CACHE )
    {
        ESP_LOGW( TAG, "register_steps: %d cache bytes exceeds max %d", cache_len, TILDAGON_I2C_MGR_MAX_JOB_CACHE );
        return -1;
    }

    for ( int i = 0; i < TILDAGON_I2C_MGR_MAX_JOBS; i++ )
    {
        if ( !jobs[i].in_use )
        {
            jobs[i].callback = NULL;
            jobs[i].port = port;
            jobs[i].i2c_addr = i2c_addr;
            jobs[i].num_steps = num_steps;
            memcpy( jobs[i].steps, steps, sizeof(tildagon_i2c_mgr_step_t) * num_steps );
            jobs[i].cache_len = 0;
            jobs[i].sequence = 0;
            jobs[i].valid = false;
            jobs[i].period_ms = period_ms;
            jobs[i].accumulator_us = 0;
            jobs[i].in_use = true;
            ESP_LOGI( TAG, "job %d registered: port=%d addr=0x%02X steps=%d cache=%d period=%ums",
                      i, port, i2c_addr, num_steps, cache_len, (unsigned)period_ms );
            return i;
        }
    }
    ESP_LOGW( TAG, "register_steps: job table full" );
    return -1;
}

void tildagon_i2c_mgr_unregister( int handle )
{
    if ( handle >= 0 && handle < TILDAGON_I2C_MGR_MAX_JOBS )
    {
        ESP_LOGI( TAG, "job %d unregistered", handle );
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
            ESP_LOGI( TAG, "job %d period %ums -> %ums", handle, (unsigned)jobs[handle].period_ms, (unsigned)period_ms );
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

int64_t tildagon_i2c_mgr_read_into( int handle, uint8_t *dest, size_t dest_len )
{
    if ( handle < 0 || handle >= TILDAGON_I2C_MGR_MAX_JOBS || !jobs[handle].in_use
         || jobs[handle].callback != NULL )
    {
        return -1;
    }

    i2c_mgr_job_t *job = &jobs[handle];
    int64_t seq = -1;

    CACHE_LOCK;
    if ( job->valid && job->cache_len <= dest_len )
    {
        memcpy( dest, job->cache, job->cache_len );
        seq = (int64_t)job->sequence;
    }
    CACHE_UNLOCK;

    return seq;
}
