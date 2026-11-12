#include <stdint.h>
#include <stdbool.h>
#define __DSB() do{}while(0)
#define __ISB() do{}while(0)
typedef struct { volatile uint32_t scr, clr; } gpio_type;
extern gpio_type GPIOA_s; 
#define GPIOA (&GPIOA_s)
void gpio_bits_set(gpio_type *g, uint16_t p); void gpio_bits_reset(gpio_type *g, uint16_t p);
void NVIC_SystemReset(void);
#define __disable_irq() do{}while(0)
#define __enable_irq() do{}while(0)
