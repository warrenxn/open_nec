/*
 * @Description: 
 * @Autor: warren xu
 * @Date: 2024-12-26 13:21:04
 * @LastEditors: warren xu
 * @LastEditTime: 2025-02-05 16:22:56
 */
#ifndef __NEC_HW_H__
#define __NEC_HW_H__

#include "nec.h"
#include "stm32f1xx_hal.h"

enum nec_hw_channel{
    NEC_HW_CHANNEL_LEFT = TIM_CHANNEL_2,
    NEC_HW_CHANNEL_RIGHT = TIM_CHANNEL_3,
};

int32_t nec_hw_send(const uint8_t *data, uint8_t len, enum nec_hw_channel channel);
int32_t nec_hw_send_raw(const uint32_t* data, uint8_t len, enum nec_hw_channel channel);

void nec_hw_init(const struct nec_callback *callback);
void nec_hw_process(void);

#endif
