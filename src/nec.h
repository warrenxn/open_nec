/*
 * @Description: 
 * @Autor: warren xu
 * @Date: 2024-12-25 15:11:03
 * @LastEditors: warren xu
 * @LastEditTime: 2025-02-05 13:25:40
 */
#ifndef __NEC_H__
#define __NEC_H__

#include <stdint.h>
#include "list.h"
#include "timer_server.h"

#define NEC_DATA_MAX            16
#define NEC_RECV_RAW_MAX        8

enum nec_state{
    NEC_STATE_WAIT_START_H=0,
    NEC_STATE_WAIT_START_L,
    NEC_STATE_WAIT_REPEAT_H,
    NEC_STATE_WAIT_DATA_H,
    NEC_STATE_WAIT_DATA_L,
    NEC_STATE_ERROR
};

struct nec_hw_interface{
    int32_t (*send)(const uint32_t* counts, uint32_t lens, void *arg);
    void (*enter_critical)(void);
    void (*exit_critical)(void);

    uint8_t (*nec_check)(const uint8_t *data, uint8_t len);
    int32_t (*nec_encoding)(const uint8_t *data, uint8_t len, uint8_t *buf, uint8_t buf_len);
    int32_t (*nec_decoding)(const uint8_t *data, uint8_t len, uint8_t *buf, uint8_t buf_len);
};

struct nec_callback{
    void (*error)(const uint32_t* counts, uint32_t lens);
    void (*repeat)(const uint8_t *data, uint8_t len);
    void (*recv)(const uint8_t *data, uint8_t len);
};


struct nec_config{
    uint32_t start_count_h;
    uint32_t start_count_l;
    uint32_t repeat_count_l;
    uint32_t repeat_count_h;

    uint32_t data_count_h;
    uint32_t zero_count_l;
    uint32_t one_count_l;

    uint32_t end_count_l;

    struct nec_hw_interface hw;
    struct nec_callback callback;
};

struct nec_instance{
    struct list_head list;
    enum nec_state state;
    
    struct{
        uint8_t buf[NEC_DATA_MAX];
        uint32_t pos;
    }recv_nec;
    struct{
        uint32_t buf[NEC_RECV_RAW_MAX];
        uint32_t pos;
    }recv_raw;
    uint8_t send_raw_flag;
    union{
        struct{
            uint8_t buf[NEC_DATA_MAX];
        }nec;
        struct {
            uint32_t buf[5];
        }raw;
    }send_data;
    uint8_t send_len;

    uint8_t repeat_flag;
    uint8_t recv_flag;

    struct nec_config config;
    TIMER_INSTANCE timer;
    void* arg_instance;
};
int32_t _nec_send(struct nec_instance *instance, const uint8_t *data, uint8_t len, void *arg, int32_t arg_size);
int32_t _nec_send_raw(struct nec_instance *instance, const uint32_t* data, uint8_t len, void *arg, int32_t arg_size);


inline uint8_t is_nec_recv_idle(struct nec_instance* instance){
    uint8_t is_idle = 0;
    if (instance){
        is_idle = is_timer_done(&instance->timer)?1:(is_timer_idle(&instance->timer)?1:0);
    }
    return is_idle;
}
struct nec_instance* nec_regist(const struct nec_config *config, struct nec_instance *instance ,void* arg_instance);
#define nec_send(instance, data, len, arg) _nec_send(instance, data, len, (void*)&arg, sizeof(arg))
#define nec_send_raw(instance, data, len, arg) _nec_send_raw(instance, data, len, (void*)&arg, sizeof(arg))
void nec_interrupt(uint32_t count, uint8_t is_high, struct nec_instance *instance);
void nec_process(void);

#endif
