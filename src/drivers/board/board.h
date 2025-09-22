#ifndef __NM_BOARD_H_
#define __NM_BOARD_H_

#if defined(BOARD_MODEL_NMAXE)
#include "NMAxe.h"
#elif defined(BOARD_MODEL_NMAXE_GAMMA)
#include "NMAxeGamma.h"
#elif defined(BOARD_MODEL_NMQAxe_PLUSPLUS)
#include "NMQAxePlusPlus.h"
#else
#error "No board model defined"
#endif

#endif