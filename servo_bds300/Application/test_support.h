#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H
#include "zf_common_headfile.h"
#include "IfxStm.h"
void test_start(const char *name);
uint32 test_ms(void);
uint8 test_get_command(uint8 *command);
#endif
