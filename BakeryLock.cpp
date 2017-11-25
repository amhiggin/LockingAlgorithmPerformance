/*
* Lamport's Bakery Lock
* As described in 'Peterson and Bakery', Dr. Jeremy Jones, SCSS TCD
* https://www.scss.tcd.ie/jeremy.jones/CS4021/Spin%20Peterson%20and%20Bakery.pdf
*/
#include "BakeryLock.h"
#include <Windows.h>
#include <intrin.h>
#include "helper.h"
using namespace std;

void BakeryLock::acquire(int pid) {
	choosing[pid] = 1;
	_mm_mfence();
	unsigned long long max = 0;
	for (int i = 0; i <threads; i++) { 
		if (number[i] > max)
			max = number[i];
	}
	number[pid] = max + 1; 
	choosing[pid] = 0;
	// adding mfence
	_mm_mfence();
	for (int j = 0; j < threads; j++) { 
		while (choosing[j]);
		while ((number[j] != 0) && ((number[j] < number[pid]) || ((number[j] == number[pid]) && (j < pid))));
	}
}
void BakeryLock::release(int pid) {
	number[pid] = 0; 
	// adding another mfence
	_mm_mfence();
}

void BakeryLock::setThreads(int _threads) {
	threads = _threads;
}

void BakeryLock::resetNumbers() {
	for (int i = 0; i<maxThreads; i++) {
		number[i] = 0;
	}
}