#ifndef __UTILS_H__
#define __UTILS_H__

#include <WString.h>

#define MS_FROM_SECONDS(s) (s * 1000)
#define MS_FROM_MINUTES(m) MS_FROM_SECONDS(m * 60)
#define MS_FROM_HOURS(h) MS_FROM_MINUTES(h * 60)


void trim_string(String& str);

#endif //__UTILS_H__