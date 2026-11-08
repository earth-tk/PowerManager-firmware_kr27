#include "WS2812B.hpp"

WS2812B::WS2812B(TIM_HandleTypeDef* htim, uint32_t tim_channel_x, DMA_HandleTypeDef* hdma){
    HTIM = htim;
    TIM_CHANNEL_X = tim_channel_x;
    HDMA =hdma;
}

void WS2812B::do_forwardRewrite(){
    //パートA
    if(rgb_buf_p < NUM_PIXELS) {
        for(uint_fast8_t i = 0; i < 8; i++) {
            pwm_buf[i     ] = ((rgb_buf[rgb_buf_p][1]>>(7-i))&1) ? HIGH : LOW;
            pwm_buf[i +  8] = ((rgb_buf[rgb_buf_p][0]>>(7-i))&1) ? HIGH : LOW;
            pwm_buf[i + 16] = ((rgb_buf[rgb_buf_p][2]>>(7-i))&1) ? HIGH : LOW;
        }
        rgb_buf_p++;
    //パートB
    } else if (rgb_buf_p < NUM_PIXELS + 2) {
        for(uint8_t i = 0; i < 24; i++){ pwm_buf[i] = 0;}
        rgb_buf_p++;
    }
}

void WS2812B::do_backRewrite(){
    //パートA
    if(rgb_buf_p < NUM_PIXELS) {
        for(uint_fast8_t i = 0; i < 8; ++i) {
            pwm_buf[i + 24] = ((rgb_buf[rgb_buf_p][1]>>(7-i))&1) ? HIGH : LOW;
            pwm_buf[i + 32] = ((rgb_buf[rgb_buf_p][0]>>(7-i))&1) ? HIGH : LOW;
            pwm_buf[i + 40] = ((rgb_buf[rgb_buf_p][2]>>(7-i))&1) ? HIGH : LOW;
        }
        rgb_buf_p++;
    //パートB
    } else if (rgb_buf_p < NUM_PIXELS + 2) {
        for(uint8_t i = 24; i < 48; i++){ pwm_buf[i] = 0;};
        rgb_buf_p++;
    //パートC
    } else {
        rgb_buf_p = 0;
        HAL_TIM_PWM_Stop_DMA(HTIM, TIM_CHANNEL_X);
    }
}

void WS2812B::set_rgb(uint8_t id, uint8_t r, uint8_t g,uint8_t b){
    rgb_buf[id][0]=r;
    rgb_buf[id][1]=g;
    rgb_buf[id][2]=b;
}

void WS2812B::show(){
    if(rgb_buf_p != 0 || HDMA->State != HAL_DMA_STATE_READY){//もし書き込み位置がリセットされていない、またはDMAが動いていたら停止し修正。
        HAL_TIM_PWM_Stop_DMA(HTIM, TIM_CHANNEL_X);
        rgb_buf_p = 0;
    }
    for(uint_fast8_t i = 0; i < 8; i++){//RESET用にPWM配列をすべて0で埋める
        pwm_buf[i     ] = 0;
        pwm_buf[i +  8] = 0;
        pwm_buf[i + 16] = 0;

        pwm_buf[i + 24] = 0;
        pwm_buf[i + 32] = 0;
        pwm_buf[i + 40] = 0;
    }
    HAL_TIM_PWM_Start_DMA(HTIM, TIM_CHANNEL_X, (uint32_t *)pwm_buf, 48);//PWM出力、およびDMA転送を開始。1ループ目ではRESET用のデータが送信されます。
    osDelay(8);//待つ
}

/* USER CODE BEGIN 0 */