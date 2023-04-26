#pragma once
#ifdef KS_DEBUG
#define DEBUG_NEW new(_NORMAL_BLOCK,__FILE__,__LINE__) 
#else
#define	DEBUG_NEW new
#endif	