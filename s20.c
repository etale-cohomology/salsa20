/*
gcc-8 s20.c -o s20  -Ofast  &&  ./s20
objdump s20 -d -M intel
git add .  &&  git commit --allow-empty-message --no-edit  &&  git push

git init
git add --all
git commit --allow-empty-message --no-edit
git remote add origin https://github.com/etale-cohomology/salsa20.git
git push --set-upstream origin master -f
*/
#include <stdio.h>
#include <alloca.h>
#include <time.h>
#include <string.h>
#include <unistd.h>    // @ftruncate()
#include <fcntl.h>     // @open()
#include <errno.h>     // @errno
#include <limits.h>    // @PATH_MAX
#include <sys/mman.h>  // @mmap()
#include <sys/stat.h>  // @fstat()
#include <stdlib.h>    // @malloc()

// ----------------------------------------------------------------------------------------------------------------------------#
/* @blk1  libtype */
#include <stdint.h>
typedef int8_t    i8;
typedef int16_t   i16;
typedef int32_t   i32;
typedef int64_t   i64;

typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;

// ----------------------------------------------------------------------------------------------------------------------------#
#define M_SEP                         "-------------------------------------------------------------------------------------------------------------------------------\x1b[91m#\x1b[0m\n"
#define m_sep()                       printf("\n"M_SEP)
#define m_chks(ST)({  if(((i64)(ST))==-1ll)  printf("\x1b[91mFAIL  \x1b[31m%s\x1b[91m:\x1b[0mL\x1b[32m%d\x1b[91m:\x1b[94m%s\x1b[0m()  \x1b[37m%s\x1b[0m\n", __FILE__,__LINE__,__func__, strerror(errno));  })  // We turn this into statement-expressions so that they can be used as single expressions in bracket-less IF or IF-ELSE statements!
#define m_fail()                             printf("\x1b[91mFAIL  \x1b[31m%s\x1b[91m:\x1b[0mL\x1b[32m%d\x1b[91m:\x1b[94m%s\x1b[0m()\n",                    __FILE__,__LINE__,__func__)
#define m_fail1(TXT)                         printf("\x1b[91mFAIL  \x1b[31m%s\x1b[91m:\x1b[0mL\x1b[32m%d\x1b[91m:\x1b[94m%s\x1b[0m()  %s\n",                __FILE__,__LINE__,__func__, TXT)
#define m_fori(IDX, IDX_INI,IDX_END)  for(i32 (IDX)=(IDX_INI); (IDX)<(IDX_END); ++(IDX))  // Fastest `for` loop: signed yields a faster loop than unsigned because there's no need for overflow checks (or something), and i32 is faster than i64!
#define u4_to_asciihex(BIN)({         u8 _bin=BIN&0b1111;  (_bin<0xa) ? _bin+0x30 : _bin+0x57;  })  // Map: a  4-bit    uint  TO an asciihex digit
#define m_min(    A, B)               ({  typeof(A) _a=(A);  typeof(B) _b=(B);  _a<_b ?  _a : _b;  })  // @typeof() is useful w/ `statement expressions`. Here's how they can be used to define a safe macro which operates on any arithmetic type and `evaluates each of its arguments exactly once`
#define fmtu32hbe( _X)({              u32 _x=(u32) (_X); i32 _n=8*sizeof(u32)/4;  char* _d=alloca(_n);  m_fori(i, 0,_n)  _d[i]=u4_to_asciihex(_x>>(8*sizeof(u32) -4*(i+1)));  _d[_n]=0x00;  _d;  })  /*map a u32  asciihexbe (to ascii hex,  most-significant bit first)*/

#define arrs_ini(TYPE,BDIM)({  /*initialize a stack-array (ie. a stack-allocated array), zero-initialized*/  \
  TYPE* arr = alloca(BDIM);  \
  memset(arr,0x00,BDIM);     \
  arr;                       \
})
#define arrs_end(ARR){}  // noop

#define datestr()({  \
  int64_t bdim = 0x100;  \
  char*   date0 = arrs_ini(char,bdim);                                                                                                          strftime(date0,bdim-1, "%Y-%m-%dT%H%M%S", localtime((time_t[]){time(NULL)}));  \
  char*   date1 = arrs_ini(char,bdim);  struct timespec ts; clock_gettime(CLOCK_REALTIME,&ts);  u64 ep = 1000000000ull*ts.tv_sec + ts.tv_nsec;  snprintf(date1,bdim-1, "%s.%06lu", date0, ep/1000ull % 1000000ull);  /*ms: ep/1000000ull % 1000ull, us: ep/1000ull % 1000000ull, ns: ep/1ull % 1000000000ull*/\
  date1;  \
})

void m_save(i64 bdim,void* data, char* path){
  int fd = open(path, O_RDWR|O_CREAT, 0b110110000);  m_chks(fd);
  m_chks(ftruncate(fd, bdim));
  i64 st = write(fd, data,bdim);
  m_chks(close(fd));
}

void s20_show(u32 x[16], char* msg){
  if(msg)  printf("\x1b[92m%s\x1b[91m:\x1b[0m\n", msg);
  m_fori(i, 0,4){
    m_fori(j, 0,4)
      printf(" %s", fmtu32hbe(x[4*i+j]));
    putchar(0x0a);
  }
}

// ----------------------------------------------------------------------------------------------------------------------------# @blk1
#define ADD(a,b)            ((a)+(b))                           // add  (out of place)
#define XOR(a,b)            ((a)^(b))                           // xor  (out of place)
#define RL( x,r)            (((x)<<(r)) | ((x)>>(32-(r))))      // rotl (out of place)
#define MOD(x,n)            (0<=(x) ? (x)%(n) : (n)+((x)%(n)))  // mod with number-theoretic wraparound (out of place)
#define RA(x, i,j, r)       RL(ADD(x[i],x[j]), r)               // add-rotl (out of place)
#define XRA(x, i,p,n, rot)  XOR(x[n], RL(ADD(x[i],x[p]),rot))   // add-rotl-xor (out of place)

#define N        (4)
#define COLP(l)  MOD(l-N,N*N)                // prev col, @l is the LINEAR index (ie. RCOLS*row + col)
#define COLN(l)  MOD(l+N,N*N)                // next col, @l is the LINEAR index (ie. RCOLS*row + col)
#define ROWP(l)  (MOD(l-1,N) + (N*((l)/N)))  // prev row, @l is the LINEAR index (ie. RCOLS*row + col)
#define ROWN(l)  (MOD(l+1,N) + (N*((l)/N)))  // next row, @l is the LINEAR index (ie. RCOLS*row + col)
#define TR(l)    (N*((l)%N) + ((l)/N))       // transpose l-index. Eg. for N==4 and the (i,j) n-index (1,3), the transpose is n-index (3,1), so its l-index 4*1+3 gets mapped to 4*3+1

// #define QC(x, l,rot)  x[COLN(l)] = XOR(x[COLN(l)], RL(ADD(x[l],x[COLP(l)]), rot))  // quarterround, column-major (in place)
// #define QR(x, l,rot)  x[ROWN(l)] = XOR(x[ROWN(l)], RL(ADD(x[l],x[ROWP(l)]), rot))  // quarterround, row   -major (in place)
#define QC(x, l,rot)  x[COLN(l)] = XRA(x, l,COLP(l),COLN(l), rot)  // quarterround, column-major (in place)
#define QR(x, l,rot)  x[ROWN(l)] = XRA(x, l,ROWP(l),ROWN(l), rot)  // quarterround, row   -major (in place)

static const u32 S20_CONSTANTS[N] = {0x61707865,0x3320646e,0x79622d32,0x6b206574};  // constants for the 256-bit-key version

#if 0  // a salsa20 block implementation
void s20_blk(u32 in[N*N], u32 out[N*N]){
  int i;
  u32 t[N*N];
  for(i=0; i<N*N; ++i)  t[i] = in[i];
  for(i=0; i<0x14; i+=2){  // each word: add-rotl-xor
    t[ 4] ^= RL(t[ 0]+t[12], 7);  t[ 8] ^= RL(t[ 4]+t[ 0], 9);  t[12] ^= RL(t[ 8]+t[ 4],13);  t[ 0] ^= RL(t[12]+t[ 8],18);  //  4 <- ( 0,12),  8 <- ( 4, 0), 12 <- ( 8, 4),  0 <- (12, 8)
    t[ 9] ^= RL(t[ 5]+t[ 1], 7);  t[13] ^= RL(t[ 9]+t[ 5], 9);  t[ 1] ^= RL(t[13]+t[ 9],13);  t[ 5] ^= RL(t[ 1]+t[13],18);  //  9 <- ( 5, 1), 13 <- ( 9, 5),  1 <- (13, 9),  5 <- ( 1,13)
    t[14] ^= RL(t[10]+t[ 6], 7);  t[ 2] ^= RL(t[14]+t[10], 9);  t[ 6] ^= RL(t[ 2]+t[14],13);  t[10] ^= RL(t[ 6]+t[ 2],18);  // 14 <- (10, 6),  2 <- (14,10),  6 <- ( 2,14), 10 <- ( 6, 2)
    t[ 3] ^= RL(t[15]+t[11], 7);  t[ 7] ^= RL(t[ 3]+t[15], 9);  t[11] ^= RL(t[ 7]+t[ 3],13);  t[15] ^= RL(t[11]+t[ 7],18);  //  3 <- (15,11),  7 <- ( 3,15), 11 <- ( 7, 3), 15 <- (11, 7)

    t[ 1] ^= RL(t[ 0]+t[ 3], 7);  t[ 2] ^= RL(t[ 1]+t[ 0], 9);  t[ 3] ^= RL(t[ 2]+t[ 1],13);  t[ 0] ^= RL(t[ 3]+t[ 2],18);  //  1 <- ( 0, 3),  2 <- ( 1, 0),  3 <- ( 2, 1),  0 <- ( 3, 2)
    t[ 6] ^= RL(t[ 5]+t[ 4], 7);  t[ 7] ^= RL(t[ 6]+t[ 5], 9);  t[ 4] ^= RL(t[ 7]+t[ 6],13);  t[ 5] ^= RL(t[ 4]+t[ 7],18);  //  6 <- ( 5, 4),  7 <- ( 6, 5),  4 <- ( 7, 6),  5 <- ( 4, 7)
    t[11] ^= RL(t[10]+t[ 9], 7);  t[ 8] ^= RL(t[11]+t[10], 9);  t[ 9] ^= RL(t[ 8]+t[11],13);  t[10] ^= RL(t[ 9]+t[ 8],18);  // 11 <- (10, 9),  8 <- (11,10),  9 <- ( 8,11), 10 <- ( 9, 8)
    t[12] ^= RL(t[15]+t[14], 7);  t[13] ^= RL(t[12]+t[15], 9);  t[14] ^= RL(t[13]+t[12],13);  t[15] ^= RL(t[14]+t[13],18);  // 12 <- (15,14), 13 <- (12,15), 14 <- (13,12), 15 <- (14,13)
  }
  for(i=0; i<N*N; ++i)  out[i] = t[i]+in[i];
}
#endif

#if 0  // a salsa20 block implementation
void s20_blk(u32 in[N*N], u32 out[N*N]){
  int i;
  u32 t[N*N];
  for(i=0; i<N*N; ++i)  t[i] = in[i];
  for(i=0; i<0x14; i+=2){  // each word: add-rotl-xor
    t[ 4] ^= RA(t, 0,12, 7);  t[ 8] ^= RA(t, 4, 0, 9);  t[12] ^= RA(t, 8, 4,13);  t[ 0] ^= RA(t,12, 8,18);  //  4 <- ( 0,12),  8 <- ( 4, 0), 12 <- ( 8, 4),  0 <- (12, 8)
    t[ 9] ^= RA(t, 5, 1, 7);  t[13] ^= RA(t, 9, 5, 9);  t[ 1] ^= RA(t,13, 9,13);  t[ 5] ^= RA(t, 1,13,18);  //  9 <- ( 5, 1), 13 <- ( 9, 5),  1 <- (13, 9),  5 <- ( 1,13)
    t[14] ^= RA(t,10, 6, 7);  t[ 2] ^= RA(t,14,10, 9);  t[ 6] ^= RA(t, 2,14,13);  t[10] ^= RA(t, 6, 2,18);  // 14 <- (10, 6),  2 <- (14,10),  6 <- ( 2,14), 10 <- ( 6, 2)
    t[ 3] ^= RA(t,15,11, 7);  t[ 7] ^= RA(t, 3,15, 9);  t[11] ^= RA(t, 7, 3,13);  t[15] ^= RA(t,11, 7,18);  //  3 <- (15,11),  7 <- ( 3,15), 11 <- ( 7, 3), 15 <- (11, 7)

    t[ 1] ^= RA(t, 0, 3, 7);  t[ 2] ^= RA(t, 1, 0, 9);  t[ 3] ^= RA(t, 2, 1,13);  t[ 0] ^= RA(t, 3, 2,18);  //  1 <- ( 0, 3),  2 <- ( 1, 0),  3 <- ( 2, 1),  0 <- ( 3, 2)
    t[ 6] ^= RA(t, 5, 4, 7);  t[ 7] ^= RA(t, 6, 5, 9);  t[ 4] ^= RA(t, 7, 6,13);  t[ 5] ^= RA(t, 4, 7,18);  //  6 <- ( 5, 4),  7 <- ( 6, 5),  4 <- ( 7, 6),  5 <- ( 4, 7)
    t[11] ^= RA(t,10, 9, 7);  t[ 8] ^= RA(t,11,10, 9);  t[ 9] ^= RA(t, 8,11,13);  t[10] ^= RA(t, 9, 8,18);  // 11 <- (10, 9),  8 <- (11,10),  9 <- ( 8,11), 10 <- ( 9, 8)
    t[12] ^= RA(t,15,14, 7);  t[13] ^= RA(t,12,15, 9);  t[14] ^= RA(t,13,12,13);  t[15] ^= RA(t,14,13,18);  // 12 <- (15,14), 13 <- (12,15), 14 <- (13,12), 15 <- (14,13)
  }
  for(i=0; i<N*N; ++i)  out[i] = t[i]+in[i];
}
#endif

#if 0  // a salsa20 block implementation
void s20_blk(u32 in[N*N], u32 out[N*N]){
  int i;
  u32 t[N*N];
  for(i=0; i<N*N; ++i)  t[i] = in[i];
  for(i=0; i<0x14; i+=2){  // each word: add-rotl-xor
    t[ 4] = XRA(t, 0,12, 4, 7);  t[ 8] = XRA(t, 4, 0, 8, 9);  t[12] = XRA(t, 8, 4,12,13);  t[ 0] = XRA(t,12, 8, 0,18);
    t[ 9] = XRA(t, 5, 1, 9, 7);  t[13] = XRA(t, 9, 5,13, 9);  t[ 1] = XRA(t,13, 9, 1,13);  t[ 5] = XRA(t, 1,13, 5,18);
    t[14] = XRA(t,10, 6,14, 7);  t[ 2] = XRA(t,14,10, 2, 9);  t[ 6] = XRA(t, 2,14, 6,13);  t[10] = XRA(t, 6, 2,10,18);
    t[ 3] = XRA(t,15,11, 3, 7);  t[ 7] = XRA(t, 3,15, 7, 9);  t[11] = XRA(t, 7, 3,11,13);  t[15] = XRA(t,11, 7,15,18);

    t[ 1] = XRA(t, 0, 3, 1, 7);  t[ 2] = XRA(t, 1, 0, 2, 9);  t[ 3] = XRA(t, 2, 1, 3,13);  t[ 0] = XRA(t, 3, 2, 0,18);
    t[ 6] = XRA(t, 5, 4, 6, 7);  t[ 7] = XRA(t, 6, 5, 7, 9);  t[ 4] = XRA(t, 7, 6, 4,13);  t[ 5] = XRA(t, 4, 7, 5,18);
    t[11] = XRA(t,10, 9,11, 7);  t[ 8] = XRA(t,11,10, 8, 9);  t[ 9] = XRA(t, 8,11, 9,13);  t[10] = XRA(t, 9, 8,10,18);
    t[12] = XRA(t,15,14,12, 7);  t[13] = XRA(t,12,15,13, 9);  t[14] = XRA(t,13,12,14,13);  t[15] = XRA(t,14,13,15,18);
  }
  for(i=0; i<N*N; ++i)  out[i] = t[i]+in[i];
}
#endif

#if 0  // a salsa20 block implementation
void s20_blk(u32 in[N*N], u32 out[N*N]){
  int i;
  u32 t[N*N];
  for(i=0; i<N*N; ++i)  t[i] = in[i];
  for(i=0; i<0x14; i+=2){  // each word: add-rotl-xor
    t[0x4] = XRA(t,0x0,0xc,0x4,0x07);  t[0x8] = XRA(t,0x4,0x0,0x8,0x09);  t[0xc] = XRA(t,0x8,0x4,0xc,0x0d);  t[0x0] = XRA(t,0xc,0x8,0x0,0x12);
    t[0x9] = XRA(t,0x5,0x1,0x9,0x07);  t[0xd] = XRA(t,0x9,0x5,0xd,0x09);  t[0x1] = XRA(t,0xd,0x9,0x1,0x0d);  t[0x5] = XRA(t,0x1,0xd,0x5,0x12);
    t[0xe] = XRA(t,0xa,0x6,0xe,0x07);  t[0x2] = XRA(t,0xe,0xa,0x2,0x09);  t[0x6] = XRA(t,0x2,0xe,0x6,0x0d);  t[0xa] = XRA(t,0x6,0x2,0xa,0x12);
    t[0x3] = XRA(t,0xf,0xb,0x3,0x07);  t[0x7] = XRA(t,0x3,0xf,0x7,0x09);  t[0xb] = XRA(t,0x7,0x3,0xb,0x0d);  t[0xf] = XRA(t,0xb,0x7,0xf,0x12);

    t[0x1] = XRA(t,0x0,0x3,0x1,0x07);  t[0x2] = XRA(t,0x1,0x0,0x2,0x09);  t[0x3] = XRA(t,0x2,0x1,0x3,0x0d);  t[0x0] = XRA(t,0x3,0x2,0x0,0x12);
    t[0x6] = XRA(t,0x5,0x4,0x6,0x07);  t[0x7] = XRA(t,0x6,0x5,0x7,0x09);  t[0x4] = XRA(t,0x7,0x6,0x4,0x0d);  t[0x5] = XRA(t,0x4,0x7,0x5,0x12);
    t[0xb] = XRA(t,0xa,0x9,0xb,0x07);  t[0x8] = XRA(t,0xb,0xa,0x8,0x09);  t[0x9] = XRA(t,0x8,0xb,0x9,0x0d);  t[0xa] = XRA(t,0x9,0x8,0xa,0x12);
    t[0xc] = XRA(t,0xf,0xe,0xc,0x07);  t[0xd] = XRA(t,0xc,0xf,0xd,0x09);  t[0xe] = XRA(t,0xd,0xc,0xe,0x0d);  t[0xf] = XRA(t,0xe,0xd,0xf,0x12);
  }
  for(i=0; i<N*N; ++i)  out[i] = t[i]+in[i];
}
#endif

#if 0  // a salsa20 block implementation
void s20_blk(u32 in[N*N], u32 out[N*N]){
  int i;
  u32 x[16];
  for(i=0; i<N*N; ++i)  x[i] = in[i];
  for(i=0; i<0x14; i+=2){  // each word: add-rotl-xor
    x[COLN(0x0)] = XRA(x,0x0,COLP(0x0),COLN(0x0), 0x07);  x[COLN(0x4)] = XRA(x,0x4,COLP(0x4),COLN(0x4), 0x09);  x[COLN(0x8)] = XRA(x,0x8,COLP(0x8),COLN(0x8), 0x0d);  x[COLN(0xc)] = XRA(x,0xc,COLP(0xc),COLN(0xc), 0x12);
    x[COLN(0x5)] = XRA(x,0x5,COLP(0x5),COLN(0x5), 0x07);  x[COLN(0x9)] = XRA(x,0x9,COLP(0x9),COLN(0x9), 0x09);  x[COLN(0xd)] = XRA(x,0xd,COLP(0xd),COLN(0xd), 0x0d);  x[COLN(0x1)] = XRA(x,0x1,COLP(0x1),COLN(0x1), 0x12);
    x[COLN(0xa)] = XRA(x,0xa,COLP(0xa),COLN(0xa), 0x07);  x[COLN(0xe)] = XRA(x,0xe,COLP(0xe),COLN(0xe), 0x09);  x[COLN(0x2)] = XRA(x,0x2,COLP(0x2),COLN(0x2), 0x0d);  x[COLN(0x6)] = XRA(x,0x6,COLP(0x6),COLN(0x6), 0x12);
    x[COLN(0xf)] = XRA(x,0xf,COLP(0xf),COLN(0xf), 0x07);  x[COLN(0x3)] = XRA(x,0x3,COLP(0x3),COLN(0x3), 0x09);  x[COLN(0x7)] = XRA(x,0x7,COLP(0x7),COLN(0x7), 0x0d);  x[COLN(0xb)] = XRA(x,0xb,COLP(0xb),COLN(0xb), 0x12);

    x[ROWN(0x0)] = XRA(x,0x0,ROWP(0x0),ROWN(0x0), 0x07);  x[ROWN(0x1)] = XRA(x,0x1,ROWP(0x1),ROWN(0x1), 0x09);  x[ROWN(0x2)] = XRA(x,0x2,ROWP(0x2),ROWN(0x2), 0x0d);  x[ROWN(0x3)] = XRA(x,0x3,ROWP(0x3),ROWN(0x3), 0x12);
    x[ROWN(0x5)] = XRA(x,0x5,ROWP(0x5),ROWN(0x5), 0x07);  x[ROWN(0x6)] = XRA(x,0x6,ROWP(0x6),ROWN(0x6), 0x09);  x[ROWN(0x7)] = XRA(x,0x7,ROWP(0x7),ROWN(0x7), 0x0d);  x[ROWN(0x4)] = XRA(x,0x4,ROWP(0x4),ROWN(0x4), 0x12);
    x[ROWN(0xa)] = XRA(x,0xa,ROWP(0xa),ROWN(0xa), 0x07);  x[ROWN(0xb)] = XRA(x,0xb,ROWP(0xb),ROWN(0xb), 0x09);  x[ROWN(0x8)] = XRA(x,0x8,ROWP(0x8),ROWN(0x8), 0x0d);  x[ROWN(0x9)] = XRA(x,0x9,ROWP(0x9),ROWN(0x9), 0x12);
    x[ROWN(0xf)] = XRA(x,0xf,ROWP(0xf),ROWN(0xf), 0x07);  x[ROWN(0xc)] = XRA(x,0xc,ROWP(0xc),ROWN(0xc), 0x09);  x[ROWN(0xd)] = XRA(x,0xd,ROWP(0xd),ROWN(0xd), 0x0d);  x[ROWN(0xe)] = XRA(x,0xe,ROWP(0xe),ROWN(0xe), 0x12);
  }
  for(i=0; i<N*N; ++i)  out[i] = x[i]+in[i];
}
#endif

#if 1  // a salsa20 block implementation
// row-major indexing:
//   00 01 02 03
//   04 05 06 07
//   08 09 0a 0b
//   0c 0d 0e 0f
// col-major indexing:
//   00 04 08 0c
//   01 05 09 0d
//   02 06 0a 0e
//   03 07 0b 0f
void s20_blk(u32 in[N*N], u32 out[N*N]){
  u32 x[N*N];
  for(int i=0; i<N*N; ++i)  x[i] = in[i];
  for(int i=0; i<0x14; i+=2){
    QC(x,0x0,0x07); QC(x,0x5,0x07); QC(x,0xa,0x07); QC(x,0xf,0x07);
    QC(x,0x4,0x09); QC(x,0x9,0x09); QC(x,0xe,0x09); QC(x,0x3,0x09);
    QC(x,0x8,0x0d); QC(x,0xd,0x0d); QC(x,0x2,0x0d); QC(x,0x7,0x0d);
    QC(x,0xc,0x12); QC(x,0x1,0x12); QC(x,0x6,0x12); QC(x,0xb,0x12);

    QR(x,0x0,0x07); QR(x,0x5,0x07); QR(x,0xa,0x07); QR(x,0xf,0x07);
    QR(x,0x1,0x09); QR(x,0x6,0x09); QR(x,0xb,0x09); QR(x,0xc,0x09);
    QR(x,0x2,0x0d); QR(x,0x7,0x0d); QR(x,0x8,0x0d); QR(x,0xd,0x0d);
    QR(x,0x3,0x12); QR(x,0x4,0x12); QR(x,0x9,0x12); QR(x,0xe,0x12);
  }
  for(int i=0; i<N*N; ++i)  out[i] = in[i] + x[i];
}
#endif

#if 0  // a salsa20 block implementation
void s20_blk(u32 in[N*N], u32 out[N*N]){
  u32 x[N*N];
  for(int i=0; i<N*N; ++i)  x[i] = in[i];
  for(int i=0; i<0x14; i+=2){
    QC(x,(0x0*N + 0x0*(N+1)) % (N*N),0x07);  QC(x,(0x0*N + 0x1*(N+1)) % (N*N),0x07);  QC(x,(0x0*N + 0x2*(N+1)) % (N*N),0x07);  QC(x,(0x0*N + 0x3*(N+1)) % (N*N),0x07);
    QC(x,(0x1*N + 0x0*(N+1)) % (N*N),0x09);  QC(x,(0x1*N + 0x1*(N+1)) % (N*N),0x09);  QC(x,(0x1*N + 0x2*(N+1)) % (N*N),0x09);  QC(x,(0x1*N + 0x3*(N+1)) % (N*N),0x09);
    QC(x,(0x2*N + 0x0*(N+1)) % (N*N),0x0d);  QC(x,(0x2*N + 0x1*(N+1)) % (N*N),0x0d);  QC(x,(0x2*N + 0x2*(N+1)) % (N*N),0x0d);  QC(x,(0x2*N + 0x3*(N+1)) % (N*N),0x0d);
    QC(x,(0x3*N + 0x0*(N+1)) % (N*N),0x12);  QC(x,(0x3*N + 0x1*(N+1)) % (N*N),0x12);  QC(x,(0x3*N + 0x2*(N+1)) % (N*N),0x12);  QC(x,(0x3*N + 0x3*(N+1)) % (N*N),0x12);

    QR(x,TR((0x0*N + 0x0*(N+1)) % (N*N)),0x07);  QR(x,TR((0x0*N + 0x1*(N+1)) % (N*N)),0x07);  QR(x,TR((0x0*N + 0x2*(N+1)) % (N*N)),0x07);  QR(x,TR((0x0*N + 0x3*(N+1)) % (N*N)),0x07);
    QR(x,TR((0x1*N + 0x0*(N+1)) % (N*N)),0x09);  QR(x,TR((0x1*N + 0x1*(N+1)) % (N*N)),0x09);  QR(x,TR((0x1*N + 0x2*(N+1)) % (N*N)),0x09);  QR(x,TR((0x1*N + 0x3*(N+1)) % (N*N)),0x09);
    QR(x,TR((0x2*N + 0x0*(N+1)) % (N*N)),0x0d);  QR(x,TR((0x2*N + 0x1*(N+1)) % (N*N)),0x0d);  QR(x,TR((0x2*N + 0x2*(N+1)) % (N*N)),0x0d);  QR(x,TR((0x2*N + 0x3*(N+1)) % (N*N)),0x0d);
    QR(x,TR((0x3*N + 0x0*(N+1)) % (N*N)),0x12);  QR(x,TR((0x3*N + 0x1*(N+1)) % (N*N)),0x12);  QR(x,TR((0x3*N + 0x2*(N+1)) % (N*N)),0x12);  QR(x,TR((0x3*N + 0x3*(N+1)) % (N*N)),0x12);
  }
  for(int i=0; i<N*N; ++i)  out[i] = in[i] + x[i];
}
#endif

#if 0  // a salsa20 block implementation
void s20_blk(u32 in[N*N], u32 out[N*N]){
  u32 x[N*N];
  for(int i=0; i<N*N; ++i)  x[i] = in[i];

  for(int i=0; i<0x14; i+=2){
    for(int j=0; j<N; ++j)  QC(x,    (N*0x0 + j*(N+1)) % (N*N),  0x07);  // row0
    for(int j=0; j<N; ++j)  QC(x,    (N*0x1 + j*(N+1)) % (N*N),  0x09);  // row1
    for(int j=0; j<N; ++j)  QC(x,    (N*0x2 + j*(N+1)) % (N*N),  0x0d);  // row2
    for(int j=0; j<N; ++j)  QC(x,    (N*0x3 + j*(N+1)) % (N*N),  0x12);  // row3

    for(int j=0; j<N; ++j)  QR(x, TR((N*0x0 + j*(N+1)) % (N*N)), 0x07);  // col0
    for(int j=0; j<N; ++j)  QR(x, TR((N*0x1 + j*(N+1)) % (N*N)), 0x09);  // col1
    for(int j=0; j<N; ++j)  QR(x, TR((N*0x2 + j*(N+1)) % (N*N)), 0x0d);  // col2
    for(int j=0; j<N; ++j)  QR(x, TR((N*0x3 + j*(N+1)) % (N*N)), 0x12);  // col3
  }

  for(int i=0; i<N*N; ++i)  out[i] = in[i] + x[i];
}
#endif

void s20_encrypt(u32 sk[N*N/2],u32 nonce[(N*N - N*N/2 - N)/2], i64 bdim,void* ptxt, void* ctxt){  // @sk: 32-byte secret key, @ptxt: data to be encrypted (plaintext), @ctxt: encrypted data (ciphertext, up to 2**70 bytes, I think)
  u32 in[N*N];
  u32 counter[(N*N - N*N/2 - N)/2] = {0x00000000,0x00000000};

  // 0) ini the s20 64-byte blk
  in[0x0] = S20_CONSTANTS[0x0]; // row0
  in[0x1] = sk[0x0];
  in[0x2] = sk[0x1];
  in[0x3] = sk[0x2];

  in[0x4] = sk[0x3];  // row1
  in[0x5] = S20_CONSTANTS[0x1];
  in[0x6] = nonce[0x0];
  in[0x7] = nonce[0x1];

  in[0x8] = counter[0x0];  // row2
  in[0x9] = counter[0x0];
  in[0xa] = S20_CONSTANTS[0x2];
  in[0xb] = sk[0x4];

  in[0xc] = sk[0x5];  // row3
  in[0xd] = sk[0x6];
  in[0xe] = sk[0x7];
  in[0xf] = S20_CONSTANTS[0x3];

  u32 out[N*N];
  u8* out8  = (u8*)out;
  u8* ptxt8 = (u8*)ptxt;
  u8* ctxt8 = (u8*)ctxt;
  for(;  0<bdim;  bdim-=4*N*N, ptxt8+=4*N*N, ctxt8+=4*N*N){
    // 1) hash the s20 64-byte blk
    printf("block \x1b[35m");  for(int i=0; i<(N*N - N*N/2 - N)/2; ++i) printf(" %08x",in[0x08+i]);  putchar(0x0a);
    s20_show(in, "in");
    s20_blk(in,out);
    s20_show(out, "out");
    s20_show((u32*)ptxt8, "ptxt");

    // 2) increase the 64-bit counter
    in[0x8] += 1;
    if(in[0x8]==0x00000000)  in[0x9] += 1;

    // 3) encrypt @ptxt
    for(int i=0; i<m_min(4*N*N,bdim); ++i)  ctxt8[i] = ptxt8[i] ^ out8[i];
    s20_show((u32*)ctxt8, "ctxt");  // NOTE! if @bdim is under 4*N*N, the extra bytes are invalid, so we're reading out-of-bounds mem. but i'll allow it
    putchar(0x0a);
  }
}

// ----------------------------------------------------------------------------------------------------------------------------# @blk1
#define mmap_write(bdim,data, pos){  memcpy(pos, data,bdim); pos+=bdim;  }

int main(int nargs, char* args[]){
  u32 SK[N*N/2] = {0x00000001,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000};

  u32 nonce[(N*N - N*N/2 - N)/2] = {};  struct timespec ep; clock_gettime(CLOCK_REALTIME, &ep);
#if 1  // 0) automatic nonce
  u64 eps = 1000000000ull*ep.tv_sec + ep.tv_nsec;
  nonce[0x0] = eps>>0x00 & 0xffffffff;
  nonce[0x1] = eps>>0x20 & 0xffffffff;
#else  // 1) manual nonce
  nonce[0x00] = 0x00000001;
  nonce[0x01] = 0x00000000;
#endif

  int    i_fd = open("msg.txt",O_RDONLY);  m_chks(i_fd);
  struct stat i_stat; fstat(i_fd,&i_stat);
  void*  i_data = mmap(NULL,i_stat.st_size, PROT_READ,MAP_SHARED, i_fd,0);

  u32* y = malloc(i_stat.st_size);

  m_sep(); puts("\x1b[91mencipher \x1b[0m(each 32-bit word is rendered as a big-endian base16 integer: most-significant nibble/digit first)\n");
  s20_encrypt(SK,nonce, i_stat.st_size,i_data,y);  // encrypt

  printf("\x1b[31msk   \x1b[91m:\x1b[0m");  m_fori(i, 0,sizeof(SK)    /4) printf(" 0x%s", fmtu32hbe(SK   [i]));  putchar(0x0a);  // secret key
  printf("\x1b[32mnonce\x1b[91m:\x1b[0m");  m_fori(i, 0,sizeof(nonce) /4) printf(" 0x%s", fmtu32hbe(nonce[i]));  putchar(0x0a);  // nonce
  printf("\x1b[94mptxt \x1b[91m:\x1b[0m");  printf(" %s\n",i_data);                                                              // "plaintext"  (just the input  to the salsa enciphering/deciphering function (it's the same function to encipher or decipher); it's the plaintext  if enciphering, and it's ciphertext    if deciphering)
  printf("\x1b[35mctxt \x1b[91m:\x1b[0m");  m_fori(i, 0,i_stat.st_size/4) printf(" 0x%s", fmtu32hbe(y[i]));      putchar(0x0a);  // "ciphertext" (just the output of the salso enciphering/deciphering function (it's the same function to encipher or decipher); it's the ciphertext if enciphering, and it's the plaintext if deciphering)

  // ----------------------------------------------------------------
#if 1  // enable this to write the nonce+ciphertext to a file (in mixed ascii/rawbin format)
  char o_path[PATH_MAX]={0x00};  snprintf(o_path,sizeof(o_path)-1, "data_%s.s20", datestr());
  int  o_fd = open(o_path, O_RDWR|O_CREAT, 0b110110000);  m_chks(o_fd);
  m_chks(ftruncate(o_fd, 7+sizeof(nonce)+1 + 7+i_stat.st_size+1));
  void* o_data = mmap(NULL,i_stat.st_size, PROT_READ|PROT_WRITE,MAP_SHARED, o_fd,0);  m_chks(o_data);
  m_chks(close(o_fd));

  void* o_pos = o_data;
  mmap_write(7,"nonce: ", o_pos);  mmap_write(sizeof(nonce), nonce, o_pos);  *(u8*)o_pos='\n'; o_pos+=1;  // nonce       // mmap_write(6,"nonce:", o_pos);  m_fori(i, 0,sizeof(nonce) /4){  mmap_write(3," 0x", o_pos); *(u64*)o_pos = *(u64*)fmtu32hbe(nonce[i]); o_pos+=8;  }  mmap_write(1,"\n", o_pos);  // nonce
  mmap_write(7,"ctxt : ", o_pos);  mmap_write(i_stat.st_size,    y, o_pos);  *(u8*)o_pos='\n'; o_pos+=1;  // ciphertext  // mmap_write(6,"ctxt :", o_pos);  m_fori(i, 0,i_stat.st_size/4){  mmap_write(3," 0x", o_pos); *(u64*)o_pos = *(u64*)fmtu32hbe(y    [i]); o_pos+=8;  }  mmap_write(1,"\n", o_pos);  // ciphertext
  m_chks(munmap(o_data,i_stat.st_size));
#endif

  // ----------------------------------------------------------------
#if 1  // enable this to decrypt
  u32* z = malloc(i_stat.st_size);

  m_sep(); puts("\x1b[91mdecipher \x1b[0m(each 32-bit word is rendered as a big-endian base16 integer: most-significant nibble/digit first)\n");
  s20_encrypt(SK,nonce, i_stat.st_size,y,z);  // decrypt

  printf("\x1b[31msk   \x1b[91m:\x1b[0m");  m_fori(i, 0,sizeof(SK)    /4) printf(" 0x%s", fmtu32hbe(SK   [i]));  putchar(0x0a);
  printf("\x1b[32mnonce\x1b[91m:\x1b[0m");  m_fori(i, 0,sizeof(nonce) /4) printf(" 0x%s", fmtu32hbe(nonce[i]));  putchar(0x0a);
  printf("\x1b[94mptxt \x1b[91m:\x1b[0m");  m_fori(i, 0,i_stat.st_size/4) printf(" 0x%s", fmtu32hbe(y    [i]));  putchar(0x0a);
  printf("\x1b[35mctxt \x1b[91m:\x1b[0m");  printf(" %s\n",z);  // m_fori(i, 0,i_stat.st_size/4) printf(" %s", fmtu32hbe(((u32*)x)[i]));  putchar(0x0a);
#endif

  // ----------------------------------------------------------------
  free(y);
}
