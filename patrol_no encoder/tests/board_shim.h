/* Intentionally provides NO encoder or IMU symbols. */
#define ATOM0_CH0_P21_2 0
#define ATOM0_CH2_P21_4 1
#define ATOM1_CH1_P33_9 2
#define P21_3 3
#define P21_5 4
#define P11_3 5
#define GPO 1
#define GPO_PUSH_PULL 1
#define PWM_DUTY_MAX 10000
void gpio_init(int pin,int mode,int level,int type);
int gpio_get_level(int pin);
void pwm_init(int channel,uint32 hz,uint32 duty);
void pwm_set_duty(int channel,uint32 duty);
uint32 interrupt_global_disable(void);
void interrupt_global_enable(uint32 state);
uint32 system_getval_ms(void);
