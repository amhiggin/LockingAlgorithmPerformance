#pragma once
#ifndef MCS_LOCK
#define MCS_LOCK

template <class T>
class ALIGNEDMA {
public:
	void* operator new(size_t);
	void operator delete(void*);
};

typedef struct QNode {
public:
	volatile int waiting;
	volatile QNode *next;
} QNode;

class MCSLock {
public:
	QNode *lock;
	MCSLock();
	void acquire(QNode **lock);
	void release(QNode **lock);
};
#endif