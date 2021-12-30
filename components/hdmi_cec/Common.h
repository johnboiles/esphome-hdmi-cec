#ifndef COMMON_H__
#define COMMON_H__

#if defined(ARDUINO_ARCH_STM32F1) || defined(ARDUINO_ARCH_STM32F2) || defined(ARDUINO_ARCH_STM32F3) || defined(ARDUINO_ARCH_STM32F4)
# define STM32
#endif


#ifdef WIN32
#include <windows.h>
#include <stdio.h>
#include <assert.h>

#define ASSERT(x) assert(x)
void DbgPrint(const char* fmt, ...);

extern "C"
{
extern unsigned long micros();
extern void delayMicroseconds(unsigned int);
}

#elif defined(STM32)

#include "dwt.h"

#define ASSERT(x) ((void)0)
void DbgPrint(const char* fmt, ...);
#ifndef NULL
	#define NULL 0
#endif

#else

#define ASSERT(x) ((void)0)
void DbgPrint(const char* fmt, ...);
#ifndef NULL
	#define NULL 0
#endif


extern "C"
{
extern unsigned long micros();
extern void delayMicroseconds(unsigned int);
}

#endif

#endif // COMMON_H__
