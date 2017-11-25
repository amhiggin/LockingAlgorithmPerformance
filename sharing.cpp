//
// sharing.cpp
//
// Copyright (C) 2013 - 2016 jones@scss.tcd.ie
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software Foundation;
// either version 2 of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
// without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//
// 19/11/12 first version
// 19/11/12 works with Win32 and x64
// 21/11/12 works with Character Set: Not Set, Unicode Character Set or Multi-Byte Character
// 21/11/12 output results so they can be easily pasted into a spreadsheet from console
// 24/12/12 increment using (0) non atomic increment (1) InterlockedIncrement64 (2) InterlockedCompareExchange
// 12/07/13 increment using (3) RTM (restricted transactional memory)
// 18/07/13 added performance counters
// 27/08/13 choice of 32 or 64 bit counters (32 bit can oveflow if run time longer than a couple of seconds)
// 28/08/13 extended struct Result
// 16/09/13 linux support (needs g++ 4.8 or later)
// 21/09/13 added getWallClockMS()
// 12/10/13 Visual Studio 2013 RC
// 12/10/13 added FALSESHARING
// 14/10/14 added USEPMS
//

//
// NB: hints for pasting from console window
// NB: Edit -> Select All followed by Edit -> Copy
// NB: paste into Excel using paste "Use Text Import Wizard" option and select "/" as the delimiter
//

#include <Windows.h>							// pre-compiled headers
#include <iostream>                             // cout
#include <iomanip>                              // setprecision
#include "helper.h"                             //
#include "BakeryLock.h"
#include "MCSLock.h"
#include "TestAndTestAndSetLock.h"

using namespace std;                            // cout

#define K           1024                        //
#define GB          (K*K*K)                     //
#define NOPS        10000                       //
#define NSECONDS    2                           // run each test for NSECONDS

#define COUNTER64                               // comment for 32 bit counter
												//#define FALSESHARING                          // allocate counters in same cache line
												//#define USEPMS                                // use PMS counters

#ifdef COUNTER64
#define VINT    UINT64                          //  64 bit counter
#else
#define VINT    UINT                            //  32 bit counter
#endif

#define ALIGNED_MALLOC(sz, align) _aligned_malloc(sz, align)

#ifdef FALSESHARING
#define GINDX(n)    (g+n)                       //
#else
#define GINDX(n)    (g+n*lineSz/sizeof(VINT))   //
#endif


UINT64 tstart;                                  // start of test in ms
int sharing;                                    // % sharing
int lineSz;                                     // cache line size
int maxThread;                                  // max # of threads

THREADH *threadH;                               // thread handles
UINT64 *ops;                                    // for ops per thread

typedef struct {
	int sharing;                                // sharing
	int nt;                                     // # threads
	UINT64 runTime;                                  // run time (ms)
	UINT64 ops;                                 // ops
	UINT64 incs;                                // should be equal ops
	UINT64 aborts;                              //
} Result;

Result *r;                                      // results
UINT indx;                                      // results index

volatile VINT *g;                               // NB: position of volatile

/*
* GLOBAL VARIABLES
*/
BakeryLock bakeryLock;
TestAndTestAndSetLock testAndTestAndSetLock;
MCSLock mcsLock;
QNode **lock = &mcsLock.lock;
DWORD tlsIndex = TlsAlloc();
int nt = 1;


//
// LOCKTYP
//
// 0::increment
// 1::InterlockedIncrement
// 2::BakeryLock
// 3::TestAndTestAndSetLock
// 4::MCSLock
//
#define LOCKTYP       2                          // set op type

#if LOCKTYP == 0
#define LOCKSTR       "increment"
#define INC(g)      (*g)++;


#elif LOCKTYP == 1
#ifdef COUNTER64
#define LOCKSTR       "InterlockedIncrement64"
#define INC(g)      InterlockedIncrement64((volatile LONG64*) g)
#else
#define LOCKSTR       "InterlockedIncrement"
#define INC(g)      InterlockedIncrement(g)
#endif


#elif LOCKTYP == 2
#define LOCKSTR		"BAKERY_LOCK"
#define INC(g)		incrementBakeryLock(thread);

#elif LOCKTYP == 3
#define LOCKSTR		"TESTANDTESTANDSET_LOCK"
#define INC(g)		incrementTestAndTestAndSetLock();

#elif LOCKTYP == 4
#define LOCKSTR		"MCS_LOCK"
#define INC(g)		incrementMCSLock();
#endif

void incrementTestAndTestAndSetLock() {
	testAndTestAndSetLock.acquire();
	(*g)++;
	testAndTestAndSetLock.release();
}

void incrementBakeryLock(int thread) {
	bakeryLock.acquire(thread);
	(*g)++;
	bakeryLock.release(thread);
}

void incrementMCSLock() {
	mcsLock.acquire(lock, tlsIndex);
	(*g)++;
	mcsLock.release(lock, tlsIndex);
}



//
// test memory allocation [see lecture notes]
//
ALIGN(64) UINT64 cnt0;
ALIGN(64) UINT64 cnt1;
ALIGN(64) UINT64 cnt2;
UINT64 cnt3;                                    // NB: in Debug mode allocated in cache line occupied by cnt0

//
// worker
//
WORKER worker(void *vthread)
{
#if LOCKTYP == 4
	QNode *qn = new QNode();
	TlsSetValue(tlsIndex, qn);
#endif
	
	int thread = (int)((size_t)vthread);

	// counter for number of operations performed by this thread
	UINT64 n = 0;

	volatile VINT *gt = GINDX(thread);
	volatile VINT *gs = GINDX(maxThread);

	do {

		//
		// do some work
		//
		for (int i = 0; i < NOPS; i++) {
			INC(gs);
		}
		n += NOPS;

	} while (!((getWallClockMS() - tstart) > NSECONDS * 1000));

	// update the  number of ops outside the loop to remove possible of false sharing
	ops[thread] = n;
	return 0;

}

//
// main
//
int main()
{
	ncpu = getNumberOfCPUs();   // number of logical CPUs
	maxThread = 2 * ncpu;       // max number of threads

	char dateAndTime[256];
	getDateAndTime(dateAndTime, sizeof(dateAndTime));

	//
	// console output
	//
	cout << getHostName() << " " << getOSName() << " sharing " << (is64bitExe() ? "(64" : "(32") << "bit EXE)";
#ifdef _DEBUG
	cout << " DEBUG";
#else
	cout << " RELEASE";
#endif
	cout << " [" << LOCKSTR << "]" << " NCPUS=" << ncpu << " RAM=" << (getPhysicalMemSz() + GB - 1) / GB << "GB " << dateAndTime << endl;
#ifdef COUNTER64
	cout << "COUNTER64";
#else
	cout << "COUNTER32";
#endif
#ifdef FALSESHARING
	cout << " FALSESHARING";
#endif
	cout << " NOPS=" << NOPS << " NSECONDS=" << NSECONDS << " LOCKSTR=" << LOCKSTR;
	cout << endl;
	cout << "Intel" << (cpu64bit() ? "64" : "32") << " family " << cpuFamily() << " model " << cpuModel() << " stepping " << cpuStepping() << " " << cpuBrandString() << endl;

	//
	// get cache info
	//
	lineSz = getCacheLineSz();
	//lineSz *= 2;

	if ((&cnt3 >= &cnt0) && (&cnt3 < (&cnt0 + lineSz / sizeof(UINT64))))
		cout << "Warning: cnt3 shares cache line used by cnt0" << endl;
	if ((&cnt3 >= &cnt1) && (&cnt3 < (&cnt1 + lineSz / sizeof(UINT64))))
		cout << "Warning: cnt3 shares cache line used by cnt1" << endl;
	if ((&cnt3 >= &cnt2) && (&cnt3 < (&cnt2 + lineSz / sizeof(UINT64))))
		cout << "Warning: cnt2 shares cache line used by cnt1" << endl;

	cout << endl;

	//
	// allocate global variable
	//
	// NB: each element in g is stored in a different cache line to stop false sharing
	//
	threadH = (THREADH*)ALIGNED_MALLOC(maxThread * sizeof(THREADH), lineSz);             // thread handles
	ops = (UINT64*)ALIGNED_MALLOC(maxThread * sizeof(UINT64), lineSz);                   // for ops per thread

#ifdef FALSESHARING
	g = (VINT*)ALIGNED_MALLOC((maxThread + 1) * sizeof(VINT), lineSz);                     // local and shared global variables
#else
	g = (VINT*)ALIGNED_MALLOC((maxThread + 1)*lineSz, lineSz);                         // local and shared global variables
#endif

	r = (Result*)ALIGNED_MALLOC(5 * maxThread * sizeof(Result), lineSz);                   // for results
	memset(r, 0, 5 * maxThread * sizeof(Result));                                           // zero

	indx = 0;
	//
	// use thousands comma separator
	//
	setCommaLocale();

	//
	// header
	//
	cout << "sharing";
	cout << setw(16) << "nt";
	cout << setw(16) << "rt";
	cout << setw(16) << "ops";
	cout << setw(16) << "incs";
	cout << setw(16) << "incs PER SEC";
	cout << setw(16) << "rel";
	cout << endl;
	
	cout << "-------";              // sharing
	cout << setw(16) << "--";        // nt
	cout << setw(16) << "--";        // rt
	cout << setw(16) << "---";      // ops
	cout << setw(16) << "---";      // incs
	cout << setw(16) << "----------";      // incs per sec
	cout << setw(16) << "---";       // rel
	cout << endl;

	//
	// boost process priority
	// boost current thread priority to make sure all threads created before they start to run
	//
#ifdef WIN32
	//  SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
	//  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif

	//
	// run tests
	//
	UINT64 ops1 = 1;
	// want to check the case for 100% sharing only
	sharing = 100;

		for (int nt = 1; nt <= maxThread; nt++, indx++) {

			for (int thread = 0; thread < nt; thread++)
				*(GINDX(thread)) = 0;   // thread local
			*(GINDX(maxThread)) = 0;    // shared
#ifdef LOCKTYP == 2			
			bakeryLock.setThreads(nt);
			bakeryLock.resetNumbers();
#endif										
			//
			// get start time
			//
			tstart = getWallClockMS();

			//
			// create worker threads
			//
			for (int thread = 0; thread < nt; thread++)
				createThread(&threadH[thread], worker, (void*)(size_t)thread);

			//
			// wait for ALL worker threads to finish
			//
			waitForThreadsToFinish(nt, threadH);
			UINT64 rt = getWallClockMS() - tstart;

			//
			// save results and output summary to console
			//
			for (int thread = 0; thread < nt; thread++) {
				r[indx].ops += ops[thread];
				r[indx].incs += *(GINDX(thread));
			}
			r[indx].incs += *(GINDX(maxThread));
			if ((sharing == 100) && (nt == 1))
				ops1 = r[indx].ops;
			r[indx].sharing = sharing;
			r[indx].nt = nt;
			r[indx].runTime = rt;

			cout << sharing << "%";
			cout << setw(16) << nt;
			cout << setw(16) << fixed << setprecision(2) << (double)rt / 1000;
			cout << setw(16) << r[indx].ops;
			cout << setw(16) << r[indx].incs;
			cout << setw(16) << fixed << setprecision(2) << (double)r[indx].incs / ((double)rt / 1000);
			cout << setw(16) << fixed << setprecision(2) << (double)r[indx].ops / ops1;

			if (r[indx].ops != r[indx].incs)
				cout << " ERROR incs " << setw(3) << fixed << setprecision(0) << 100.0 * r[indx].incs / r[indx].ops << "% effective";

			cout << endl;

			//
			// delete thread handles
			//
			for (int thread = 0; thread < nt; thread++)
				closeThread(threadH[thread]);

		}

	cout << endl;

	//
	// output results so they can easily be pasted into a spread sheet getWallClockfrom console window
	//
	setLocale();
	cout << "nt/rt/ops/incs/incspersec";
	cout << endl;
	for (UINT i = 0; i < indx; i++) {
		cout << r[i].nt << "/" << r[i].runTime << "/" << r[i].ops << "/" << r[i].incs << "/" << (r[i].incs / (r[i].runTime / 1000.0));
		cout << endl;
	}
	cout << endl;

	quit();

	return 0;

}

// eof