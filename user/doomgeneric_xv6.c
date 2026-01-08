#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"
#include "kernel/fcntl.h"
#include "doomgeneric.h" 
#include "doomkeys.h"
#include "d_event.h"
#include "tables.h"
#include <stdarg.h>

#define fopen  ram_fopen
#define fclose ram_fclose
#define fread  ram_fread
#define fseek  ram_fseek
#define ftell  ram_ftell
#define KEY_LIFETIME 4

void D_PostEvent (event_t* ev);

// Global variables
void* screen_ptr = 0;
uint64 fb_pa = 0;
byte* I_VideoBuffer = 0; 
int usegamma = 0;
int mouse_acceleration = 0;
int mouse_threshold = 0;
int drone = 0;
int screensaver_mode = 0;
int net_client_connected = 0;
int vanilla_keyboard_mapping = 0;
int usemouse = 0;
int snd_musicdevice = 0;
int screenvisible = 1;
uint32_t palette_lookup[256];
int errno = 0;
int key_life[256];

// RAM-Disk File System (lseek behaved weirdly)
#undef malloc
#undef free
#undef realloc

void* DG_Malloc(uint size) {
    // size + 8 (header)
    uint *p = malloc(size + 8); 
    if (!p) {
        return 0;
    }

    *p = size; 
    
    return (void *)(p + 2);
}

void DG_Free(void* ptr) {
    if (!ptr) {
        return;
    }

    uint *p = (uint *) ptr - 2;
    free(p);
}

void* DG_Realloc(void* ptr, uint new_size) {
    if (!ptr) {
        return DG_Malloc(new_size);
    }

    if(new_size == 0) { 
        DG_Free(ptr); return 0; 
    }

    uint *p = (uint *)ptr - 2;
    uint old_size = *p;

    void *new_ptr = DG_Malloc(new_size);
    if (!new_ptr) {
        return 0;
    }

    uint copy_size = (old_size < new_size) ? old_size : new_size;
    memmove(new_ptr, ptr, copy_size);

    DG_Free(ptr);
    return new_ptr;
}

typedef struct {
    int fd;
    byte *data;
    uint size;
    uint pos;
} RAMFILE;

// Load entire file in memory
FILE *ram_fopen(const char *fname, const char *mode) {

    int fd = open(fname, O_RDONLY);
    if(fd < 0) return 0;

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd); return 0;
    }

    RAMFILE *rf = malloc(sizeof(RAMFILE));
    if(!rf) { 
        close(fd); return 0; 
    }

    rf -> fd = fd;
    rf -> size = st.size;
    rf -> pos = 0;
    rf -> data = malloc(st.size);
    
    if(!rf -> data) {
        printf("fopen: Out of RAM for %s (size %ld)\n", fname, st.size);
        free(rf);
        close(fd);
        return 0;
    }

    if(read(fd, rf->data, st.size) != st.size) {
        printf("fopen: Read incomplete\n");
    }

    close(fd);
    
    return (FILE*)rf;
}

// Clsoe RAM file
int ram_fclose(FILE *f) {
    RAMFILE *rf = (RAMFILE *) f;
    if (rf) {
        if (rf->data) free(rf->data);
        free(rf);
    }
    return 0;
}

// Read from RAM file
size_t ram_fread(void *dest, size_t size, size_t count, FILE *f) {
    RAMFILE *rf = (RAMFILE*)f;
    uint bytes = size * count;
    
    if (rf->pos + bytes > rf->size) {
        bytes = rf->size - rf->pos;
    }

    memmove(dest, rf->data + rf->pos, bytes);
    rf->pos += bytes;
    
    return bytes / size;
}

// Seek in RAM file
int ram_fseek(FILE *f, long offset, int origin) {
    RAMFILE *rf = (RAMFILE *) f;
    switch (origin) {
        case SEEK_SET:
            rf -> pos = offset;
            break;
        case SEEK_CUR:
            rf -> pos += offset;
            break;
        case SEEK_END:
            rf -> pos = rf -> size + offset; 
            break;
    }
    
    // Clamp to size
    if (rf -> pos > rf -> size) rf -> pos = rf -> size;
    return 0;
}

// Returns pos in RAM file
long ram_ftell(FILE *f) {
    RAMFILE *rf = (RAMFILE *) f;
    return rf->pos;
}

// Write to stdout or stderr
uint64 fwrite(const void *ptr, uint64 size, uint64 nmemb, FILE *stream) {
    if((uint64) stream == 1 || (uint64) stream == 2) {
        printf("%s", (char *) ptr);
        return nmemb;
    }
    return 0;
}

// Remove a file by unlinking
int remove(const char *filename) { 
  return unlink(filename); 
}

// Rename a file by unlinking the old name and linking to new name
int rename(const char *old, const char *new) {
  unlink(new);
  if (link(old, new) < 0) return -1;
  return unlink(old);
}

// Math stuff
int abs(int x) { 
    return x < 0 ? -x : x; 
}

double fabs(double x) { 
    return x < 0 ? -x : x; 
}

int isdigit(int c) {
    if (c >= '0' && c <= '9') {
        return 1;
    } else {
        return 0;
    }
}

int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

// Written by Gemini 3 Pro
double atof(const char *s)
{
    double val, power;
    int i, sign;
    for (i = 0; isspace(s[i]); i++) {
        continue;
    }

    sign = (s[i] == '-') ? -1 : 1;
    if (s[i] == '+' || s[i] == '-')
        i++;

    for (val = 0.0; isdigit(s[i]); i++) {
        val = 10.0 * val + (s[i] - '0');
    }

    power = 1;

    if (s[i] == '.') {
        i++;
        for (power = 1.0; isdigit(s[i]); i++) {
            val = 10.0 * val + (s[i] - '0');
            power *= 10.0;
        }
    }

    return sign * val / power;
}

int toupper(int c) { 
  return (c >= 'a' && c <= 'z') ? c - 32 : c; 
}

// Written by Gemini 3 Pro
int sscanf(const char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    
    int count = 0;
    const char *fmt_ptr = format;
    
    while (*fmt_ptr && *str) {
        // Skip whitespace in format and input
        if (isspace(*fmt_ptr)) {
            fmt_ptr++;
            continue;
        }
        while (isspace(*str)) str++;

        if (*fmt_ptr == '%') {
            fmt_ptr++; // Skip '%'
            
            char type = *fmt_ptr;
            if (type == 's') {
                char *dest = va_arg(ap, char*);
                // Copy until whitespace
                while (*str && !isspace(*str)) {
                    *dest++ = *str++;
                }
                *dest = 0; // Null terminate
                count++;
            } 
            else if (type == 'x') { // HEX Support
                int *dest = va_arg(ap, int*);
                int val = 0;
                // Skip '0x' if present
                if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) str += 2;
                
                while (*str) {
                    char c = *str;
                    int d = -1;
                    if (c >= '0' && c <= '9') d = c - '0';
                    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                    else break;
                    val = val * 16 + d;
                    str++;
                }
                *dest = val;
                count++;
            } 
            else if (type == 'd') { // DECIMAL Support
                int *dest = va_arg(ap, int*);
                int val = 0;
                int sign = 1;
                if (*str == '-') { sign = -1; str++; }
                while (*str >= '0' && *str <= '9') {
                    val = val * 10 + (*str - '0');
                    str++;
                }
                *dest = val * sign;
                count++;
            }
            fmt_ptr++;
        } else {
            // Literal match
            if (*str != *fmt_ptr) break;
            str++;
            fmt_ptr++;
        }
    }
    
    va_end(ap);
    return count;
}

// Written by Gemini 3 Pro
static void
sprintint(char **buf, char *end, int xx, int base, int sign, int min_width, char pad_char)
{
  char digits[] = "0123456789abcdef";
  char buf_tmp[16];
  int i = 0;
  uint x;

  if(sign && (sign = xx < 0)) x = -xx;
  else x = xx;

  // Generate digits backwards
  do {
    buf_tmp[i++] = digits[x % base];
  } while((x /= base) != 0);

  // Add negative sign if needed (treat as part of length for padding?)
  // Standard printf pads usually *before* the sign for 0-padding, but internal buffers are tricky.
  // For Doom, we mostly care about %03d which is positive.
  if(sign) buf_tmp[i++] = '-';

  // Calculate actual length
  int len = i;
  
  // Apply Padding (Write extra pad_chars if len < min_width)
  while(len < min_width && *buf < end - 1) {
      *(*buf)++ = pad_char;
      min_width--;
  }

  // Write the actual number (reversed)
  while(--i >= 0 && *buf < end - 1)
    *(*buf)++ = buf_tmp[i];
}

// Written by Gemini 3 Pro
int vsnprintf(char *buf, uint size, const char *fmt, va_list ap)
{
  char *end = buf + size;
  char *s;
  int c, i;
  
  for(i = 0; fmt[i]; i++){
    c = fmt[i] & 0xff;
    
    if(c != '%'){
      if(buf < end - 1) *buf++ = c;
      continue;
    }

    i++; // Move past '%'
    
    int width = 0;
    char pad = ' ';
    
    // Check for zero padding
    if(fmt[i] == '0'){
        pad = '0';
        i++;
    }
    
    // Check for precision acting as zero padding (e.g. %.3d)
    else if(fmt[i] == '.'){
        pad = '0'; // For integers, precision is basically zero-padding
        i++;
    }

    // Parse Width (Simple 1-digit support is enough for Doom's %.3d)
    while(fmt[i] >= '0' && fmt[i] <= '9'){
        width = width * 10 + (fmt[i] - '0');
        i++;
    }

    // Check Type
    c = fmt[i]; 
    
    if(c == 'd' || c == 'i'){
        sprintint(&buf, end, va_arg(ap, int), 10, 1, width, pad);
    } else if(c == 'x' || c == 'p'){
        sprintint(&buf, end, va_arg(ap, int), 16, 0, width, pad);
    } else if(c == 's'){
        s = va_arg(ap, char*);
        if(!s) s = "(null)";
        while(*s && buf < end - 1) *buf++ = *s++;
    } else if(c == 'c'){
        if(buf < end - 1) *buf++ = va_arg(ap, int);
    } else if(c == '%'){
        if(buf < end - 1) *buf++ = '%';
    } else {
        // Unknown char, just print it?
        if(buf < end - 1) *buf++ = c;
    }
  }
  *buf = 0; 
  return 0;
}

int snprintf(char *buf, uint size, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, size, fmt, ap);
  va_end(ap);
  return 0;
}

char *strrchr(const char *s, int c) {
    char* ret = NULL;

    while (*s != '\0') {
        if (*s == (char) c) {
            ret = (char *) s;
        }
        s++;
    }
    if ((char) c == '\0') {
        return (char *) s;
    }

    return ret;
}

char *strdup(const char *s) {
    int len = strlen(s);
    char *d = DG_Malloc(len + 1); 
    if (d) {
        memmove(d, s, len+1);
    }
    return d;
}

char *strncpy(char *dest, const char *src, int n) {
    int i;
    for (i = 0; i < n && src[i]; i++) {
        dest[i] = src[i];
    }
    for(; i < n; i++) {
        dest[i] = 0;
    }
    return dest;
}

char *strstr(const char *txt, const char *pat) {
  int n = strlen(pat);
  while (*txt) {
    if(strncmp(txt, pat, n) == 0) {
        return (char *) txt;
    }
    txt++;
  }
  return 0;
}

// Doom System Stubs
typedef struct { 
    unsigned char r,g,b; 
} DG_Color;

extern DG_Color DG_Palette[256];

int xlate_key(int c)
{   
    switch(c) {

        case KEY_W: return KEY_UPARROW;
        case KEY_S: return KEY_DOWNARROW;
        case KEY_A: return KEY_LEFTARROW;
        case KEY_D: return KEY_RIGHTARROW;
        case KEY_UP: return KEY_UPARROW;
        case KEY_DOWN: return KEY_DOWNARROW;
        case KEY_LEFT: return KEY_LEFTARROW;
        case KEY_RIGHT: return KEY_RIGHTARROW;
        
        case KEY_ENTER: return KEY_ENTER;
        case KEY_LF: return KEY_ENTER;
        case KEY_SPACE: return KEY_FIRE;
        case KEY_E: return KEY_USE;
        case KEY_F: return KEY_PAUSE;
        case KEY_ESC: return KEY_ESCAPE;
        
        case KEY_Q: return ',';
        case KEY_R: return '.';

        case KEY_Y: return 'y';
        case KEY_N: return 'n';

        default: return 0;
    }
}

void I_SetPalette(byte* palette)
{
    int i;
    for (i=0; i<256; i++)
    {
        uint8_t r = *palette++;
        uint8_t g = *palette++;
        uint8_t b = *palette++;
        palette_lookup[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
}

int I_GetTime (void) {
    static int first_time = -1;
    static int last_reported_time = 0;
    
    int t = uptime(); 
    
    if (first_time == -1) {
        first_time = t;
        last_reported_time = 0;
    }
    
    // Convert xv6 ticks to Doom ticks
    int doom_time = (t - first_time) * 35 / 10;
    
    // Prevent huge jumps
    if (doom_time > last_reported_time + 35) {
        doom_time = last_reported_time + 1; 
    }
    
    last_reported_time = doom_time;
    return doom_time;
}

void I_Error(char *error, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, error);
    vsnprintf(buf, sizeof(buf), error, ap);
    va_end(ap);
    printf("DOOM ERROR: %s\n", buf);
    exit(1);
}

// Increase Zone Memory to 12MB to handle RAM-Disk overhead
byte *I_ZoneBase(int *size) {
    *size = 12 * 1024 * 1024; 
    byte *p = malloc(*size);
    if (!p) { 
        printf("I_ZoneBase: Malloc failed\n"); 
        exit(1); 
    }
    return p;
}

int I_GetMemoryValue(void) { return 12 * 1024 * 1024; }
void DG_Init(void) {}
void I_InitGraphics(void) {}
void I_ShutdownGraphics(void) {}
void I_ConsoleStdout(void) {}
void I_UpdateNoBlit(void) {}
void I_FinishUpdate(void) { DG_DrawFrame(); }
void I_ReadScreen(byte* scr) {}
void I_StartFrame(void) {}
void I_StartTic(void) {}
int I_GetTimeMS(void) { return DG_GetTicksMs(); }
void I_Sleep(int ms) { pause(ms/10); }
void I_WaitVBL(int count) { pause(count); }
void I_Quit(void) { exit(0); }
void I_BeginRead(void) {}
void I_EndRead(void) {}
void I_InitSound(void) {}
void I_UpdateSound(void) {}
void I_SubmitSound(void) {}
void I_ShutdownSound(void) {}
void I_SetChannels(void) {}
void* I_GetSfx(int id, void* data) { return 0; }
int I_StartSound(int id, int vol, int sep, int pitch, int priority) { return 0; }
void I_StopSound(int handle) {}
int I_SoundIsPlaying(int handle) { return 0; }
void I_UpdateSoundParams(int handle, int vol, int sep, int pitch) {}
int I_GetSfxLumpNum(void* sfx) { return 0; }
void I_PrecacheSounds(void* sfx, int num) {}
void I_InitMusic(void) {}
void I_ShutdownMusic(void) {}
void I_PlaySong(int handle, int looping) {}
void I_PauseSong(int handle) {}
void I_ResumeSong(int handle) {}
void I_StopSong(int handle) {}
void I_UnRegisterSong(int handle) {}
int I_RegisterSong(void* data) { return 1; }
void I_SetMusicVolume(int volume) {}
int I_MusicIsPlaying(int handle) { return 0; }
void I_AtExit(void) {}
void I_PrintBanner(char* msg) { printf("%s", msg); }
void I_PrintDivider(void) {}
void I_PrintStartupBanner(char* msg) { printf("%s", msg); }
void I_CheckIsScreensaver(void) {}
void I_InitTimer(void) {}
void I_InitJoystick(void) {}
void I_Tactile(int on, int off, int total) {}
void StatDump(void) {}
void StatCopy(void) {}
void I_DisplayFPSDots(int dots) {}
void I_SetWindowTitle(char* title) {}
void I_GraphicsCheckCommandLine(void) {}
void I_SetGrabMouseCallback(void* func) {}
void I_EnableLoadingDisk(void) {}
void I_Endoom(void) {}
void I_BindVideoVariables(void) {}
void I_BindJoystickVariables(void) {}
void I_BindSoundVariables(void) {}
int I_GetPaletteIndex(int r, int g, int b) { return 0; }

// DoomGeneric Hooks
void DG_DrawFrame()
{
    if (screen_ptr == 0 || I_VideoBuffer == 0) return;

    uint32_t *gpu_dest = (uint32_t *)screen_ptr;
    byte *doom_src = I_VideoBuffer;
    
    // Upscale to 640x400
    for (int y = 0; y < 200; y++) 
    {
        for (int x = 0; x < 320; x++) 
        {
            uint32_t color = palette_lookup[*doom_src++];

            int top_left = (y * 2) * 640 + (x * 2);

            gpu_dest[top_left] = color;
            gpu_dest[top_left + 1] = color;
            gpu_dest[top_left + 640] = color;
            gpu_dest[top_left + 640 + 1] = color;
        }
    }

    flushfb();
}

void DG_KeyInput(int key, int pressed)
{
    event_t event;
    
    if (pressed) {
        event.type = ev_keydown;
    }
    else {
        event.type = ev_keyup;
    }
    
    // Key code
    event.data1 = key;
    
    D_PostEvent(&event);
}

void DG_SleepMs(uint32_t ms)
{
    uint16_t code;
    uint32_t val;

    while (getch(&code, &val)) {
        int key = xlate_key(code);
        if (key) {
             DG_KeyInput(key, (val == 1)); 
        }
    }
    
    if (ms > 0) {
        pause(ms / 10);
    }
}

uint32 DG_GetTicksMs() { 
    return uptime() * 100; 
}

int DG_GetKey(int *pressed, unsigned char *key) { 
    return 0; 
}

void DG_SetWindowTitle(const char * title) {}

// Main
int main(int argc, char *argv[]) {

    uint64 fb_addr = getfb();

    if(fb_addr == -1 || fb_addr == 0) {
        printf("DEBUG: getfb failed!\n");
        exit(1);
    }
    
    screen_ptr = (void *) fb_addr;
    
    I_VideoBuffer = DG_Malloc(320 * 200);;

    doomgeneric_Create(argc, argv); 
    
    while (1)
    {
        doomgeneric_Tick();
    }

    return 0;
}