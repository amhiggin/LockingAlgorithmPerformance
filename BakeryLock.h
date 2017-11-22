#pragma once
#ifndef BAKERY_LOCK
#define BAKERY_LOCK	
using namespace std;
class BakeryLock
{
public:
	static const int maxThreads = 16;
	unsigned long long number[maxThreads]; 
	int choosing[maxThreads];
	int threads;

	void acquire(int pid);
	void release(int pid);
	void setThreads(int _threads);
	void resetNumbers();
};
#endif