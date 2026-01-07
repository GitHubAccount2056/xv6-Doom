#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/riscv.h"
#include "kernel/vm.h"
#include "user/user.h"

// Initialise stdin, stdout, stderr
static FILE _stdin_storage = {0};
static FILE _stdout_storage = {1};
static FILE _stderr_storage = {2};

FILE *stdin = &_stdin_storage;
FILE *stdout = &_stdout_storage;
FILE *stderr = &_stderr_storage;

//
// wrapper so that it's OK if main() does not call exit().
//
void
start(int argc, char **argv)
{
  int r;
  extern int main(int argc, char **argv);
  r = main(argc, argv);
  exit(r);
}

char*
strcpy(char *s, const char *t)
{
  char *os;

  os = s;
  while((*s++ = *t++) != 0)
    ;
  return os;
}

int
strcmp(const char *p, const char *q)
{
  while(*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
}

uint
strlen(const char *s)
{
  int n;

  for(n = 0; s[n]; n++)
    ;
  return n;
}

void*
memset(void *dst, int c, uint n)
{
  char *cdst = (char *) dst;
  int i;
  for(i = 0; i < n; i++){
    cdst[i] = c;
  }
  return dst;
}

char*
strchr(const char *s, char c)
{
  for(; *s; s++)
    if(*s == c)
      return (char*)s;
  return 0;
}

char*
gets(char *buf, int max)
{
  int i, cc;
  char c;

  for(i=0; i+1 < max; ){
    cc = read(0, &c, 1);
    if(cc < 1)
      break;
    buf[i++] = c;
    if(c == '\n' || c == '\r')
      break;
  }
  buf[i] = '\0';
  return buf;
}

int
stat(const char *n, struct stat *st)
{
  int fd;
  int r;

  fd = open(n, O_RDONLY);
  if(fd < 0)
    return -1;
  r = fstat(fd, st);
  close(fd);
  return r;
}

int
atoi(const char *s)
{
  int n;

  n = 0;
  while('0' <= *s && *s <= '9')
    n = n*10 + *s++ - '0';
  return n;
}

void*
memmove(void *vdst, const void *vsrc, int n)
{
  char *dst;
  const char *src;

  dst = vdst;
  src = vsrc;
  if (src > dst) {
    while(n-- > 0)
      *dst++ = *src++;
  } else {
    dst += n;
    src += n;
    while(n-- > 0)
      *--dst = *--src;
  }
  return vdst;
}

int
memcmp(const void *s1, const void *s2, uint n)
{
  const char *p1 = s1, *p2 = s2;
  while (n-- > 0) {
    if (*p1 != *p2) {
      return *p1 - *p2;
    }
    p1++;
    p2++;
  }
  return 0;
}

void *
memcpy(void *dst, const void *src, uint n)
{
  return memmove(dst, src, n);
}

char *
sbrk(int n) {
  return sys_sbrk(n, SBRK_EAGER);
}

char *
sbrklazy(int n) {
  return sys_sbrk(n, SBRK_LAZY);
}

FILE *
fopen(const char *fname, const char *mode) {
  int fd = open(fname, O_RDONLY);
  if (fd < 0) {
    return 0;
  }
  FILE *f = malloc(sizeof(FILE));
  f -> fd = fd;
  return f;
}

int
fclose(FILE *f) {
  if (!f) {
    return -1;
  }
  close(f -> fd);
  free(f);
  return 0;
}

int
fseek(FILE *f, long off, int origin) {
  return lseek(f -> fd, off, origin);
}

long
ftell(FILE *f) {
  return lseek(f -> fd, 0, SEEK_CUR);
}

size_t
fread(void *dest, size_t size, size_t count, FILE *src) {
  int total_read = size * count;
  int result = read(src -> fd, dest, total_read);
  if (result < 0) {
    return -1;
  }
  return result / size;
}

int
strcasecmp(const char *s1, const char *s2) {
  while (*s1 && *s2) {
    char c1 = (*s1 >= 'A' && *s1 <= 'Z') ? *s1 + ('a' - 'A') : *s1;
    char c2 = (*s2 >= 'A' && *s2 <= 'Z') ? *s2 + ('a' - 'A') : *s2;
    if (c1 != c2) {
      return c1 - c2;
    }
    s1++; s2++;
  }
  return *s2 - *s1;
}

int strncasecmp(const char *s1, const char *s2, int n) {
  if (n <= 0) return 0;
  while (n-- > 0) {
    char c1 = *s1;
    char c2 = *s2;
    
    if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
    if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
    
    if (c1 != c2) return c1 - c2;
    
    if (c1 == 0) return 0;
    
    s1++;
    s2++;
  }
  return 0;
}

int
strncmp(const char *p, const char *q, uint n)
{
  while(n > 0 && *p && *p == *q){
    n--;
    p++;
    q++;
  }
  if(n == 0) {
    return 0;
  }
  return (uchar)*p - (uchar)*q;
}