#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "doomgeneric.h"

#define SCREEN_SIZE = 320 * 200 * 4

// Global GPU pointer
void *screen_ptr = 0;

void DG_DrawFrame()
{
    if (screen_ptr == 0) return;
    memmove(screen_ptr, DG_ScreenBuffer, 320 * 200 * 4);
    flushfb();
}


void DG_SleepMs(uint32_t ms)
{
    sleep(ms / 10);
}


uint32_t DG_GetTicksMs()
{
    return uptime() * 10;
}

// TODO: implement this
int DG_GetKey(int *pressed, unsigned char *key)
{
    return 0;
}


void DG_SetWindowTitle(const char * title) {}

int
main(int argc, char *argv[])
{
    uint64 fb_pa = getfb();
    printf("Doom starting... FB at %p\n", fb_pa);

    screen_ptr = mmap(0, SCREEN_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_DEVICE, -1, fb_pa);
    
    if(screen_ptr == (void*) -1) {
        printf("mmap failed\n");
        exit(1);
    }

    doomgeneric_Create(argc, argv); 

    while (1)
    {
        doomgeneric_Tick();
    }
    return 0;
}