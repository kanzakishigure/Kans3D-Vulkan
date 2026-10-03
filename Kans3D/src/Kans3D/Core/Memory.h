#pragma once
#if defined(KS_DEBUG) && defined(_MSC_VER)
#define DEBUG_NEW new (_NORMAL_BLOCK, __FILE__, __LINE__)
#elif defined(KS_DEBUG) && defined(_GNUC_)
#define DEBUG_NEW new
#else
#define DEBUG_NEW new
#endif