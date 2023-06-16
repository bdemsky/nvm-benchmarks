#ifndef __COMMON_H__ 
#define __COMMON_H__ 

#include "random"

extern "C" {
	inline void * getRegionFromID(uint ID) {
		if (rand() % 2 == 1)
			return new int(1);
		else
			return NULL;
	}
	inline void setRegionFromID(uint ID, void *ptr) {
		int x = 1;
	}

	inline void jaaru_ignore_analysis(char * addrs, size_t size) {}
	inline void jaaru_recovery_procedure_begin() {}
	inline void jaaru_recovery_procedure_end() {}
}

#endif
