/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

 /**
  * Taken partly from ports/esp32/machine_i2c.c
  * 2026: Upgraded for use with ESP IDF Version 5.5 and the new async I2C driver API.
 */

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "py/mpthread.h"
#include "extmod/modmachine.h"

#include "driver/i2c_master.h"
#include "driver/gpio.h"

#include "tildagon_i2c.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "Tildagon_I2C";

#if MICROPY_PY_TILDAGON_I2C

#define _PERFORMANCE_TRACE_GPIO

#define TILDAGON_I2C_MUX_ADDRESS (0x77)

#define I2C_DEFAULT_TIMEOUT_US (50000) // 50ms

#define MP_I2C_MUX_PORT_MIN (0)
#define MP_I2C_MUX_PORT_MAX (7)



static tildagon_mux_i2c_obj_t tildagon_mux_i2c_obj[8];

static tca9548a_i2c_mux_t tildagon_i2c_mux;

static i2c_master_dev_handle_t m_target_dev_handle = NULL;

static i2c_device_config_t m_target_dev_config;


/**
 * @brief Sets the active downstream port on the TCA9548A I2C multiplexer.
 */
static esp_err_t tca9548a_set_downstream_raw(tca9548a_i2c_mux_t *self, uint8_t port, TickType_t ticks_to_wait) {
    esp_err_t ret = ESP_OK;
    if (port != self->active_port) {
        uint8_t control_byte = (1 << port);

        ret = i2c_master_transmit(self->mux_device, &control_byte, 1, ticks_to_wait);
        if (ret == ESP_OK) {
            self->active_port = port;
        }
    }
    return ret;
}


/**
 * @brief Tidy up the I2C transaction state after a timeout or error.
 * so that we don't get a spurious interrupt
 *
 * @param self
 */
static esp_err_t tildagon_mux_i2c_transaction_tidyup(tildagon_mux_i2c_obj_t *self, i2c_master_dev_handle_t target_dev)
{
    // Clear the I2C hardware state machine to avoid spurious interrupts
    esp_err_t err = i2c_master_bus_reset(self->mux->bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset I2C bus after error: %s", esp_err_to_name(err));
    }

    // Hardware reset the physical multiplexer chip on the board
    gpio_set_level(GPIO_NUM_9, 0);
    esp_rom_delay_us(1);
    gpio_set_level(GPIO_NUM_9, 1);
    tildagon_i2c_mux.active_port = 0xFF; // Force the next call to re-select the

    if (target_dev) {
        err = i2c_master_bus_rm_device(target_dev);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remove target device after error: %s", esp_err_to_name(err));
        }
        target_dev = NULL;
    }

    // deregister mux device
    err = i2c_master_bus_rm_device(tildagon_i2c_mux.mux_device);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove mux device after error: %s", esp_err_to_name(err));
    }
    tildagon_i2c_mux.mux_device = NULL;

    err = i2c_del_master_bus(tildagon_i2c_mux.bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete i2c bus after error: %s", esp_err_to_name(err));
    }
    tildagon_i2c_mux.bus_handle = NULL;

    tildagon_i2c_init();
    return ESP_OK;
}


/**
 * @brief Main micro-allocated Async Transaction Runner.
 * Routing payloads through a TCA9548A.
 */
int tildagon_mux_i2c_transaction(tildagon_mux_i2c_obj_t *self,
                                 uint16_t addr,
                                 size_t n,
                                 mp_machine_i2c_buf_t *bufs,
                                 unsigned int flags) {
    size_t data_len = 0;
    esp_err_t err = ESP_OK;
    TickType_t timeout_ticks = pdMS_TO_TICKS(100);
    i2c_master_dev_handle_t target_dev = NULL;
    bool b_ofinterest = false;

    // Investigating why EEPROM access fails
    /*
    if (addr >= 0x50 && addr <= 0x57) {
        ESP_LOGI(TAG, "port=%u, Addr=0x%02X, n=%u, flags=%s", self->port, addr, n, (flags & MP_MACHINE_I2C_FLAG_READ) ? ((flags & MP_MACHINE_I2C_FLAG_WRITE1) ? "WRITE1" : "READ") : "WRITE");
        b_ofinterest = true;
    }
    */

    static int  last_core_id = -1;
    static char *last_task_name = NULL;

    if (b_ofinterest) {
         // 1. Get the raw core ID (Returns 0 or 1 on the ESP32-S3)
        int core_id = xPortGetCoreID();

        // 2. Get the string name of the currently running FreeRTOS task
        TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
        const char *task_name = pcTaskGetName(current_task);
        UBaseType_t task_priority = uxTaskPriorityGet(current_task);

        // if core or task has changed since last time, print a warning message (only once per change)
        if (core_id != last_core_id || task_name != last_task_name) {
            ESP_LOGI(TAG, "I2C transaction function running on CORE %d | Task Name: '%s', Task Priority: %u", core_id, task_name, task_priority);
            last_core_id = core_id;
            last_task_name = (char *)task_name;
        }
    }
    else
    {
        last_core_id = -1;
        last_task_name = NULL;
    }

    // GIL only exists inside the MicroPython VM; this runs from C board-init too, where it is NULL.
    const bool gil_held = false; // (mp_thread_get_state() != NULL);

    // ==========================================
    // DYNAMIC PRIORITY BOOST
    // ==========================================
    // 1. Get the current task handle running this C code
    //TaskHandle_t current_task_handle = xTaskGetCurrentTaskHandle();

    // 2. Query and save its original priority
    //UBaseType_t original_priority = uxTaskPriorityGet(current_task_handle);

    // 3. Boost it to a high priority (e.g., configMAX_PRIORITIES - 2 or an explicit high rank)
    // Make sure this is higher than your background display/processing tasks
    //UBaseType_t boost_priority = configMAX_PRIORITIES - 1; //original_priority + 2;
    //if (boost_priority >= configMAX_PRIORITIES) {
    //    boost_priority = configMAX_PRIORITIES - 1;
    //}
    //vTaskPrioritySet(current_task_handle, boost_priority);
    // Force FreeRTOS to recognize the priority state change before blocking
    //taskYIELD();

    // ==========================================
    // Lock the bus for the ENTIRE transaction
    // ==========================================
    #ifdef _PERFORMANCE_TRACE_GPIO
    gpio_set_direction(GPIO_NUM_14, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_NUM_11, GPIO_MODE_OUTPUT);
    #endif
    if (gil_held) { MP_THREAD_GIL_EXIT(); } // Release GIL so other python threads run while we wait for the hardware lock
    BaseType_t sem_ret = xSemaphoreTake(self->mux->mtx, timeout_ticks);
    if (gil_held) { MP_THREAD_GIL_ENTER(); } // Re-enter GIL to modify memory safe profiles
    #ifdef _PERFORMANCE_TRACE_GPIO
    gpio_set_level(GPIO_NUM_14, 0);
    gpio_set_level(GPIO_NUM_11, 0);
    gpio_set_level(GPIO_NUM_14, 1);
    #endif

    if (sem_ret != pdTRUE) {
        #ifdef _PERFORMANCE_TRACE_GPIO
        // toggle the GPIO_11 three times to indicate a timeout error
        for (int i = 0; i < 3; i++) {
            gpio_set_level(GPIO_NUM_11, 1);
            esp_rom_delay_us(1);
            gpio_set_level(GPIO_NUM_11, 0);
        }
        gpio_set_level(GPIO_NUM_11, 0);
        gpio_set_level(GPIO_NUM_14, 0);    // Diagnostics GPIO output to indicate I2C error

        #endif
        //vTaskPrioritySet(current_task_handle, original_priority); // RESTORE PRIORITY ON EARLY EXIT
        return -MP_ETIMEDOUT;
    }

    // Route the multiplexer to match the required channel assignment
    err = tca9548a_set_downstream_raw(self->mux, self->port, timeout_ticks);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set downstream port %d on TCA9548A mux: %s", self->port, esp_err_to_name(err));
        tildagon_mux_i2c_transaction_tidyup(self, NULL);
        goto transaction_exit_release_mutex;
    }

    {
        i2c_device_config_t dev_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = TILDAGON_HOST_I2C_FREQ,
        };

        err = i2c_master_bus_add_device(tildagon_i2c_mux.bus_handle, &dev_config, &target_dev);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add target device 0x%02X to I2C bus: %s", addr, esp_err_to_name(err));
            goto transaction_exit_release_mutex;
        }

        /*
        if (addr != m_target_dev_config.device_address) {
            // Change the target device address to the one required for this transaction
            err = i2c_master_device_change_address(m_target_dev_handle, addr, timeout_ticks);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to change target device address to 0x%02X: %s", addr, esp_err_to_name(err));
                goto transaction_exit_release_mutex;
            }
            else
            {
                //ESP_LOGI(TAG, "Target device address changed to 0x%02X", addr);
            }
            m_target_dev_config.device_address = addr;
        }
        */

        if (flags & MP_MACHINE_I2C_FLAG_WRITE1) {
            // Clear segmentation properties to safeguard against 259 memory overlap rules
            uint8_t *reg_ptr = bufs[0].buf;
            size_t reg_len = bufs[0].len;
            uint8_t *rx_ptr = bufs[1].buf;
            size_t rx_len = bufs[1].len;

            err = i2c_master_transmit_receive(target_dev, reg_ptr, reg_len, rx_ptr, rx_len, timeout_ticks);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to T/R (n=%u) to device 0x%02X: %s, reg_len=%u, reg=0x%02X, rx_len=%u", n, addr, esp_err_to_name(err), reg_len, reg_ptr[0], rx_len);
                goto transaction_cleanup_error;
            }
            data_len = reg_len + rx_len;
        } else if (n == 1 && bufs[0].len == 0) {
            // Special case: zero-length transfer (used for probing)
            ESP_LOGI(TAG, "Probing device 0x%02X on I2C bus", addr);
            err = i2c_master_probe(self->mux->bus_handle, addr, timeout_ticks);
            if (err == ESP_ERR_NOT_FOUND) {
                ESP_LOGI(TAG, "Device 0x%02X not found on I2C bus", addr);
                goto transaction_cleanup_error;
            } else if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to probe device 0x%02X: %s", addr, esp_err_to_name(err));
                goto transaction_cleanup_error;
            }
        // Transfer data and copy it from/to the buffers as needed.
        } else if (flags & MP_MACHINE_I2C_FLAG_READ) {
            for (size_t i = 0; i < n; i++) {
                if (bufs[i].len == 0)
                {
                    ESP_LOGI(TAG, "Skipping read zero-length buffer %zu for device 0x%02X", i, addr);
                    continue;
                }
                err = i2c_master_receive(target_dev, bufs[i].buf, bufs[i].len, timeout_ticks);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to receive from device 0x%02X: %s", addr, esp_err_to_name(err));
                    goto transaction_cleanup_error;
                }
                data_len += bufs[i].len;
            }
        } else if (1 == n) {
            // WRITE
            err = i2c_master_transmit(target_dev, bufs[0].buf, bufs[0].len, timeout_ticks);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to write device 0x%02X: %s", addr, esp_err_to_name(err));
                goto transaction_cleanup_error;
            }
            data_len = bufs[0].len;
        } else if (1 < n) {
            // Allocate an array of modern buffer tracking objects.
            // Max size will be 'n' (the fragments).
            i2c_master_transmit_multi_buffer_info_t info_list[n];
            size_t info_count = 0;
            for (size_t i = 0; i < n; i++) {
                if (bufs[i].len != 0) {
                    info_list[info_count].write_buffer = bufs[i].buf;
                    info_list[info_count].buffer_size = bufs[i].len;
                    info_count++;
                    data_len += bufs[i].len;
                }
            }
            // Fire the entire multi-buffer collection in ONE atomic action!
            err = i2c_master_multi_buffer_transmit(target_dev, info_list, info_count, timeout_ticks);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to transmit to port %u, device 0x%02X: n=%u, %s", self->port, addr, n, esp_err_to_name(err));
                goto transaction_cleanup_error;
            }
        }
    }

    if (err == ESP_OK) {
        // log the successful transaction, device, address and type
        ESP_LOGI(TAG, "I2C transaction successful: device 0x%02X, %s, %s, n=%u, %u bytes", addr,
                (flags & MP_MACHINE_I2C_FLAG_READ) ? "read" : "write", (flags & MP_MACHINE_I2C_FLAG_WRITE1) ? "write1" : "", n, data_len);
    }

transaction_exit_release_mutex:
transaction_cleanup_error:
    if (target_dev) {
        i2c_master_bus_rm_device(target_dev);
        target_dev = NULL;
    }

    if (ESP_ERR_TIMEOUT == err || ESP_ERR_INVALID_STATE == err) {
        ESP_LOGE(TAG, "I2C '%s' port=%u, device 0x%02X, n=%u, flags=%s", esp_err_to_name(err), self->port, addr, n, (flags & MP_MACHINE_I2C_FLAG_READ) ? ((flags & MP_MACHINE_I2C_FLAG_WRITE1) ? "WRITE1" : "READ") : "WRITE");
        err = i2c_master_bus_reset(self->mux->bus_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reset I2C bus after error: %s", esp_err_to_name(err));
        }
    }

    #ifdef _PERFORMANCE_TRACE_GPIO
    if (err != ESP_OK) {
        gpio_set_level(GPIO_NUM_11, 1);    // Diagnostics GPIO output to indicate I2C error
    }
    gpio_set_level(GPIO_NUM_14, 0);    // Diagnostics GPIO output to indicate end of I2C activity
    #endif

    // Restore the task to its normal scheduling rank before returning to Python
    //vTaskPrioritySet(current_task_handle, original_priority);

    xSemaphoreGive(self->mux->mtx);

    if (err == ESP_FAIL || err == ESP_ERR_INVALID_RESPONSE || err == ESP_ERR_INVALID_STATE || err == ESP_ERR_NOT_FOUND) {
        return -MP_ENODEV;
    } else if (err == ESP_ERR_TIMEOUT) {
        return -MP_ETIMEDOUT;
    } else if (err != ESP_OK) {
        return -abs(err);
    }
    return data_len;
}


tildagon_mux_i2c_obj_t* tildagon_get_mux_obj( uint8_t port )
{
    if ( tildagon_mux_i2c_obj[port].base.type == NULL )
    {
        // Created for the first time
        tildagon_mux_i2c_obj[port].base.type = &machine_i2c_type;
        tildagon_mux_i2c_obj[port].mux = tildagon_get_i2c_mux();
        tildagon_mux_i2c_obj[port].port = port;
    }
    return &tildagon_mux_i2c_obj[port];
}

tca9548a_i2c_mux_t *tildagon_get_i2c_mux() {
    return &tildagon_i2c_mux;
}

void tildagon_i2c_init() {
    // Only allow one instance of the I2C multiplexer to be created
    if (tildagon_i2c_mux.bus_handle != NULL) {
        return;
    }

    if (tildagon_i2c_mux.mtx == NULL) {
        tildagon_i2c_mux.mtx = xSemaphoreCreateMutex();
    }
    tildagon_i2c_mux.addr = TILDAGON_I2C_MUX_ADDRESS;       // Address for the TCA9548A I2C multiplexer on the Tildagon board
    tildagon_i2c_mux.active_port = 0xFF;

    // Define Master Bus Parameters
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = TILDAGON_HOST_I2C_PORT,
        .scl_io_num = TILDAGON_HOST_I2C_SCL,
        .sda_io_num = TILDAGON_HOST_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,                   // Auto-allocate interrupt rank level
        .trans_queue_depth = 0,               // Allocating a non-zero queue unlocks Async behavior!
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &tildagon_i2c_mux.bus_handle));

    // Register the TCA9548A hardware multiplexer itself as a device tied to the new bus
    i2c_device_config_t mux_dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TILDAGON_I2C_MUX_ADDRESS,
        .scl_speed_hz = TILDAGON_HOST_I2C_FREQ,
    };

    ESP_ERROR_CHECK(i2c_master_bus_add_device(tildagon_i2c_mux.bus_handle, &mux_dev_config, &tildagon_i2c_mux.mux_device));

    // Register a generic device tied to the new bus - we will change its address dynamically for each transaction
    m_target_dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    m_target_dev_config.device_address = 0x50;  // Will be replaced dynamically for each transaction
    m_target_dev_config.scl_speed_hz = TILDAGON_HOST_I2C_FREQ;

    //ESP_ERROR_CHECK(i2c_master_bus_add_device(tildagon_i2c_mux.bus_handle, &m_target_dev_config, &m_target_dev_handle));
    //i2c_master_event_callbacks_t cbs = {
    //    .on_trans_done = i2c_async_transaction_cb
    //};
    //ESP_ERROR_CHECK(i2c_master_register_event_callbacks(m_target_dev_handle, &cbs, m_result_queue));

    ESP_LOGI(TAG, "I2C multiplexer initialized on bus %d, SCL=%d, SDA=%d, freq=%d Hz", TILDAGON_HOST_I2C_PORT, TILDAGON_HOST_I2C_SCL, TILDAGON_HOST_I2C_SDA, TILDAGON_HOST_I2C_FREQ);

    // Setup activity indicator trace on diagnostics line
    #ifdef _PERFORMANCE_TRACE_GPIO
    gpio_set_direction(GPIO_NUM_11, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_NUM_14, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_11, 0);
    gpio_set_level(GPIO_NUM_14, 0);
    #endif
    // reset I2C multiplexer
    gpio_set_direction(GPIO_NUM_9, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_9, 1);
}


int tildagon_mux_i2c_transfer(mp_obj_base_t *self_in, uint16_t addr, size_t n, mp_machine_i2c_buf_t *bufs, unsigned int flags) {
    tildagon_mux_i2c_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (addr == self->mux->addr) {
        return -MP_ENODEV;
    }

    return tildagon_mux_i2c_transaction( self, addr, n, bufs, flags);
}

/******************************************************************************/
// MicroPython bindings for machine API

static void tildagon_mux_i2c_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    tildagon_mux_i2c_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "I2C(%u, freq=%u, async)", self->port, TILDAGON_HOST_I2C_FREQ);
}

mp_obj_t tildagon_mux_i2c_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    MP_MACHINE_I2C_CHECK_FOR_LEGACY_SOFTI2C_CONSTRUCTION(n_args, n_kw, all_args);

    // Parse args
    enum { ARG_id };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // Get I2C bus
    mp_int_t i2c_id = mp_obj_get_int(args[ARG_id].u_obj);
    if (!(MP_I2C_MUX_PORT_MIN <= i2c_id && i2c_id <= MP_I2C_MUX_PORT_MAX)) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("I2C(%d) doesn't exist"), i2c_id);
    }

    // Get static peripheral object
    tildagon_mux_i2c_obj_t *self = (tildagon_mux_i2c_obj_t *)&tildagon_mux_i2c_obj[i2c_id];

    if (self->base.type == NULL) {
        // Created for the first time, set default pins
        self->base.type = &machine_i2c_type;
        self->mux = tildagon_get_i2c_mux();
        self->port = i2c_id;
    }

    return MP_OBJ_FROM_PTR(self);
}

static const mp_machine_i2c_p_t tildagon_mux_i2c_p = {
    .transfer_supports_write1 = true,
    .transfer = tildagon_mux_i2c_transfer,
};

MP_DEFINE_CONST_OBJ_TYPE(
    machine_i2c_type,
    MP_QSTR_I2C,
    MP_TYPE_FLAG_NONE,
    make_new, tildagon_mux_i2c_make_new,
    print, tildagon_mux_i2c_print,
    protocol, &tildagon_mux_i2c_p,
    locals_dict, &mp_machine_i2c_locals_dict
    );

esp_err_t tildagon_i2c_reg_read(uint8_t port, uint16_t addr, uint8_t reg_addr, uint8_t *data, uint32_t len) {
    tildagon_mux_i2c_obj_t *mux_obj = tildagon_get_mux_obj(port);
    uint8_t tx[] = { reg_addr };
    mp_machine_i2c_buf_t bufs[2] = { { .len = 1, .buf = tx }, { .len = len, .buf = data } };
    int ret = tildagon_mux_i2c_transaction(mux_obj, addr, 2, bufs,
        MP_MACHINE_I2C_FLAG_WRITE1 | MP_MACHINE_I2C_FLAG_READ | MP_MACHINE_I2C_FLAG_STOP);
    return ret < 0 ? ESP_FAIL : ESP_OK;
}

esp_err_t tildagon_i2c_reg_write(uint8_t port, uint16_t addr, uint8_t reg_addr, const uint8_t *data, uint32_t len) {
    tildagon_mux_i2c_obj_t *mux_obj = tildagon_get_mux_obj(port);
    uint8_t tx[len + 1];
    tx[0] = reg_addr;
    memcpy(tx + 1, data, len);
    mp_machine_i2c_buf_t bufs[1] = { { .len = len + 1, .buf = tx } };
    int ret = tildagon_mux_i2c_transaction(mux_obj, addr, 1, bufs, MP_MACHINE_I2C_FLAG_STOP);
    return ret < 0 ? ESP_FAIL : ESP_OK;
}

#endif
