#pragma once
#ifndef TESTANDTESTANDSET_LOCK
#define TESTANDTESTANDSET_LOCK

class TestAndTestAndSetLock {
public:
	volatile long lock;

	void acquire();
	void release();
};
#endif
