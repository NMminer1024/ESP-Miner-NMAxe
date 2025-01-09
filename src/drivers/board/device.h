#ifndef __DEVICE_H__
#define __DEVICE_H__

#if defined(NM_AXE_1366)
#include "nmaxe_v1x.h"
#elif defined(NM_AXE_1370)
#include "nmaxe_gamma.h"





#else
#error "No device defined"
#endif

#endif // __DEVICE_H__
