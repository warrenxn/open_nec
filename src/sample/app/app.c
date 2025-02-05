/*
 * @Description: 
 * @Autor: warren xu
 * @Date: 2025-02-05 16:29:17
 * @LastEditors: warren xu
 * @LastEditTime: 2025-02-05 17:07:39
 */

#include <stdlib.h>

#include "timer_server.h"
#include "nec_hw.h"

static void enter_critical(void){
    __disable_irq();
}

static void exit_critical(void){
    __enable_irq();
}

static void nec_error(const uint32_t* counts, uint32_t lens){
    // do something
}

static void nec_repeat(const uint8_t *data, uint8_t len){
    // do something
}

static void nec_recv(const uint8_t *data, uint8_t len){
    // do something
}

static void send_nec_timer(void* params){
    static uint8_t send_raw = 0;
    if (send_raw){
        const uint32_t send_raw_data[] = {9000,4500,560,560,560};
        const uint8_t send_raw_len = sizeof(send_raw_data)/sizeof(send_raw_data[0]);
        enum nec_hw_channel channel = NEC_HW_CHANNEL_LEFT;
        nec_hw_send_raw(send_raw_data, send_raw_len, channel);
        send_raw = 0;
    }else{
        const uint8_t send_nec_data[] = {0x01,0x02,0x03,0x04,0x05};
        const uint8_t send_nec_len = sizeof(send_nec_data)/sizeof(send_nec_data[0]);
        enum nec_hw_channel channel = NEC_HW_CHANNEL_RIGHT;
        nec_hw_send(send_nec_data, send_nec_len , channel);
        send_raw = 1;
    }
}

void process_init(void){
    struct timer_server_config timer_config = {
        .get_tick = HAL_GetTick,
        .enter_critical = enter_critical,
        .exit_critical = exit_critical
    };
    timer_system_init(&timer_config);
    struct nec_callback callback = {
        .error = nec_error,
        .repeat = nec_repeat,
        .recv = nec_recv
    };
    nec_hw_init(&callback);
    static TIMER_INSTANCE timer;
    create_timer_ex(&timer, 1000, 1 , send_nec_timer, NULL);
}

void process(void){
    timer_process();
    nec_hw_process();
}
