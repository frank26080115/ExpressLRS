#pragma once

void hbridge_init(void);
void hbridge_failsafe(void);
void hbridge_update(unsigned long now);
extern bool hbridge_isArmed();
