#include "BakeryLock.h"

void BakeryLock::acquire(int pid) {
	choosing[pid] = 1;
	unsigned long long max = 0;
	for (int i = 0; i <threads; i++) { 
		if (number[i] > max)
			max = number[i];
	}
	number[pid] = max + 1; 
	choosing[pid] = 0;
	//_mm_mfence();
	for (int j = 0; j < threads; j++) { 
		while (choosing[j]);
		while ((number[j] != 0) && ((number[j] < number[pid]) || ((number[j] == number[pid]) && (j < pid))));
	}
}
void BakeryLock::release(int pid) {
	number[pid] = 0; // release lock
	//_mm_mfence();
}

void BakeryLock::setThreads(int _threads) {
	threads = _threads;
}

void BakeryLock::resetNumbers() {
	for (int i = 0; i<maxThreads; i++) {
		number[i] = 0;
	}
}