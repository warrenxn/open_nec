/*
 * @Description: 
 * @Autor: warren xu
 * @Date: 2024-12-26 13:21:35
 * @LastEditors: warren xu
 * @LastEditTime: 2025-01-17 15:37:34
 */
#include "nec_hw.h"
#include "main.h"

struct nec_message{
    uint32_t channel;
};

#define NEC_CARRIER_CCR_COUNT_H         625
#define NEC_CARRIER_CCR_COUNT_L         0
#define NEC_CARRIER_TIME                26.32
#define GET_ARR_COUNT(x)                (x/NEC_CARRIER_TIME-1)

static struct {
    uint32_t channel;
    uint32_t len;
    uint32_t ptr;
    uint32_t count[NEC_DATA_MAX*8*2];
}send_arr_value;

static const uint32_t SEND_ARR_VALUE_BUF_SIZE = (sizeof(send_arr_value.count)/sizeof(send_arr_value.count[0]));
static const uint32_t NEC_CARRIER_IDLE_COUNT = GET_ARR_COUNT(10000);

static struct nec_instance* used_nec = NULL;


static uint8_t nec_check_private(const uint8_t *data, uint8_t len){
    uint8_t is_legal = 0;
    if(len%2 != 0){
        if (len >= 3){
            if ((data[0] ^ data[1]) == 0xff){
                uint8_t temp = data[2]&0x0F;
                if((temp^(data[2]>>4)) == 0x0f){
                    if ((((temp+1)<<1)+1) == len){
                        is_legal = 1;
                        for (size_t i = 0; i < temp; i++){
                            if ((data[i*2+3] ^ data[i*2+4]) != 0xff){
                                is_legal = 0;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    return is_legal;
}

static int32_t nec_encoding_private(const uint8_t *data, uint8_t len, uint8_t *buf, uint8_t buf_len){
    int ret = -1;
    if (buf && data && len){
        ret = -2;
        if (buf_len >= ((len<<1)+1)){
            ret = ((len<<1)+1);
            uint8_t temp = len-1;
            buf[0] = data[0];
            buf[1] = ~data[0];
            buf[2] = ((((~temp) << 4) & 0xF0) | (temp & 0x0F));
            for (size_t i = 1; i < len; i++){
                buf[i*2+3] = data[i];
                buf[i*2+4] = ~data[i];
            }
        }
    }
		return ret;
}

static int32_t nec_decoding_private(const uint8_t *data, uint8_t len, uint8_t *buf, uint8_t buf_len){
    int32_t ret = -1;
    if (buf && data && len){
        ret = -2;
        if((len>>1) <= buf_len){
            ret = (len-1)>>1;
            buf[0] = data[0];
            for (size_t i = 1; i < ret; i++){
                buf[i] = data[i*2+1];
            }
        }
        
    }
    return ret;
}

static int32_t pwm_send(const uint32_t* counts, uint32_t lens, void *arg){
    int32_t ret = -1;
    struct nec_message* message = (struct nec_message*)arg;
    if (message){
        ret = -2;
        if ((message->channel == TIM_CHANNEL_2)||(message->channel == TIM_CHANNEL_3)){
            ret = -3;
            if ((lens < SEND_ARR_VALUE_BUF_SIZE)\
                &&((lens%2 != 0))&&((lens+1) < SEND_ARR_VALUE_BUF_SIZE)){
                ret = 0;
                if (send_arr_value.ptr >= send_arr_value.len){
                    send_arr_value.channel = message->channel;
                    int32_t temp = 0;
                    for (size_t i = 0; i < lens; i++){
                        temp = GET_ARR_COUNT(counts[i]);
                        if (temp >= 0){
                            send_arr_value.count[i] = temp;
                        }else{
                            ret = -4;
                            break;
                        }
                    }
                    if ((lens%2) != 0){
                        send_arr_value.count[lens] = NEC_CARRIER_IDLE_COUNT;
                        lens += 1;
                    }
                    HAL_TIM_IC_Stop_IT(&htim4, TIM_CHANNEL_2);
                    if (ret == 0){
                        __disable_irq();
                        send_arr_value.ptr = 0;
                        send_arr_value.len = lens;
                        __enable_irq();
                        ret = lens;
                    }
                }
            }
        }
    }
    return ret;
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim){
    if (htim == &htim3){
        if (send_arr_value.ptr < send_arr_value.len){
            if (send_arr_value.ptr%2){
                __HAL_TIM_SET_COMPARE(&htim2, send_arr_value.channel, NEC_CARRIER_CCR_COUNT_L);
                __HAL_TIM_SET_AUTORELOAD(&htim3, send_arr_value.count[send_arr_value.ptr]);
            }else{
                __HAL_TIM_SET_COMPARE(&htim2, send_arr_value.channel, NEC_CARRIER_CCR_COUNT_H);
                __HAL_TIM_SET_AUTORELOAD(&htim3, send_arr_value.count[send_arr_value.ptr]);
            }
            send_arr_value.ptr++;
            if (send_arr_value.ptr >= send_arr_value.len){
                __HAL_TIM_SET_CAPTUREPOLARITY(&htim4, TIM_CHANNEL_2, TIM_INPUTCHANNELPOLARITY_FALLING);
                HAL_TIM_IC_Start_IT(&htim4, TIM_CHANNEL_2);
            }
        }else{
            __HAL_TIM_SET_AUTORELOAD(&htim3, NEC_CARRIER_IDLE_COUNT);
            __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, NEC_CARRIER_CCR_COUNT_L);
            __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, NEC_CARRIER_CCR_COUNT_L);
        }
    }
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim){
	if (htim == &htim4){
        uint32_t count = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_2);
        uint32_t ccer = htim->Instance->CCER;
        htim->Instance->CCER &= ~TIM_CCER_CC2E;
        uint8_t is_logic_high = 1;
        if (htim->Instance->CCER&(0x01<<5)){
            is_logic_high = 0;
            ccer &= ~(0x01<<5);
        }else{
            ccer |= 0x01<<5;
        }
        htim->Instance->CCER = ccer;
        nec_interrupt(count, is_logic_high, used_nec);
	}
}

void nec_hw_init(const struct nec_callback *callback){
    static struct nec_message message;
    static struct nec_instance nec;
    struct nec_config config = {
        .start_count_h = 9000,
        .start_count_l = 4500,
        .repeat_count_l = 2250,
        .data_count_h = 560,
        .zero_count_l = 560,
        .one_count_l = 1690,
        .end_count_l = 560,
        
        .hw.send = pwm_send,
        .hw.nec_check = nec_check_private,
        .hw.nec_encoding = nec_encoding_private,
        .hw.nec_decoding = nec_decoding_private
    };

    config.callback = *callback;

    used_nec = nec_regist(&config, &nec, &message);

    HAL_TIM_IC_Start_IT(&htim4, TIM_CHANNEL_2);

    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, NEC_CARRIER_CCR_COUNT_H);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, NEC_CARRIER_CCR_COUNT_L);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
    
    __HAL_TIM_SET_AUTORELOAD(&htim3, NEC_CARRIER_IDLE_COUNT);
    HAL_TIM_Base_Start(&htim3);
    __HAL_TIM_ENABLE_IT(&htim3,TIM_IT_UPDATE);
}

void nec_hw_process(void){
    static TIMER_INSTANCE timer;    
    static uint8_t send_l = 1;
    if (is_timer_done(&timer)|| is_timer_idle(&timer)){
        struct nec_message _message = {.channel = TIM_CHANNEL_2};
        const uint32_t nec_data_l[] = {9000,4500,560,560,560};
        const uint32_t nec_data_r[] = {9000,4500,560,1690,560};
        deinit_timer(&timer);
        create_timer(&timer, 500);
        if (send_l){
            if (nec_send_raw(used_nec, nec_data_l, 5, _message) > 0){
                send_l = 0;
            }
        }else{
            _message.channel = TIM_CHANNEL_3;
            if (nec_send_raw(used_nec, nec_data_r, 5, _message) > 0){
                send_l = 1;
            }
        }
    }
    nec_process();
}

int32_t nec_hw_send(const uint8_t *data, uint8_t len, enum nec_hw_channel channel){
    int32_t ret = -1;
    if ((channel == NEC_HW_CHANNEL_LEFT) || (channel == NEC_HW_CHANNEL_RIGHT)){
        struct nec_message _message;
        _message.channel = channel;
        ret = nec_send(used_nec, data, len, _message);
        if (ret < 0){
            ret -= 10;
        }
    }
    return ret;
}

int32_t nec_hw_send_raw(const uint32_t* data, uint8_t len, enum nec_hw_channel channel){
    int32_t ret = -1;
    if ((channel == NEC_HW_CHANNEL_LEFT) || (channel == NEC_HW_CHANNEL_RIGHT)){
        struct nec_message _message;
        _message.channel = channel;
        ret = nec_send_raw(used_nec, data, len, _message);
        if (ret < 0){
            ret -= 10;
        }
    }
    return ret;
}
