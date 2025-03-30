#ifndef _MONITOR_H_
#define _MONITOR_H_
#include <Arduino.h>

void monitor_thread_entry(void *args);
void swarm_thread_entry(void *args);
void daemon_thread_entry(void *args);
#endif