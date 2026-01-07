#ifndef DOOMGENERIC_H
#define DOOMGENERIC_H

#include "user.h"
#include <stdarg.h>

// Define standard types that xv6 lacks (but standard stdint.h usually has)
// We use #ifndef checks to avoid conflicts if they are defined elsewhere
#ifndef _STDINT_H
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned long  uint64_t;
typedef char           int8_t;
typedef short          int16_t;
typedef int            int32_t;
typedef long           int64_t;
#endif

#ifndef EISDIR
#define EISDIR 21
#endif

// Doom-specific types
// If 'byte' is already defined in doomtype.h, we shouldn't redefine it.
// But usually, it's safe to typedef it here if we removed the conflicting headers.
typedef unsigned char byte;
typedef uint32_t   pixel_t;

// Resolution (320x200 is native Doom)
#define DOOMGENERIC_RESX 320
#define DOOMGENERIC_RESY 200

// Public Functions
void doomgeneric_Create(int argc, char *argv[]);
void doomgeneric_Tick(void);

// Logic provided by the backend (doomgeneric_xv6.c)
uint32_t DG_GetTicksMs(void);
void DG_SleepMs(uint32_t ms);
void DG_DrawFrame(void);
int DG_GetKey(int *pressed, unsigned char *key);
void DG_SetWindowTitle(const char * title);
int snprintf(char *buf, uint size, const char *fmt, ...);
int vsnprintf(char *buf, uint size, const char *fmt, va_list ap);
int strncasecmp(const char *s1, const char *s2, int n);

// External global buffer (defined in doomgeneric.c)
extern uint32_t* DG_ScreenBuffer;

// Map fprintf to printf, ignoring the stream argument (stderr)
#undef fprintf
#define fprintf(stream, ...) printf(__VA_ARGS__)

void* DG_Malloc(unsigned int size);
void  DG_Free(void* ptr);
void* DG_Realloc(void* ptr, unsigned int new_size);

#define malloc  DG_Malloc
#define free    DG_Free
#define realloc DG_Realloc

#endif