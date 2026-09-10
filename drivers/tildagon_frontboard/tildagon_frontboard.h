#ifndef TILDAGON_FRONTBOARD_H
#define TILDAGON_FRONTBOARD_H

#include "aw9523b.h"

extern uint8_t iox_int;
extern const uint8_t ls1;
extern uint16_t board_identity;
extern void tildagon_frontboard_init( uint16_t board_id );
extern uint16_t tildagon_frontboard_get_model( void );
extern void cy8cmbrx_cb( aw9523b_device_t *dev, aw9523b_pin_t pin, uint8_t event );
#endif
