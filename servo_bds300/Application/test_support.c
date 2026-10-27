#include "test_support.h"
static uint32 stm_hz;
void test_start(const char *name)
{
    clock_init();
    debug_init();
    stm_hz = (uint32)IfxStm_getFrequency(&MODULE_STM0);
    cpu_wait_event_ready();
    printf("\r\n%s | UART0 115200 8N1\r\n", name);
}
uint32 test_ms(void)
{
    /* Use the full STM counter: the library's 32-bit 10 ns timer wraps in 43 s. */
    return (uint32)(IfxStm_get(&MODULE_STM0) / (stm_hz / 1000u));
}
uint8 test_get_command(uint8 *command)
{
    return debug_read_ring_buffer(command, 1u) == 1u;
}
