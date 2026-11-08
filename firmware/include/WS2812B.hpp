#pragma once 

#include "main.h"

class WS2812B
{
private:
    uint32_t TIM_CHANNEL_X;
    TIM_HandleTypeDef* HTIM;
    DMA_HandleTypeDef* HDMA;

    #define HIGH 13//ビット値1
    #define LOW 7 //ビット値0
    #define NUM_PIXELS 200 //NeoPixelの数

    uint8_t rgb_buf[NUM_PIXELS][3]; //色データの配列。1つのNeoPixelにつきGRBの3色分のデータがあります。
    uint32_t pwm_buf[48]={0}; //PWM配列
    uint16_t rgb_buf_p = 0; //色配列のどこまで書き込みがおわったかを記録する変数

public:
    WS2812B(TIM_HandleTypeDef* htim, uint32_t tim_channel_x, DMA_HandleTypeDef* hdma);
    void do_forwardRewrite();
    void do_backRewrite();
    void set_rgb(uint8_t id, uint8_t r, uint8_t g, uint8_t b);
    void show();
    void clear();
};