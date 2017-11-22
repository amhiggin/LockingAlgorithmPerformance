/*
* Test and Set Lock
* Implemented as a performance baseline
* As described in 'Locks', Dr. Jeremy Jones, SCSS TCD
* https://www.scss.tcd.ie/jeremy.jones/CS4021/Locks.pdf
*/

#include "TestAndTestAndSetLock.h"
#include <Windows.h>
#include "intrin.h"
#include "helper.h"

using namespace std;


void TestAndTestAndSetLock::acquire() {
	// Implementing the optimistic version
	while (InterlockedExchange(&lock, 1)) // try for lock
		while (lock == 1) // wait until lock free
			_mm_pause(); // instrinsic see next slide
}

void TestAndTestAndSetLock::release() {
	lock = 0;
}