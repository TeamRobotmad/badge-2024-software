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
    uint32_t sequence;
    tildagon_i2c_mgr_step_t steps[TILDAGON_I2C_MGR_MAX_STEPS];
    uint8_t cache[TILDAGON_I2C_MGR_MAX_JOB_CACHE];
    uint8_t port;
    uint8_t i2c_addr;
    uint8_t num_steps;
    uint8_t cache_len;
    uint8_t attempt;
} i2c_mgr_step_job_t;

typedef struct
{
    uint32_t accumulator_us;
    uint16_t period_ms;
    uint8_t flags;
    union
    {
        tildagon_i2c_mgr_job_fn_t callback;
        i2c_mgr_step_job_t step_job;
    } data;
} i2c_mgr_job_t;

/* flags: three booleans, job kind, and three-bit status in one byte. */
#define JOB_FLAG_IN_USE          (1U << 0)
#define JOB_FLAG_STEP_BASED      (1U << 1)
#define JOB_FLAG_VALID           (1U << 2)
#define JOB_FLAG_RUN_ONCE_PENDING (1U << 3)
#define JOB_STATUS_SHIFT         (4U)
#define JOB_STATUS_MASK          (7U << JOB_STATUS_SHIFT)

/* Zero-initialised: every slot starts free. */
static i2c_mgr_job_t jobs[TILDAGON_I2C_MGR_MAX_JOBS];

/* Protects cache/cache_len/sequence/valid publication so read_into() always
 * sees a consistent (cache, sequence) pair - matches the LOCK/UNLOCK
 * convention used by lsm6ds3.c / st3m_imu.c. */
static SemaphoreHandle_t cache_mu;
#define CACHE_LOCK   xSemaphoreTake( cache_mu, portMAX_DELAY )
#define CACHE_UNLOCK xSemaphoreGive( cache_mu )

static bool i2c_mgr_period_valid( uint16_t period_ms )
{
    return period_ms == TILDAGON_I2C_MGR_PERIOD_OFF ||
           period_ms >= TILDAGON_I2C_MGR_MIN_PERIOD_MS;
}

static inline bool i2c_mgr_job_flag( const i2c_mgr_job_t *job, uint8_t flag )
{
    return (job->flags & flag) == flag;
}

static inline void i2c_mgr_set_status( i2c_mgr_job_t *job, uint8_t status )
{
    job->flags = (job->flags & ~JOB_STATUS_MASK) |
                 ((status << JOB_STATUS_SHIFT) & JOB_STATUS_MASK);
}

static inline uint8_t i2c_mgr_get_job_status( const i2c_mgr_job_t *job )
{
    return (job->flags & JOB_STATUS_MASK) >> JOB_STATUS_SHIFT;
}

static void i2c_mgr_finish_attempt( i2c_mgr_job_t *job, tildagon_i2c_mgr_status_t status )
{
    CACHE_LOCK;
    job->data.step_job.attempt++;
    i2c_mgr_set_status( job, status );
    CACHE_UNLOCK;
}

/* Runs every step of a generic job in order. On success (every step
 * completes and no CHECK step aborts), publishes the new cache and bumps
 * the sequence number; otherwise leaves the previously published data
 * untouched. */
static void i2c_mgr_run_step_job( int handle, i2c_mgr_job_t *job )
{
    i2c_mgr_step_job_t *step_job = &job->data.step_job;
    uint8_t local_cache[TILDAGON_I2C_MGR_MAX_JOB_CACHE];
    uint8_t offset = 0;

    for ( int s = 0; s < step_job->num_steps; s++ )
    {
        const tildagon_i2c_mgr_step_t *step = &step_job->steps[s];

        switch ( (tildagon_i2c_mgr_step_type_t)step->type )
        {
            case TILDAGON_I2C_MGR_STEP_READ:
            {
                esp_err_t err = tildagon_i2c_reg_read( step_job->port, step_job->i2c_addr, step->a,
                                                        &local_cache[offset], step->b );
                if ( err != ESP_OK )
                {
                    ESP_LOGI( TAG, "job %d step %d READ reg 0x%02X len %d failed: 0x%x",
                              handle, s, step->a, step->b, err );
                    i2c_mgr_finish_attempt( job, TILDAGON_I2C_MGR_STATUS_I2C_ERROR );
                    return;
                }
                offset += step->b;
                break;
            }
            case TILDAGON_I2C_MGR_STEP_WRITE:
            {
                esp_err_t err = tildagon_i2c_reg_write( step_job->port, step_job->i2c_addr, step->a,
                                                         step->data, step->b );
                if ( err != ESP_OK )
                {
                    ESP_LOGI( TAG, "job %d step %d WRITE reg 0x%02X len %d failed: 0x%x",
                              handle, s, step->a, step->b, err );
                    i2c_mgr_finish_attempt( job, TILDAGON_I2C_MGR_STATUS_I2C_ERROR );
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
                    i2c_mgr_finish_attempt( job, TILDAGON_I2C_MGR_STATUS_CHECK_ABORTED );
                    return;
                }
                break;
            }
        }
    }

    CACHE_LOCK;
    memcpy( step_job->cache, local_cache, offset );
    step_job->cache_len = offset;
    step_job->sequence++;
    step_job->attempt++;
    job->flags |= JOB_FLAG_VALID;
    i2c_mgr_set_status( job, TILDAGON_I2C_MGR_STATUS_SUCCESS );
    CACHE_UNLOCK;

    ESP_LOGI( TAG, "job %d poll ok, seq=%u attempt=%u, %d bytes", handle,
              (unsigned)step_job->sequence, (unsigned)step_job->attempt, offset );
}

static inline void i2c_mgr_run_job( int handle )
{
    i2c_mgr_job_t *job = &jobs[handle];
    if ( !i2c_mgr_job_flag( job, JOB_FLAG_STEP_BASED ) )
    {
        job->data.callback();
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

    uint32_t last_wake_us = (uint32_t)esp_timer_get_time();

    while (1)
    {
        vTaskDelay( delay_ticks );

        /* Measure actual elapsed time rather than assuming exactly
         * TILDAGON_I2C_MGR_TICK_MS passed - a busy system can delay this
         * task past its requested wake-up. */
        uint32_t now_us = (uint32_t)esp_timer_get_time();
        uint32_t elapsed_us = now_us - last_wake_us;
        last_wake_us = now_us;

        for ( int i = 0; i < TILDAGON_I2C_MGR_MAX_JOBS; i++ )
        {
            if ( !i2c_mgr_job_flag( &jobs[i], JOB_FLAG_IN_USE ) )
            {
                continue;
            }

            bool run_once = false;
            if ( i2c_mgr_job_flag( &jobs[i], JOB_FLAG_STEP_BASED ) )
            {
                CACHE_LOCK;
                run_once = i2c_mgr_job_flag( &jobs[i], JOB_FLAG_RUN_ONCE_PENDING );
                CACHE_UNLOCK;
            }
            if ( run_once )
            {
                i2c_mgr_run_job( i );
                CACHE_LOCK;
                jobs[i].flags &= ~JOB_FLAG_RUN_ONCE_PENDING;
                CACHE_UNLOCK;
                continue;
            }

            if ( jobs[i].period_ms == TILDAGON_I2C_MGR_PERIOD_OFF )
            {
                continue;
            }

            uint32_t period_us = (uint32_t)jobs[i].period_ms * 1000U;
            uint32_t until_due_us = period_us - jobs[i].accumulator_us;
            if ( elapsed_us >= until_due_us )
            {
                jobs[i].accumulator_us = (elapsed_us - until_due_us) % period_us;
                i2c_mgr_run_job( i );
            }
            else
            {
                jobs[i].accumulator_us += elapsed_us;
            }
        }
    }
}

void tildagon_i2c_mgr_init( void )
{
    ESP_LOGI( TAG, "init: %u bytes/job, %u bytes job table",
              (unsigned)sizeof(i2c_mgr_job_t), (unsigned)sizeof(jobs) );
    cache_mu = xSemaphoreCreateMutex();
    xTaskCreate( i2c_mgr_task, "i2c_mgr", 4096, NULL, tskIDLE_PRIORITY + 5, NULL );
}

int tildagon_i2c_mgr_register( tildagon_i2c_mgr_job_fn_t callback, uint16_t period_ms )
{
    if ( callback == NULL || !i2c_mgr_period_valid( period_ms ) )
    {
        ESP_LOGW( TAG, "register: invalid callback or period %ums", (unsigned)period_ms );
        return -1;
    }

    for ( int i = 0; i < TILDAGON_I2C_MGR_MAX_JOBS; i++ )
    {
        if ( !i2c_mgr_job_flag( &jobs[i], JOB_FLAG_IN_USE ) )
        {
            jobs[i].data.callback = callback;
            jobs[i].period_ms = period_ms;
            jobs[i].accumulator_us = 0;
            jobs[i].flags = JOB_FLAG_IN_USE;
            ESP_LOGI( TAG, "job %d registered: callback, period=%ums", i, (unsigned)period_ms );
            return i;
        }
    }
    ESP_LOGW( TAG, "register: job table full" );
    return -1;
}

int tildagon_i2c_mgr_register_steps( uint8_t port, uint8_t i2c_addr,
                                      const tildagon_i2c_mgr_step_t *steps, uint8_t num_steps,
                                      uint16_t period_ms )
{
    if ( port > 7 || i2c_addr > 0x7f )
    {
        ESP_LOGW( TAG, "register_steps: invalid port %u or address 0x%02X",
                  port, i2c_addr );
        return -1;
    }
    if ( !i2c_mgr_period_valid( period_ms ) )
    {
        ESP_LOGW( TAG, "register_steps: invalid period %ums", (unsigned)period_ms );
        return -1;
    }
    if ( num_steps == 0 || num_steps > TILDAGON_I2C_MGR_MAX_STEPS )
    {
        ESP_LOGW( TAG, "register_steps: invalid num_steps %d", num_steps );
        return -1;
    }

    uint16_t cache_len = 0;
    for ( int s = 0; s < num_steps; s++ )
    {
        switch ( (tildagon_i2c_mgr_step_type_t)steps[s].type )
        {
            case TILDAGON_I2C_MGR_STEP_READ:
                cache_len += steps[s].b;
                break;
            case TILDAGON_I2C_MGR_STEP_WRITE:
                if ( steps[s].b > TILDAGON_I2C_MGR_MAX_STEP_BYTES )
                {
                    ESP_LOGW( TAG, "register_steps: invalid WRITE length %u", steps[s].b );
                    return -1;
                }
                break;
            case TILDAGON_I2C_MGR_STEP_CHECK:
                if ( steps[s].a >= cache_len )
                {
                    ESP_LOGW( TAG, "register_steps: CHECK offset %u outside cache", steps[s].a );
                    return -1;
                }
                break;
            default:
                ESP_LOGW( TAG, "register_steps: invalid step type %u", steps[s].type );
                return -1;
        }
    }
    if ( cache_len > TILDAGON_I2C_MGR_MAX_JOB_CACHE )
    {
        ESP_LOGW( TAG, "register_steps: %d cache bytes exceeds max %d", cache_len, TILDAGON_I2C_MGR_MAX_JOB_CACHE );
        return -1;
    }

    for ( int i = 0; i < TILDAGON_I2C_MGR_MAX_JOBS; i++ )
    {
        if ( !i2c_mgr_job_flag( &jobs[i], JOB_FLAG_IN_USE ) )
        {
            i2c_mgr_step_job_t *step_job = &jobs[i].data.step_job;
            step_job->port = port;
            step_job->i2c_addr = i2c_addr;
            step_job->num_steps = num_steps;
            memcpy( step_job->steps, steps, sizeof(tildagon_i2c_mgr_step_t) * num_steps );
            step_job->cache_len = 0;
            step_job->sequence = 0;
            step_job->attempt = 0;
            jobs[i].period_ms = period_ms;
            jobs[i].accumulator_us = 0;
            jobs[i].flags = JOB_FLAG_IN_USE | JOB_FLAG_STEP_BASED;
            i2c_mgr_set_status( &jobs[i], TILDAGON_I2C_MGR_STATUS_IDLE );
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
        CACHE_LOCK;
        jobs[handle].flags = 0;
        CACHE_UNLOCK;
    }
}

bool tildagon_i2c_mgr_set_period( int handle, uint16_t period_ms, bool force )
{
    if ( !i2c_mgr_period_valid( period_ms ) || handle < 0 ||
         handle >= TILDAGON_I2C_MGR_MAX_JOBS ||
         !i2c_mgr_job_flag( &jobs[handle], JOB_FLAG_IN_USE ) )
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

uint16_t tildagon_i2c_mgr_get_period( int handle )
{
    if ( handle < 0 || handle >= TILDAGON_I2C_MGR_MAX_JOBS ||
         !i2c_mgr_job_flag( &jobs[handle], JOB_FLAG_IN_USE ) )
    {
        return TILDAGON_I2C_MGR_PERIOD_OFF;
    }
    return jobs[handle].period_ms;
}

int16_t tildagon_i2c_mgr_run_once( int handle )
{
    if ( handle < 0 || handle >= TILDAGON_I2C_MGR_MAX_JOBS )
    {
        return -1;
    }

    int16_t target_attempt = -1;
    CACHE_LOCK;
    i2c_mgr_job_t *job = &jobs[handle];
    if ( i2c_mgr_job_flag( job, JOB_FLAG_IN_USE | JOB_FLAG_STEP_BASED ) &&
         job->period_ms == TILDAGON_I2C_MGR_PERIOD_OFF &&
         !i2c_mgr_job_flag( job, JOB_FLAG_RUN_ONCE_PENDING ) )
    {
        job->flags |= JOB_FLAG_RUN_ONCE_PENDING;
        i2c_mgr_set_status( job, TILDAGON_I2C_MGR_STATUS_PENDING );
        target_attempt = (uint8_t)(job->data.step_job.attempt + 1U);
    }
    CACHE_UNLOCK;

    if ( target_attempt >= 0 )
    {
        ESP_LOGI( TAG, "job %d one-shot armed for attempt %u", handle,
              (unsigned)target_attempt );
    }
    return target_attempt;
}

bool tildagon_i2c_mgr_get_status( int handle, uint8_t *attempt, uint8_t *status )
{
    if ( handle < 0 || handle >= TILDAGON_I2C_MGR_MAX_JOBS )
    {
        return false;
    }

    bool ok = false;
    CACHE_LOCK;
    i2c_mgr_job_t *job = &jobs[handle];
    if ( i2c_mgr_job_flag( job, JOB_FLAG_IN_USE | JOB_FLAG_STEP_BASED ) )
    {
        *attempt = job->data.step_job.attempt;
        *status = i2c_mgr_get_job_status( job );
        ok = true;
    }
    CACHE_UNLOCK;
    return ok;
}

int64_t tildagon_i2c_mgr_read_into( int handle, uint8_t *dest, size_t dest_len )
{
    if ( handle < 0 || handle >= TILDAGON_I2C_MGR_MAX_JOBS ||
         !i2c_mgr_job_flag( &jobs[handle], JOB_FLAG_IN_USE | JOB_FLAG_STEP_BASED ) )
    {
        return -1;
    }

    i2c_mgr_job_t *job = &jobs[handle];
    i2c_mgr_step_job_t *step_job = &job->data.step_job;
    int64_t seq = -1;

    CACHE_LOCK;
    if ( i2c_mgr_job_flag( job, JOB_FLAG_VALID ) && step_job->cache_len <= dest_len )
    {
        memcpy( dest, step_job->cache, step_job->cache_len );
        seq = (int64_t)step_job->sequence;
    }
    CACHE_UNLOCK;

    return seq;
}
