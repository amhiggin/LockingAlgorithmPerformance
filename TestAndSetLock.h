#pragma once
#ifndef TESTANDSET_LOCK
#define TESTANDSET_LOCK

class TestAndSetLock {
public:
	volatile long lock;

	void acquire();
	void release();
};
#endif
