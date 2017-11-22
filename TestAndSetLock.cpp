/*
* Test and Set Lock
* Implemented as a performance baseline
* As described in 'Locks', Dr. Jeremy Jones, SCSS TCD
* https://www.scss.tcd.ie/jeremy.jones/CS4021/Locks.pdf
*/

#include "TestAndSetLock.h"
#include "stdafx.h"
#include "intrin.h"
#include <intrin.h>
using namespace std;


void TestAndSetLock::acquire() {
	while (InterlockedExchange(&lock, 1));
}

void TestAndSetLock::release() {
	lock = 0;
}