/*
 * @Description: 
 * @Autor: warren xu
 * @Date: 2024-12-25 15:10:47
 * @LastEditors: warren xu
 * @LastEditTime: 2025-01-02 13:46:14
 */
#include "nec.h"
#include <string.h>
#include <stdlib.h>

#define NEC_RECV_RAW_FLAG_START                 0x01

#define NEC_RECV_RAW_FLAG_ERROR_START           0x02
#define NEC_RECV_RAW_FLAG_ERROR_END             0x04
#define NEC_RECV_RAW_FLAG_ERROR_MASK            0x06

#define NEC_WAIT_IDLE_TIME      10 //ms

#define __is_data_legal(data, check_data, limit_percent) (abs((int32_t)data-(int32_t)check_data) < (data*limit_percent/100))

#define is_data_legal(data, check_data) __is_data_legal(data, check_data, 90)

struct list_head instance_list_head = {.next = NULL, .prev = NULL};

static uint8_t nec_check_default(const uint8_t *data, uint8_t len){
    uint8_t is_legal = 0;
    if((len%2 == 0) && len){
        uint8_t _len = len/2;
        is_legal = 1;
        for (size_t i = 0; i < _len; i++){
            if((data[i] ^ data[2*i+1]) != 0xff){
                is_legal = 0;
                break;
            }
        }
    }
    return is_legal;
}

static int32_t nec_encoding_default(const uint8_t *data, uint8_t len, uint8_t *buf, uint8_t buf_len){
    int32_t ret = -1;
    if (buf && data && len){
        ret = -2;
        if (buf_len <= len*2){
            ret = len*2;
            for (size_t i = 0; i < len; i++){
                buf[i*2] = data[i];
                buf[i*2+1] = ~data[i];
            }
        }
    }
    return ret;
}

static int32_t nec_decoding_default(const uint8_t *data, uint8_t len, uint8_t *buf, uint8_t buf_len){
    int32_t ret = -1;
    if (buf && data && len){
        ret = -2;
        if (len%2 == 0){
            ret = len/2;
            for (size_t i = 0; i < len/2; i++){
                if ((data[i*2] ^ data[i*2+1]) != 0xff){
                    ret = -3;
                    break;
                }
                buf[i] = data[i*2];
            }
        }
    }
    return ret;
}

struct nec_instance* nec_regist(const struct nec_config *config, struct nec_instance *instance ,void* arg_instance){
    if (instance && config){
        instance->config = *config;
        if (!instance->config.hw.nec_check){
            instance->config.hw.nec_check = nec_check_default;
        }
        if (!instance->config.hw.nec_encoding){
            instance->config.hw.nec_encoding = nec_encoding_default;
        }
        if (!instance->config.hw.nec_decoding){
            instance->config.hw.nec_decoding = nec_decoding_default;
        }
        instance->arg_instance = arg_instance;
        if (instance_list_head.next == NULL){
            INIT_LIST_HEAD(&instance_list_head);
        }
        list_add_tail(&instance->list, &instance_list_head);
    }
    return instance;
}

int32_t _nec_send(struct nec_instance *instance, const uint8_t *data, uint8_t len, void *arg, int32_t arg_size){
    int32_t ret = -1;
    if (instance && data && len){
        ret = -2;
        if ((arg && instance->arg_instance)
        || (!arg && !instance->arg_instance)){
            ret = -3;
            if (instance->send_len == 0){
                ret = 0;
                int32_t send_len = instance->config.hw.nec_encoding(data, len, instance->send_data.nec.buf, NEC_DATA_MAX);
                if (send_len > 0){
                    ret = send_len;
                    instance->send_raw_flag = 0;
                    instance->send_len = send_len;
                    if (instance->arg_instance){
                        memcpy(instance->arg_instance, arg, arg_size);
                    }
                }
            }
        }
    }
    return ret;
}

int32_t _nec_send_raw(struct nec_instance *instance, const uint32_t* data, uint8_t len, void *arg, int32_t arg_size){
    int32_t ret = -1;
    if (instance && data && len){
        ret = -2;
        if ((arg && instance->arg_instance)
        || (!arg && !instance->arg_instance)){
            ret = -3;
            const uint32_t max_len = sizeof(instance->send_data.raw.buf)/sizeof(instance->send_data.raw.buf[0]);
            if(len <= max_len){
                ret = 0;
                if (instance->send_len == 0){
                    instance->send_raw_flag = 1;
                    ret = 2;
                    instance->send_len = len;
                    memcpy(instance->send_data.raw.buf, data, len*sizeof(uint32_t));
                    if (instance->arg_instance){
                        memcpy(instance->arg_instance, arg, arg_size);
                    }
                }
            }
        }
    }
    return ret;
}

void nec_interrupt(uint32_t count, uint8_t is_high, struct nec_instance *instance){
    if (instance){
        is_high = is_high?1:0;
        if(!(instance->recv_flag&NEC_RECV_RAW_FLAG_ERROR_MASK)){
            switch (instance->state)
            {
            case NEC_STATE_WAIT_START_H:
                if (is_high){
                    if (is_data_legal(instance->config.start_count_h, count)){
                        instance->state = NEC_STATE_WAIT_START_L;
                    }else{
                        instance->recv_flag |= NEC_RECV_RAW_FLAG_ERROR_START;
                        instance->state = NEC_STATE_WAIT_START_H;
                    }
                    instance->recv_raw.pos = 0;
                }
                break;
            case NEC_STATE_WAIT_START_L:
                if ((is_data_legal(instance->config.start_count_l, count))&&(!is_high)){
                    instance->state = NEC_STATE_WAIT_DATA_H;
                    instance->recv_nec.pos = 0;
                    memset(instance->recv_nec.buf, 0, sizeof(instance->recv_nec.buf));
                }else if((is_data_legal(instance->config.repeat_count_l, count))&&(!is_high)){
                    instance->state = NEC_STATE_WAIT_REPEAT_H;
                }else{
                    instance->recv_flag |= NEC_RECV_RAW_FLAG_ERROR_START;
                    instance->state = NEC_STATE_WAIT_START_H;
                }
                break;
            case NEC_STATE_WAIT_REPEAT_H:
                if((is_data_legal(instance->config.repeat_count_h, count))&&(is_high)){
                    instance->repeat_flag = 1;
                    instance->state = NEC_STATE_WAIT_START_H;
                }else{
                    instance->recv_flag |= NEC_RECV_RAW_FLAG_ERROR_START;
                    instance->state = NEC_STATE_WAIT_START_H;
                }
                break;
            case NEC_STATE_WAIT_DATA_H:
                if ((is_data_legal(instance->config.data_count_h, count))&&(is_high)){
                    instance->state = NEC_STATE_WAIT_DATA_L;
                    deinit_timer(&instance->timer);
                    create_timer(&instance->timer, NEC_WAIT_IDLE_TIME);
                }else{
                    instance->recv_flag |= NEC_RECV_RAW_FLAG_ERROR_START;
                    instance->state = NEC_STATE_WAIT_START_H;
                }
                break;        
            case NEC_STATE_WAIT_DATA_L:
                do{
                    uint8_t data_index = instance->recv_nec.pos/8;
                    if ((is_data_legal(instance->config.zero_count_l, count))&&(!is_high)){
                        instance->recv_nec.buf[data_index] &= ~(1<<(instance->recv_nec.pos%8));
                        instance->recv_nec.pos++;
                        instance->state = NEC_STATE_WAIT_DATA_H;
                    }else if ((is_data_legal(instance->config.one_count_l, count))&&(!is_high)){
                        instance->recv_nec.buf[data_index] |= 1<<(instance->recv_nec.pos%8);
                        instance->recv_nec.pos++;
                        instance->state = NEC_STATE_WAIT_DATA_H;
                    }else{
                        instance->recv_flag |= NEC_RECV_RAW_FLAG_ERROR_START;
                        instance->state = NEC_STATE_WAIT_START_H;
                    }
                } while (0);
                break;
            default:
                break;
            }
        }
        if(instance->recv_flag&NEC_RECV_RAW_FLAG_ERROR_MASK){
            instance->recv_nec.pos = 0;
        }
        instance->recv_raw.buf[instance->recv_raw.pos] = count;
        instance->recv_raw.pos++;
        if (instance->recv_raw.pos >= NEC_RECV_RAW_MAX){
            if(instance->recv_flag & NEC_RECV_RAW_FLAG_ERROR_START){
                instance->recv_flag |= NEC_RECV_RAW_FLAG_ERROR_END;
            }else{
                instance->recv_raw.pos = 0;
            }
        }
        deinit_timer(&instance->timer);
        create_timer(&instance->timer, NEC_WAIT_IDLE_TIME);
    }
}

void nec_process(void){
    struct list_head* data;
    list_for_each(data, &instance_list_head){
        struct nec_instance *instance = list_entry(data, struct nec_instance, list);
        if (is_timer_done(&instance->timer)){
            uint8_t recv_len = instance->recv_nec.pos/8;
            if(instance->config.hw.enter_critical){instance->config.hw.enter_critical();}
            deinit_timer(&instance->timer);
            if(instance->config.hw.exit_critical){instance->config.hw.exit_critical();}
            if (instance->recv_flag & NEC_RECV_RAW_FLAG_ERROR_MASK){
                if (instance->config.callback.error){
                    instance->config.callback.error(instance->recv_raw.buf, instance->recv_raw.pos);
                }
            }else if(instance->config.hw.nec_check(instance->recv_nec.buf, recv_len)){
                if (instance->config.callback.recv){
                    const uint8_t buf_len = sizeof(instance->recv_nec.buf)/2;
                    uint8_t buf[buf_len];
                    int32_t data_len = instance->config.hw.nec_decoding(instance->recv_nec.buf, recv_len, buf, buf_len);
                    instance->config.callback.recv(buf, data_len);
                }
            }else{
                if (instance->config.callback.error){
                    instance->config.callback.error(instance->recv_raw.buf, instance->recv_raw.pos);
                }
            }
            if(instance->config.hw.enter_critical){instance->config.hw.enter_critical();}
            instance->recv_flag = 0;
						instance->state = NEC_STATE_WAIT_START_H;
            if(instance->config.hw.exit_critical){instance->config.hw.exit_critical();}
        }
        if (instance->recv_flag & NEC_RECV_RAW_FLAG_ERROR_END){
            if (instance->config.callback.error){
                instance->config.callback.error(instance->recv_raw.buf, instance->recv_raw.pos);
            }
            if(instance->config.hw.enter_critical){instance->config.hw.enter_critical();}
            instance->recv_flag = 0;
            if(instance->config.hw.exit_critical){instance->config.hw.exit_critical();}
        }
        if (instance->repeat_flag){
            if (instance->config.callback.repeat){
                uint8_t recv_len = instance->recv_nec.pos/8;
                if(instance->config.hw.nec_check(instance->recv_nec.buf, recv_len)){
                    const uint8_t buf_len = sizeof(instance->recv_nec.buf)/2;
                    uint8_t buf[buf_len];
                    int32_t data_len = instance->config.hw.nec_decoding(instance->recv_nec.buf, recv_len, buf, buf_len);
                    instance->config.callback.repeat(buf, data_len);
                }
            }
            if(instance->config.hw.enter_critical){instance->config.hw.enter_critical();}
            instance->repeat_flag = 0;
            instance->recv_flag = 0;
            if(instance->config.hw.exit_critical){instance->config.hw.exit_critical();}
        }
        if (!instance->recv_flag && (instance->state == NEC_STATE_WAIT_START_H)\
            &&(instance->send_len != 0)){
            if (instance->config.hw.send){
                if (instance->send_raw_flag){
                    if(instance->config.hw.send(instance->send_data.raw.buf, instance->send_len, instance->arg_instance) != 0){
                        instance->send_len = 0;
                    }
                }else{
                    const uint32_t max_len = sizeof(instance->recv_nec.buf)*8*2;
                    uint32_t counts[max_len];
                    uint8_t count_lens = 3+instance->send_len*8*2;
                    counts[0] = instance->config.start_count_h;
                    counts[1] = instance->config.start_count_l;
                    for (size_t index = 0; index < instance->send_len; index++){
                        uint32_t base_index = 2+(index*8*2);
                        for (size_t pos = 0; pos < 8; pos++){
                            counts[base_index+(pos*2)] = instance->config.data_count_h;
                            if (instance->send_data.nec.buf[index] & (1<<pos)){
                                counts[base_index+(pos*2)+1] = instance->config.one_count_l;
                            }else{
                                counts[base_index+(pos*2)+1] = instance->config.zero_count_l;
                            }
                        }
                    }
                    counts[count_lens-1] = instance->config.data_count_h;
                    if(instance->config.hw.send(counts, count_lens, instance->arg_instance) >= 0){
                        instance->send_len = 0;
                    }
                }
            }
        }
    }
}
