#include "tildagon_i2c_manager.h"

#include "py/builtin.h"
#include "py/runtime.h"
#include "py/obj.h"
#include <string.h>

typedef struct _i2c_mgr_job_obj_t {
    mp_obj_base_t base;
    int handle;
} i2c_mgr_job_obj_t;

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

    uint32_t period_ms = ( args[ARG_period_ms].u_obj == mp_const_none ) ?
        TILDAGON_I2C_MGR_PERIOD_OFF : (uint32_t)mp_obj_get_int( args[ARG_period_ms].u_obj );
    bool force = args[ARG_force].u_bool;
    return mp_obj_new_bool( tildagon_i2c_mgr_set_period( self->handle, period_ms, force ) );
}
static MP_DEFINE_CONST_FUN_OBJ_KW( i2c_mgr_job_set_period_obj, 2, i2c_mgr_job_set_period );

static mp_obj_t i2c_mgr_job_get_period( mp_obj_t self_in )
{
    i2c_mgr_job_obj_t *self = MP_OBJ_TO_PTR( self_in );
    uint32_t period_ms = tildagon_i2c_mgr_get_period( self->handle );
    if ( period_ms == TILDAGON_I2C_MGR_PERIOD_OFF )
    {
        return mp_const_none;
    }
    return mp_obj_new_int_from_uint( period_ms );
}
static MP_DEFINE_CONST_FUN_OBJ_1( i2c_mgr_job_get_period_obj, i2c_mgr_job_get_period );

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
        steps[i].type = (uint8_t)type;
        steps[i].a = (uint8_t)mp_obj_get_int( item[1] );
        steps[i].b = (uint8_t)mp_obj_get_int( item[2] );

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
            steps[i].data[0] = (uint8_t)mp_obj_get_int( item[3] );
        }
    }

    uint32_t period_ms = ( args[ARG_period_ms].u_obj == MP_OBJ_NULL || args[ARG_period_ms].u_obj == mp_const_none ) ?
        TILDAGON_I2C_MGR_PERIOD_OFF : (uint32_t)mp_obj_get_int( args[ARG_period_ms].u_obj );

    int handle = tildagon_i2c_mgr_register_steps( (uint8_t)args[ARG_port].u_int, (uint16_t)args[ARG_addr].u_int,
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
};
static MP_DEFINE_CONST_DICT( i2c_mgr_globals, i2c_mgr_globals_table );

const mp_obj_module_t mp_module_i2c_mgr_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&i2c_mgr_globals,
};

MP_REGISTER_MODULE(MP_QSTR_i2c_mgr, mp_module_i2c_mgr_user_cmodule);
