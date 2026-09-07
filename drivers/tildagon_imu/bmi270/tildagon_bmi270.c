/*
 * Copyright (c) 2023 Bosch Sensortec GmbH. All rights reserved.
 *
 * This file is a minimal Tildagon-specific implementation derived from the
 * Bosch Sensortec BMI270 Sensor API reference code and register-level usage
 * patterns used to guide the sensor configuration and data-processing logic.
 * The upstream Bosch source in this repo is version v2.86.1.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "tildagon_bmi270.h"

#include "tildagon_i2c.h"
#include "esp_err.h"
#include "freertos/semphr.h"

#define ADDRESS 0x69U
#define READ ( MP_MACHINE_I2C_FLAG_WRITE1 | MP_MACHINE_I2C_FLAG_READ | MP_MACHINE_I2C_FLAG_STOP )
#define WRITE MP_MACHINE_I2C_FLAG_STOP

#define BMI2_CHIP_ID_ADDR            0x00U
#define BMI2_STATUS_ADDR             0x03U
#define BMI2_ACC_DATA_ADDR           0x0CU
#define BMI2_GYR_DATA_ADDR           0x12U
#define BMI2_STEP_COUNTER_DATA_ADDR  0x1EU
#define BMI2_TEMP_DATA_ADDR          0x22U
#define BMI2_ACC_CONF_ADDR           0x40U
#define BMI2_GYR_CONF_ADDR           0x42U
#define BMI2_PWR_CTRL_ADDR           0x7DU

#define BMI270_CHIP_ID               0x24U

#define BMI2_ACC_EN_POS              0x02U
#define BMI2_ACC_EN_MASK             0x04U
#define BMI2_GYR_EN_POS              0x01U
#define BMI2_GYR_EN_MASK             0x02U
#define BMI2_TEMP_EN_POS             0x03U
#define BMI2_TEMP_EN_MASK            0x08U
#define BMI2_DRDY_ACC_MASK           0x80U
#define BMI2_DRDY_GYR_MASK           0x40U
#define BMI2_ENABLE                  0x01U

#define BMI2_SET_BITS(reg_data, bitname, data) \
    ((reg_data & ~(bitname##_MASK)) | \
     ((data << bitname##_POS) & bitname##_MASK))

#define ACC_RANGE_2G            0x00U
#define GYR_RANGE_2000          0x00U
#define ODR_25HZ                0x06U
#define ODR_50HZ                0x07U
#define ODR_100HZ               0x08U

#define FLAG_INITIALISED        0x01U

static tildagon_imu_state_t *imu_state = NULL;
static tildagon_mux_i2c_obj_t *mux_port;

static SemaphoreHandle_t _mu;

#define LOCK xSemaphoreTake(_mu, portMAX_DELAY)
#define UNLOCK xSemaphoreGive(_mu)

static int bmi270_status_ready( uint8_t mask )
{
    uint8_t status = 0U;
    if ( bmi270_read( BMI2_STATUS_ADDR, &status, 1U ) < 0 )
    {
        return 0;
    }

    return (status & mask) != 0U;
}

static void bmi270_update_step_count( uint32_t new_count )
{
    if ( imu_state == NULL )
    {
        return;
    }

    if ( new_count >= imu_state->last_step_count )
    {
        imu_state->steps += (new_count - imu_state->last_step_count);
    }
    else
    {
        imu_state->steps += new_count;
    }
    imu_state->last_step_count = new_count;
}

static int bmi270_step_read_raw( uint32_t *count )
{
    uint8_t read_buffer[4] = { 0U };
    int ret = tildagon_i2c_reg_read( TILDAGON_SYS_I2C_PORT, ADDRESS,
                                     BMI2_STEP_COUNTER_DATA_ADDR, read_buffer,
                                     sizeof(read_buffer) );
    if ( ret >= 0 )
    {
        *count = (uint32_t)read_buffer[0] |
                 ((uint32_t)read_buffer[1] << 8) |
                 ((uint32_t)read_buffer[2] << 16) |
                 ((uint32_t)read_buffer[3] << 24);
    }
    return ret;
}

int bmi270_write(uint8_t reg_addr, uint8_t *reg_data, uint8_t len )
{
    if ( len == 0U )
    {
        return 0;
    }

    return tildagon_i2c_reg_write( TILDAGON_SYS_I2C_PORT, ADDRESS,
                                   reg_addr, reg_data, len );
}

int bmi270_read(uint8_t reg_addr, uint8_t *reg_data, uint8_t len )
{
    return tildagon_i2c_reg_read( TILDAGON_SYS_I2C_PORT, ADDRESS,
                                  reg_addr, reg_data, len );
}

int bmi270_set_period(uint16_t period_ms)
{
    uint8_t acc_config[2] = { 0U, 0U };
    uint8_t gyr_config[2] = { 0U, 0U };
    uint8_t odr = ODR_25HZ;
    int ret;

    if ( period_ms <= 10U )
    {
        odr = ODR_100HZ;
    }
    else if ( period_ms <= 20U )
    {
        odr = ODR_50HZ;
    }

    ret = bmi270_read( BMI2_ACC_CONF_ADDR, acc_config, sizeof(acc_config) );
    if ( ret < 0 )
    {
        return ret;
    }

    ret = bmi270_read( BMI2_GYR_CONF_ADDR, gyr_config, sizeof(gyr_config) );
    if ( ret < 0 )
    {
        return ret;
    }

    acc_config[0] = (uint8_t)((acc_config[0] & 0xF0U) | odr);
    acc_config[1] = (uint8_t)((acc_config[1] & 0xFCU) | ACC_RANGE_2G);
    gyr_config[0] = (uint8_t)((gyr_config[0] & 0xF0U) | odr);
    gyr_config[1] = (uint8_t)((gyr_config[1] & 0xF8U) | GYR_RANGE_2000);

    ret = bmi270_write( BMI2_ACC_CONF_ADDR, acc_config, sizeof(acc_config) );
    if ( ret < 0 )
    {
        return ret;
    }

    return bmi270_write( BMI2_GYR_CONF_ADDR, gyr_config, sizeof(gyr_config) );
}

int bmi270_init( tildagon_imu_state_t *state )
{
    imu_state = state;

    if ( imu_state == NULL )
    {
        return ESP_FAIL;
    }

    uint8_t chip_id = 0U;
    uint8_t acc_config[2] = { 0xA6U, 0x00U };
    uint8_t gyr_config[2] = { 0xA6U, 0x00U };
    int ret;

    if (imu_state->flags & FLAG_INITIALISED)
    {
        return ESP_OK;
    }

    _mu = xSemaphoreCreateMutex();
    if ( _mu == NULL )
    {
        return ESP_FAIL;
    }

    mux_port = tildagon_get_mux_obj( TILDAGON_SYS_I2C_PORT );
    if ( mux_port == NULL )
    {
        return ESP_FAIL;
    }

    ret = bmi270_read( BMI2_CHIP_ID_ADDR, &chip_id, 1U );
    if ( ret < 0 || chip_id != BMI270_CHIP_ID )
    {
        return ESP_FAIL;
    }

    uint8_t pwr_ctrl = 0U;
    ret = bmi270_read( BMI2_PWR_CTRL_ADDR, &pwr_ctrl, 1U );
    if ( ret < 0 )
    {
        return ESP_FAIL;
    }

    pwr_ctrl = BMI2_SET_BITS( pwr_ctrl, BMI2_ACC_EN, BMI2_ENABLE );
    pwr_ctrl = BMI2_SET_BITS( pwr_ctrl, BMI2_GYR_EN, BMI2_ENABLE );
    pwr_ctrl = BMI2_SET_BITS( pwr_ctrl, BMI2_TEMP_EN, BMI2_ENABLE );

    ret = bmi270_write( BMI2_PWR_CTRL_ADDR, &pwr_ctrl, 1U );
    if ( ret < 0 )
    {
        return ESP_FAIL;
    }

    ret = bmi270_write( BMI2_ACC_CONF_ADDR, acc_config, sizeof(acc_config) );
    if ( ret < 0 )
    {
        return ESP_FAIL;
    }

    ret = bmi270_write( BMI2_GYR_CONF_ADDR, gyr_config, sizeof(gyr_config) );
    if ( ret < 0 )
    {
        return ESP_FAIL;
    }

    imu_state->flags |= FLAG_INITIALISED;
    return ESP_OK;
}

void bmi270_read_acc_mps(float *x, float *y, float *z)
{
    if ( imu_state == NULL )
    {
        *x = 0.0F;
        *y = 0.0F;
        *z = 0.0F;
        return;
    }

    LOCK;
    *x = imu_state->acc_x;
    *y = imu_state->acc_y;
    *z = imu_state->acc_z;
    UNLOCK;
}

void bmi270_read_gyro_dps(float *x, float *y, float *z)
{
    if ( imu_state == NULL )
    {
        *x = 0.0F;
        *y = 0.0F;
        *z = 0.0F;
        return;
    }

    LOCK;
    *x = imu_state->gyro_x;
    *y = imu_state->gyro_y;
    *z = imu_state->gyro_z;
    UNLOCK;
}

void bmi270_read_steps(uint32_t *steps)
{
    if ( imu_state == NULL )
    {
        *steps = 0U;
        return;
    }

    LOCK;
    *steps = imu_state->steps;
    imu_state->steps = 0U;
    UNLOCK;
}

void bmi270_reset_steps( void )
{
    if ( imu_state == NULL )
    {
        return;
    }

    LOCK;
    imu_state->steps = 0U;
    imu_state->last_step_count = 0U;
    UNLOCK;
}

void bmi270_read_temperature(float *temperature)
{
    if ( imu_state == NULL )
    {
        *temperature = 0.0F;
        return;
    }

    LOCK;
    *temperature = imu_state->temperature;
    UNLOCK;
}

void bmi270_task_acc_gyro( void )
{
    if ( !bmi270_status_ready( BMI2_DRDY_ACC_MASK | BMI2_DRDY_GYR_MASK ) )
    {
        return;
    }

    uint8_t read_buffer[12] = { 0U };
    int ret = bmi270_read( BMI2_ACC_DATA_ADDR, read_buffer, sizeof(read_buffer) );
    if ( ret < 0 )
    {
        return;
    }

    if ( imu_state == NULL )
    {
        return;
    }

    LOCK;
    const float acc_scale = (2.0F * 9.80665F) / 32768.0F;
    const float gyro_scale = 2000.0F / 32768.0F;

    int16_t ax = (int16_t)( read_buffer[0] | ((uint16_t)read_buffer[1] << 8) );
    int16_t ay = (int16_t)( read_buffer[2] | ((uint16_t)read_buffer[3] << 8) );
    int16_t az = (int16_t)( read_buffer[4] | ((uint16_t)read_buffer[5] << 8) );
    int16_t gx = (int16_t)( read_buffer[6] | ((uint16_t)read_buffer[7] << 8) );
    int16_t gy = (int16_t)( read_buffer[8] | ((uint16_t)read_buffer[9] << 8) );
    int16_t gz = (int16_t)( read_buffer[10] | ((uint16_t)read_buffer[11] << 8) );

    imu_state->acc_x = (float)ax * acc_scale;
    imu_state->acc_y = (float)ay * acc_scale;
    imu_state->acc_z = (float)az * acc_scale;
    imu_state->gyro_x = (float)gx * gyro_scale;
    imu_state->gyro_y = (float)gy * gyro_scale;
    imu_state->gyro_z = (float)gz * gyro_scale;
    UNLOCK;
}

void bmi270_task_temperature( void )
{
    uint8_t write_buffer[1] = { BMI2_TEMP_DATA_ADDR };
    uint8_t read_buffer[2] = { 0U };
    mp_machine_i2c_buf_t buffer[2] = { { .len = 1, .buf = write_buffer },
                                       { .len = 2, .buf = read_buffer } };
    int ret = tildagon_mux_i2c_transaction( mux_port, ADDRESS, 2, buffer, READ );
    if ( ret < 0 )
    {
        return;
    }

    if ( imu_state == NULL )
    {
        return;
    }

    LOCK;
    imu_state->temperature = (((float)((int16_t)( read_buffer[0] | ((uint16_t)read_buffer[1] << 8) ))) * 0.001953125F) + 23.0F;
    UNLOCK;
}

void bmi270_task_steps( void )
{
    uint32_t count = 0U;
    if ( bmi270_step_read_raw( &count ) < 0 )
    {
        return;
    }

    LOCK;
    bmi270_update_step_count( count );
    UNLOCK;
}
