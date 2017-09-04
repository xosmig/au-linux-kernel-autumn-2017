#ifndef _LL_ASSERT_H
#define _LL_ASSERT_H

#define assert(expr) \
    if(unlikely(!(expr))) { \
        panic("Assertion failed! %s,%s,%s,line=%d\n", \
        #expr, __FILE__, __FUNCTION__, __LINE__); \
    }

#endif //_LL_ASSERT_H
