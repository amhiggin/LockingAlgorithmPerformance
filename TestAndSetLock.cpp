#include "TestAndSetLock.h"
#include "stdafx.h"
#include <intrin.h>
using namespace std;


void TestAndSetLock::acquire() {
	d = 1; // initialise back off delay
	while (InterlockedExchange64(&lock, 1)) { // if unsuccessful…
		delay(d); // delay d time units
		d *= 2; // exponential back off
	}
}

void TestAndSetLock::release() {
	lock = 0;
}