#include "tildagon_i2c_manager.h"

#include "py/builtin.h"
#include "py/runtime.h"
#include "py/obj.h"
#include <string.h>

typedef struct _i2c_mgr_job_obj_t {
    mp_obj_base_t base;
    int handle;
} i2c_mgr_job_obj_t;

static uint16_t i2c_mgr_parse_period( mp_obj_t period_in )
{
    if ( period_in == MP_OBJ_NULL || period_in == mp_const_none )
    {
        return TILDAGON_I2C_MGR_PERIOD_OFF;
    }

    mp_int_t period_ms = mp_obj_get_int( period_in );
    if ( period_ms < (mp_int_t)TILDAGON_I2C_MGR_MIN_PERIOD_MS ||
            period_ms > (mp_int_t)TILDAGON_I2C_MGR_MAX_PERIOD_MS )
    {
        mp_raise_ValueError( MP_ERROR_TEXT("period must be None or 10..65534 ms") );
    }
    return (uint16_t)period_ms;
}

static mp_obj_t i2c_mgr_job_read_into( mp_obj_t self_in, mp_obj_t buf_in )
{
    i2c_mgr_job_obj_t *self = MP_OBJ_TO_PTR( self_in );
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise( buf_in, &bufinfo, MP_BUFFER_WRITE );

    int64_t seq = tildagon_i2c_mgr_read_into( self->handle, (uint8_t *)bufinfo.buf, bufinfo.len );
    if ( seq < 0 )
    {
        return mp_const_none;
    }
    return mp_obj_new_int_from_ull( (unsigned long long)seq );
}
static MP_DEFINE_CONST_FUN_OBJ_2( i2c_mgr_job_read_into_obj, i2c_mgr_job_read_into );

/* job.set_period(period_ms, force=False) */
static mp_obj_t i2c_mgr_job_set_period( size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args )
{
    enum { ARG_period_ms, ARG_force };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_period_ms, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_force, MP_ARG_BOOL, {.u_bool = false} },
    };
    i2c_mgr_job_obj_t *self = MP_OBJ_TO_PTR( pos_args[0] );
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all( n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args );

    uint16_t period_ms = i2c_mgr_parse_period( args[ARG_period_ms].u_obj );
    bool force = args[ARG_force].u_bool;
    return mp_obj_new_bool( tildagon_i2c_mgr_set_period( self->handle, period_ms, force ) );
}
static MP_DEFINE_CONST_FUN_OBJ_KW( i2c_mgr_job_set_period_obj, 2, i2c_mgr_job_set_period );

static mp_obj_t i2c_mgr_job_get_period( mp_obj_t self_in )
{
    i2c_mgr_job_obj_t *self = MP_OBJ_TO_PTR( self_in );
    uint16_t period_ms = tildagon_i2c_mgr_get_period( self->handle );
    if ( period_ms == TILDAGON_I2C_MGR_PERIOD_OFF )
    {
        return mp_const_none;
    }
    return mp_obj_new_int_from_uint( period_ms );
}
static MP_DEFINE_CONST_FUN_OBJ_1( i2c_mgr_job_get_period_obj, i2c_mgr_job_get_period );

static mp_obj_t i2c_mgr_job_run_once( mp_obj_t self_in )
{
    i2c_mgr_job_obj_t *self = MP_OBJ_TO_PTR( self_in );
    int16_t attempt = tildagon_i2c_mgr_run_once( self->handle );
    if ( attempt < 0 )
    {
        return mp_const_none;
    }
    return mp_obj_new_int( attempt );
}
static MP_DEFINE_CONST_FUN_OBJ_1( i2c_mgr_job_run_once_obj, i2c_mgr_job_run_once );

static mp_obj_t i2c_mgr_job_get_status( mp_obj_t self_in )
{
    i2c_mgr_job_obj_t *self = MP_OBJ_TO_PTR( self_in );
    uint8_t attempt;
    uint8_t status;
    if ( !tildagon_i2c_mgr_get_status( self->handle, &attempt, &status ) )
    {
        return mp_const_none;
    }

    mp_obj_t result[2] = {
        mp_obj_new_int( attempt ),
        mp_obj_new_int( status ),
    };
    return mp_obj_new_tuple( 2, result );
}
static MP_DEFINE_CONST_FUN_OBJ_1( i2c_mgr_job_get_status_obj, i2c_mgr_job_get_status );

static mp_obj_t i2c_mgr_job_unregister( mp_obj_t self_in )
{
    i2c_mgr_job_obj_t *self = MP_OBJ_TO_PTR( self_in );
    if ( self->handle >= 0 )
    {
        tildagon_i2c_mgr_unregister( self->handle );
        self->handle = -1;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1( i2c_mgr_job_unregister_obj, i2c_mgr_job_unregister );

static const mp_rom_map_elem_t i2c_mgr_job_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_read_into), MP_ROM_PTR(&i2c_mgr_job_read_into_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_period), MP_ROM_PTR(&i2c_mgr_job_set_period_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_period), MP_ROM_PTR(&i2c_mgr_job_get_period_obj) },
    { MP_ROM_QSTR(MP_QSTR_run_once), MP_ROM_PTR(&i2c_mgr_job_run_once_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_status), MP_ROM_PTR(&i2c_mgr_job_get_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_unregister), MP_ROM_PTR(&i2c_mgr_job_unregister_obj) },
};
static MP_DEFINE_CONST_DICT( i2c_mgr_job_locals_dict, i2c_mgr_job_locals_dict_table );

/* Not directly constructable from Python - only produced by add_job(). */
MP_DEFINE_CONST_OBJ_TYPE(
    i2c_mgr_job_type,
    MP_QSTR_Job,
    MP_TYPE_FLAG_NONE,
    locals_dict, &i2c_mgr_job_locals_dict
    );

/* i2c_mgr.add_job(port, addr, steps, period_ms=None)
 *
 * steps is a tuple/list of step tuples:
 *   (i2c_mgr.READ,  reg, len)
 *   (i2c_mgr.WRITE, reg, len, data)   # data: bytes-like of length len
 *   (i2c_mgr.CHECK, offset, mask, value)
 *
 * Returns a Job object, or None if the job table is full. */
static mp_obj_t i2c_mgr_add_job( size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args )
{
    enum { ARG_port, ARG_addr, ARG_steps, ARG_period_ms };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_port, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_addr, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_steps, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_period_ms, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all( n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args );

    if ( args[ARG_port].u_int < 0 || args[ARG_port].u_int > 7 )
    {
        mp_raise_ValueError( MP_ERROR_TEXT("port must be 0..7") );
    }
    if ( args[ARG_addr].u_int < 0 || args[ARG_addr].u_int > 0x7f )
    {
        mp_raise_ValueError( MP_ERROR_TEXT("address must be 7-bit") );
    }

    size_t num_steps;
    mp_obj_t *step_items;
    mp_obj_get_array( args[ARG_steps].u_obj, &num_steps, &step_items );

    if ( num_steps == 0 || num_steps > TILDAGON_I2C_MGR_MAX_STEPS )
    {
        mp_raise_ValueError( MP_ERROR_TEXT("bad step count") );
    }

    tildagon_i2c_mgr_step_t steps[TILDAGON_I2C_MGR_MAX_STEPS];
    memset( steps, 0, sizeof(steps) );

    for ( size_t i = 0; i < num_steps; i++ )
    {
        size_t item_len;
        mp_obj_t *item;
        mp_obj_get_array( step_items[i], &item_len, &item );
        if ( item_len < 3 )
        {
            mp_raise_ValueError( MP_ERROR_TEXT("bad step tuple") );
        }

        mp_int_t type = mp_obj_get_int( item[0] );
        mp_int_t a = mp_obj_get_int( item[1] );
        mp_int_t b = mp_obj_get_int( item[2] );
        if ( type < TILDAGON_I2C_MGR_STEP_READ || type > TILDAGON_I2C_MGR_STEP_CHECK ||
             a < 0 || a > UINT8_MAX || b < 0 || b > UINT8_MAX )
        {
            mp_raise_ValueError( MP_ERROR_TEXT("invalid step field") );
        }
        steps[i].type = (uint8_t)type;
        steps[i].a = (uint8_t)a;
        steps[i].b = (uint8_t)b;

        if ( type == TILDAGON_I2C_MGR_STEP_WRITE )
        {
            if ( item_len < 4 )
            {
                mp_raise_ValueError( MP_ERROR_TEXT("write step needs data") );
            }
            mp_buffer_info_t bufinfo;
            mp_get_buffer_raise( item[3], &bufinfo, MP_BUFFER_READ );
            if ( bufinfo.len > TILDAGON_I2C_MGR_MAX_STEP_BYTES || bufinfo.len != steps[i].b )
            {
                mp_raise_ValueError( MP_ERROR_TEXT("write step data length mismatch") );
            }
            memcpy( steps[i].data, bufinfo.buf, bufinfo.len );
        }
        else if ( type == TILDAGON_I2C_MGR_STEP_CHECK )
        {
            if ( item_len < 4 )
            {
                mp_raise_ValueError( MP_ERROR_TEXT("check step needs a value") );
            }
            mp_int_t value = mp_obj_get_int( item[3] );
            if ( value < 0 || value > UINT8_MAX )
            {
                mp_raise_ValueError( MP_ERROR_TEXT("check value must be 0..255") );
            }
            steps[i].data[0] = (uint8_t)value;
        }
    }

    uint16_t period_ms = i2c_mgr_parse_period( args[ARG_period_ms].u_obj );

    int handle = tildagon_i2c_mgr_register_steps( (uint8_t)args[ARG_port].u_int, (uint8_t)args[ARG_addr].u_int,
                                                   steps, (uint8_t)num_steps, period_ms );
    if ( handle < 0 )
    {
        return mp_const_none;
    }

    i2c_mgr_job_obj_t *job = mp_obj_malloc( i2c_mgr_job_obj_t, &i2c_mgr_job_type );
    job->handle = handle;
    return MP_OBJ_FROM_PTR( job );
}
static MP_DEFINE_CONST_FUN_OBJ_KW( i2c_mgr_add_job_obj, 3, i2c_mgr_add_job );

static const mp_rom_map_elem_t i2c_mgr_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_add_job), MP_ROM_PTR(&i2c_mgr_add_job_obj) },
    { MP_ROM_QSTR(MP_QSTR_READ), MP_ROM_INT(TILDAGON_I2C_MGR_STEP_READ) },
    { MP_ROM_QSTR(MP_QSTR_WRITE), MP_ROM_INT(TILDAGON_I2C_MGR_STEP_WRITE) },
    { MP_ROM_QSTR(MP_QSTR_CHECK), MP_ROM_INT(TILDAGON_I2C_MGR_STEP_CHECK) },
    { MP_ROM_QSTR(MP_QSTR_OFF), MP_ROM_INT(TILDAGON_I2C_MGR_PERIOD_OFF) },
    { MP_ROM_QSTR(MP_QSTR_MIN_PERIOD_MS), MP_ROM_INT(TILDAGON_I2C_MGR_MIN_PERIOD_MS) },
    { MP_ROM_QSTR(MP_QSTR_STATUS_IDLE), MP_ROM_INT(TILDAGON_I2C_MGR_STATUS_IDLE) },
    { MP_ROM_QSTR(MP_QSTR_STATUS_PENDING), MP_ROM_INT(TILDAGON_I2C_MGR_STATUS_PENDING) },
    { MP_ROM_QSTR(MP_QSTR_STATUS_SUCCESS), MP_ROM_INT(TILDAGON_I2C_MGR_STATUS_SUCCESS) },
    { MP_ROM_QSTR(MP_QSTR_STATUS_CHECK_ABORTED), MP_ROM_INT(TILDAGON_I2C_MGR_STATUS_CHECK_ABORTED) },
    { MP_ROM_QSTR(MP_QSTR_STATUS_I2C_ERROR), MP_ROM_INT(TILDAGON_I2C_MGR_STATUS_I2C_ERROR) },
};
static MP_DEFINE_CONST_DICT( i2c_mgr_globals, i2c_mgr_globals_table );

const mp_obj_module_t mp_module_i2c_mgr_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&i2c_mgr_globals,
};

MP_REGISTER_MODULE(MP_QSTR_i2c_mgr, mp_module_i2c_mgr_user_cmodule);
