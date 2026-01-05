#define SBRK_ERROR ((char *)-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
typedef unsigned long size_t;
typedef long int off_t;
struct stat;

typedef struct {
  int fd;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

// Replacement for stdint.h
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned long  uint64_t;
typedef char           int8_t;
typedef short          int16_t;
typedef int            int32_t;
typedef long           int64_t;
typedef uint8_t        byte;

// Replacement for limits.h
#ifndef INT_MAX
#define INT_MAX       2147483647
#endif

#ifndef INT_MIN
#define INT_MIN       (-INT_MAX - 1)
#endif

#ifndef SHRT_MAX
#define SHRT_MAX      32767
#endif

#ifndef SHRT_MIN
#define SHRT_MIN      (-SHRT_MAX - 1)
#endif

#ifndef PATH_MAX
#define PATH_MAX      128
#endif

// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int*);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(const char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sys_sbrk(int,int);
int pause(int);
int uptime(void);
void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t len);
int lseek(int fd, int offset, int whence);
uint64 getfb(void);
void flushfb(void);

// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
int atoi(const char*);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);
char* sbrk(int);
char* sbrklazy(int);
void DG_SleepMs(uint32 ms);
uint32 DG_GetTicksMs();
int strncmp(const char *p, const char *q, uint n);
int strncasecmp(const char *s1, const char *s2, int n);
size_t fread(void *dest, size_t size, size_t count, FILE *src);
long ftell(FILE *f);
int fseek(FILE *f, long off, int origin);
int fclose(FILE *f);
FILE * fopen(const char *fname, const char *mode);

// printf.c
void fprintf(int, const char*, ...) __attribute__ ((format (printf, 2, 3)));
void printf(const char*, ...) __attribute__ ((format (printf, 1, 2)));

// umalloc.c
void* malloc(uint);
void free(void*);
void* calloc(uint n, uint size);
