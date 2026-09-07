#pragma once
#include "nvs.h"
extern int fail_open, fail_read, fail_write, fail_commit;
void fixture_reset(void);
void fixture_corrupt(void);
