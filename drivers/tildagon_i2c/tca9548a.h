#ifndef _TCA9548A_H
#define _TCA9548A_H

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef unsigned char tca9548a_i2c_port_t;

typedef struct _tca9548a_i2c_mux {
  i2c_master_bus_handle_t bus_handle;
  i2c_master_dev_handle_t mux_device;
  uint16_t addr;
  SemaphoreHandle_t mtx;
  tca9548a_i2c_port_t active_port;
} tca9548a_i2c_mux_t;

#endif // _TCA9548A_H
