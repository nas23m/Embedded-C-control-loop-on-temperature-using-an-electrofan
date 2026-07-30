#include "main.h"
#include "stm_customDriver.hpp"
#include <string.h>
#include <stdio.h>

/* ── Constantes ── */
#define Tcmax     70
#define Tcmin     50
#define ARR       16000
#define TACH_T_80 160
#define PWM_80    12800
#define ARR_I     ((int32_t)ARR)

/* ── Objets périphériques ── */
stm_gpiogr    leds      (GPIOD);
stm_gpiogr    tim2pwm   (GPIOA);
stm_gpiogr    uart4gpio (GPIOC);
stm_gpiogr    adc_pin   (GPIOA);
stm_gpiogr    ICGPIO    (GPIOB);

stm_pwmtimer  pwmtimer  (TIM2);
stm_uart      usart_4   (UART4);
stm_adc_group my_adc_1  (ADC1, ADC_CHANNEL_1, ADC_CHANNEL_2);
FanTachometre myFan     (TIM3, TIM_CHANNEL_4, GPIO_PIN_1);
stm_basetimer timer10   (TIM10);

/* ── Variables globales ── */
volatile uint32_t LM35        = 0;
volatile uint32_t pot         = 0;
volatile bool     sem         = false;
volatile bool     degCK       = false;
volatile uint32_t fan_capture = 0;
volatile uint32_t pwm_image   = 0;
static   uint16_t mli         = 0;

/* ── Prototypes ── */
void     SystemClock_Config(void);
void     Error_Handler(void);
uint16_t regPID(uint32_t mesure, uint32_t consigne);

int main(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_TIM10_CLK_ENABLE();
    __HAL_RCC_UART4_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    HAL_Init();
    SystemClock_Config();

    /* LEDs PD12..PD15 */
    leds.stm_init(GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15,
                  GPIO_MODE_OUTPUT_PP, 0);

    /* PWM PA5 -> TIM2 CH1 */
    tim2pwm.stm_init(GPIO_PIN_5, GPIO_MODE_AF_PP, GPIO_AF1_TIM2);
    pwmtimer.pwminit(ARR, 1, TIM_COUNTERMODE_UP, TIM_CHANNEL_1);
    pwmtimer.startchannel(TIM_CHANNEL_1);
    pwmtimer.setdutycycle(TIM_CHANNEL_1, 0);

    /* Timer10 : 100 ms */
    timer10.baseinit(999, 1599);
    timer10.basestart_IT();
    timer10.basestart();
    HAL_NVIC_SetPriority(TIM1_UP_TIM10_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM1_UP_TIM10_IRQn);

    /* UART4 PC10=TX PC11=RX */
    uart4gpio.stm_init(GPIO_PIN_10 | GPIO_PIN_11, GPIO_MODE_AF_PP,
                       GPIO_AF8_UART4, GPIO_PULLUP);
    usart_4.init(9600);
    usart_4.send(std::string("System ready - Mode: CELSIUS\r\n"));

    /* ADC PA1=LM35  PA2=potentiometre */
    adc_pin.stm_init(GPIO_PIN_1 | GPIO_PIN_2, GPIO_MODE_ANALOG);
    my_adc_1.init();
    my_adc_1.enableInterrupt(2);

    /* Tachymetre TIM3 CH4 sur PB1 */
    ICGPIO.stm_init(GPIO_PIN_1, GPIO_MODE_AF_PP, GPIO_AF2_TIM3, GPIO_PULLUP);
    myFan.init(0xFFFF, 1599);
    myFan.startit(TIM_CHANNEL_4);
    HAL_NVIC_SetPriority(TIM3_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);

    /* Bouton PA0 */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin  = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);

    uint32_t Tm, Tc;
    char buf[80];

    while (1)
    {
        if (sem)
        {
            Tm = (LM35 * 330UL) >> 12;
            Tc = Tcmin + (((Tcmax - Tcmin) * pot) >> 12);

            uint32_t consigne_ccr = ((Tc - Tcmin) * ARR) / (Tcmax - Tcmin);
            mli = regPID(pwm_image, consigne_ccr);
            pwmtimer.setdutycycle(TIM_CHANNEL_1, mli);

            if (degCK)
            {
                uint32_t Tm_F = (9UL * Tm) / 5UL + 32UL;
                uint32_t Tc_F = (9UL * Tc) / 5UL + 32UL;
                uint32_t Tm_K = Tm + 273;
                uint32_t Tc_K = Tc + 273;
                sprintf(buf, "TM = %lu F | %lu K\r\n", (unsigned long)Tm_F, (unsigned long)Tm_K);
                usart_4.send(std::string(buf));
                sprintf(buf, "TC = %lu F | %lu K\r\n", (unsigned long)Tc_F, (unsigned long)Tc_K);
                usart_4.send(std::string(buf));
            }
            else
            {
                sprintf(buf, "TM = %lu C\r\n", (unsigned long)Tm);
                usart_4.send(std::string(buf));
                sprintf(buf, "TC = %lu C\r\n", (unsigned long)Tc);
                usart_4.send(std::string(buf));
            }

            sprintf(buf, "Tachy_val = %u\r\n", (unsigned int)mli);
            usart_4.send(std::string(buf));

            sem = false;
        }
    }
}

uint16_t regPID(uint32_t mesure, uint32_t consigne)
{
    const int32_t Kp = 2;
    int32_t erreur = (int32_t)consigne - (int32_t)mesure;
    int32_t sortie = (int32_t)mli + erreur * Kp;
    if (sortie > ARR_I) sortie = ARR_I;
    return (uint16_t)sortie;
}

extern "C" {

void EXTI0_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_FLAG(GPIO_PIN_0) == SET)
    {
        degCK = !degCK;
        leds.stm_toggle(GPIO_PIN_0);

        if (degCK)
            usart_4.send(std::string(">>> FAHRENHEIT | KELVIN <<<\r\n"));
        else
            usart_4.send(std::string(">>> CELSIUS    <<<\r\n"));

        __HAL_GPIO_EXTI_CLEAR_FLAG(GPIO_PIN_0);
    }
}

void TIM1_UP_TIM10_IRQHandler(void)
{
    static uint16_t joker = 0x1000;
    if (__HAL_TIM_GET_FLAG(timer10.getHandle(), TIM_FLAG_UPDATE) == SET)
    {
        if ((joker <<= 1) == 0) joker = 0x1000;
        leds.stm_toggle((GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15) & joker);
        my_adc_1.startNextConversionIT();
        __HAL_TIM_CLEAR_FLAG(timer10.getHandle(), TIM_FLAG_UPDATE);
    }
}

void ADC_IRQHandler(void)
{
    static bool cond = false;
    if (__HAL_ADC_GET_FLAG(my_adc_1.getHandle(), ADC_FLAG_EOC))
    {
        if (!cond)
        {
            LM35 = my_adc_1.getValue();
            my_adc_1.startNextConversionIT();
            cond = true;
        }
        else
        {
            pot  = my_adc_1.getValue();
            cond = false;
            sem  = true;
        }
        __HAL_ADC_CLEAR_FLAG(my_adc_1.getHandle(), ADC_FLAG_EOC);
    }
}

void TIM3_IRQHandler(void)
{
    if ((__HAL_TIM_GET_FLAG    (myFan.getHandle(), TIM_FLAG_CC4) != RESET) &&
        (__HAL_TIM_GET_IT_SOURCE(myFan.getHandle(), TIM_IT_CC4)  != RESET))
    {
        __HAL_TIM_CLEAR_IT(myFan.getHandle(), TIM_IT_CC4);
        fan_capture = TIM3->CCR4;
        __HAL_TIM_SET_COUNTER(myFan.getHandle(), 0);

        if (fan_capture > 0)
        {
            int32_t img = (int32_t)PWM_80
                        - ((int32_t)fan_capture - TACH_T_80) * 60;
            if (img > ARR_I) img = ARR_I;
            pwm_image = (uint32_t)img;
        }
    }
}

} /* extern "C" */

void SystemClock_Config(void) { /* ton config CubeMX */ }
void Error_Handler(void) { while (1); }
