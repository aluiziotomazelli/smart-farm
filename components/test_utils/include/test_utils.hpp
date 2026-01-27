#pragma once

#include <stddef.h>

#define TEST_MEMORY_LEAK_THRESHOLD (-500)

#ifdef __cplusplus
extern "C" {
#endif

void setUp(void);
void tearDown(void);
void app_main(void);

#ifdef __cplusplus
}
#endif
