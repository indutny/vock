#ifndef CONFIG_H
#define CONFIG_H

#define CELT_BUILD            1

#define restrict
#define inline __inline
#define getpid _getpid

#define USE_ALLOCA            1

#define OPUS_BUILD            1

#ifndef FIXED_POINT
#define FIXED_POINT           1
#endif // FIXED_POINT

#endif // CONFIG_H
