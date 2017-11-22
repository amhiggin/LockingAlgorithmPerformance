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
// 0::InterlockedIncrement
// 1::BakeryLock
// 2::TestAndTestAndSetLock
// 3::MCSLock
//

#define LOCKTYP       0                           // set op type

#if LOCKTYP == 0
#define LOCKSTR       "inc"
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
#define INC(g)		{                                                                           
						bakeryLock.acquire(thread);
						(*g)++;
						bakeryLock.release(thread);
					}

#elif LOCKTYP == 3
#define LOCKSTR		"TESTANDTESTANDSET_LOCK"
#define INC(g)		{
						testAndTestAndSetLock.acquire();
						(*g)++;
						testAndTestAndSetLock.release();
					}
#elif LOCKTYP == 4
#define LOCKSTR		"MCS_LOCK"
#define INC(g)		{
						mcsLock.acquire(lock, tlsIndex);
						(*g)++;
						mcsLock.release(lock, tlsIndex);
					}
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
	UINT64 rt;                                  // run time (ms)
	UINT64 ops;                                 // ops
	UINT64 incs;                                // should be equal ops
	UINT64 aborts;                              //
} Result;

Result *r;                                      // results
UINT indx;                                      // results index

volatile VINT *g;                               // NB: position of volatile

												//
												// test memory allocation [see lecture notes]
												//
ALIGN(64) UINT64 cnt0;
ALIGN(64) UINT64 cnt1;
ALIGN(64) UINT64 cnt2;
UINT64 cnt3;                                    // NB: in Debug mode allocated in cache line occupied by cnt0

												//
												// PMS
												//
#ifdef USEPMS

UINT64 *fixedCtr0;                              // fixed counter 0 counts
UINT64 *fixedCtr1;                              // fixed counter 1 counts
UINT64 *fixedCtr2;                              // fixed counter 2 counts
UINT64 *pmc0;                                   // performance counter 0 counts
UINT64 *pmc1;                                   // performance counter 1 counts
UINT64 *pmc2;                                   // performance counter 2 counts
UINT64 *pmc3;                                   // performance counter 2 counts

												//
												// zeroCounters
												//
void zeroCounters()
{
	for (UINT i = 0; i < ncpu; i++) {
		for (int j = 0; j < 4; j++) {
			if (j < 3)
				writeFIXED_CTR(i, j, 0);
			writePMC(i, j, 0);
		}
	}
}

//
// void setupCounters()
//
void setupCounters()
{
	if (!openPMS())
		quit();

	//
	// enable FIXED counters
	//
	for (UINT i = 0; i < ncpu; i++) {
		writeFIXED_CTR_CTRL(i, (FIXED_CTR_RING123 << 8) | (FIXED_CTR_RING123 << 4) | FIXED_CTR_RING123);
		writePERF_GLOBAL_CTRL(i, (0x07ULL << 32) | 0x0f);
	}

}

//
// void saveCounters()
//
void saveCounters()
{
	for (UINT i = 0; i < ncpu; i++) {
		fixedCtr0[indx*ncpu + i] = readFIXED_CTR(i, 0);
		fixedCtr1[indx*ncpu + i] = readFIXED_CTR(i, 1);
		fixedCtr2[indx*ncpu + i] = readFIXED_CTR(i, 2);
		pmc0[indx*ncpu + i] = readPMC(i, 0);
		pmc1[indx*ncpu + i] = readPMC(i, 1);
		pmc2[indx*ncpu + i] = readPMC(i, 2);
		pmc3[indx*ncpu + i] = readPMC(i, 3);
	}
}

#endif

//
// worker
//
WORKER worker(void *vthread)
{
	int thread = (int)((size_t)vthread);

	UINT64 n = 0;

	volatile VINT *gt = GINDX(thread);
	volatile VINT *gs = GINDX(maxThread);

	runThreadOnCPU(thread % ncpu);

	while (1) {

		//
		// do some work
		//
		for (int i = 0; i < NOPS / 4; i++) {

			switch (sharing) {
			case 0:

				INC(gt);
				INC(gt);
				INC(gt);
				INC(gt);
				break;

			case 25:
				INC(gt);
				INC(gt);
				INC(gt);
				INC(gs);
				break;

			case 50:
				INC(gt);
				INC(gs);
				INC(gt);
				INC(gs);
				break;

			case 75:
				INC(gt);
				INC(gs);
				INC(gs);
				INC(gs);
				break;

			case 100:
				INC(gs);
				INC(gs);
				INC(gs);
				INC(gs);

			}
		}
		n += NOPS;

		//
		// check if runtime exceeded
		//
		if ((getWallClockMS() - tstart) > NSECONDS * 1000)
			break;

	}

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

								//
								// get date
								//
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
	cout << " NOPS=" << NOPS << " NSECONDS=" << NSECONDS << " LOCKTYP=" << LOCKTYP;
#ifdef USEPMS
	cout << " USEPMS";
#endif
	cout << endl;
	cout << "Intel" << (cpu64bit() ? "64" : "32") << " family " << cpuFamily() << " model " << cpuModel() << " stepping " << cpuStepping() << " " << cpuBrandString() << endl;
#ifdef USEPMS
	cout << "performance monitoring version " << pmversion() << ", " << nfixedCtr() << " x " << fixedCtrW() << "bit fixed counters, " << npmc() << " x " << pmcW() << "bit performance counters" << endl;
#endif

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

#ifdef USEPMS

	fixedCtr0 = (UINT64*)ALIGNED_MALLOC(5 * maxThread*ncpu * sizeof(UINT64), lineSz);      // for fixed counter 0 results
	fixedCtr1 = (UINT64*)ALIGNED_MALLOC(5 * maxThread*ncpu * sizeof(UINT64), lineSz);      // for fixed counter 1 results
	fixedCtr2 = (UINT64*)ALIGNED_MALLOC(5 * maxThread*ncpu * sizeof(UINT64), lineSz);      // for fixed counter 2 results
	pmc0 = (UINT64*)ALIGNED_MALLOC(5 * maxThread*ncpu * sizeof(UINT64), lineSz);           // for performance counter 0 results
	pmc1 = (UINT64*)ALIGNED_MALLOC(5 * maxThread*ncpu * sizeof(UINT64), lineSz);           // for performance counter 1 results
	pmc2 = (UINT64*)ALIGNED_MALLOC(5 * maxThread*ncpu * sizeof(UINT64), lineSz);           // for performance counter 2 results
	pmc3 = (UINT64*)ALIGNED_MALLOC(5 * maxThread*ncpu * sizeof(UINT64), lineSz);           // for performance counter 3 results

#endif

	r = (Result*)ALIGNED_MALLOC(5 * maxThread * sizeof(Result), lineSz);                   // for results
	memset(r, 0, 5 * maxThread * sizeof(Result));                                           // zero

	indx = 0;

#ifdef USEPMS
	//
	// set up performance monitor counters
	//
	setupCounters();
#endif

	//
	// use thousands comma separator
	//
	setCommaLocale();

	//
	// header
	//
	cout << "sharing";
	cout << setw(4) << "nt";
	cout << setw(6) << "rt";
	cout << setw(16) << "ops";
	cout << setw(6) << "rel";
	cout << endl;

	cout << "-------";              // sharing
	cout << setw(4) << "--";        // nt
	cout << setw(6) << "--";        // rt
	cout << setw(16) << "---";      // ops
	cout << setw(6) << "---";       // rel
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

	for (sharing = 0; sharing <= 100; sharing += 25) {

		for (int nt = 1; nt <= maxThread; nt *= 2, indx++) {

			//
			//  zero shared memory
			//
			for (int thread = 0; thread < nt; thread++)
				*(GINDX(thread)) = 0;   // thread local
			*(GINDX(maxThread)) = 0;    // shared

#ifdef USEPMS
			zeroCounters();             // zero PMS counters
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

#ifdef USEPMS
			saveCounters();             // save PMS counters
#endif

										//
										// save results and output summary to console
										//
			for (int thread = 0; thread < nt; thread++) {
				r[indx].ops += ops[thread];
				r[indx].incs += *(GINDX(thread));
			}
			r[indx].incs += *(GINDX(maxThread));
			if ((sharing == 0) && (nt == 1))
				ops1 = r[indx].ops;
			r[indx].sharing = sharing;
			r[indx].nt = nt;
			r[indx].rt = rt;

			cout << setw(6) << sharing << "%";
			cout << setw(4) << nt;
			cout << setw(6) << fixed << setprecision(2) << (double)rt / 1000;
			cout << setw(16) << r[indx].ops;
			cout << setw(6) << fixed << setprecision(2) << (double)r[indx].ops / ops1;

			if (r[indx].ops != r[indx].incs)
				cout << " ERROR incs " << setw(3) << fixed << setprecision(0) << 100.0 * r[indx].incs / r[indx].ops << "% effective";

			cout << endl;

			//
			// delete thread handles
			//
			for (int thread = 0; thread < nt; thread++)
				closeThread(threadH[thread]);

		}

	}

	cout << endl;

	//
	// output results so they can easily be pasted into a spread sheet from console window
	//
	setLocale();
	cout << "sharing/nt/rt/ops/incs";
	cout << endl;
	for (UINT i = 0; i < indx; i++) {
		cout << r[i].sharing << "/" << r[i].nt << "/" << r[i].rt << "/" << r[i].ops << "/" << r[i].incs;
		cout << endl;
	}
	cout << endl;

#ifdef USEPMS

	//
	// output PMS counters
	//
	cout << "FIXED_CTR0 instructions retired" << endl;
	for (UINT i = 0; i < indx; i++) {
		for (UINT j = 0; j < ncpu; j++)
			cout << ((j) ? "/" : "") << fixedCtr0[i*ncpu + j];
		cout << endl;
	}
	cout << endl;
	cout << "FIXED_CTR1 unhalted core cycles" << endl;
	for (UINT i = 0; i < indx; i++) {
		for (UINT j = 0; j < ncpu; j++)
			cout << ((j) ? "/" : "") << fixedCtr1[i*ncpu + j];
		cout << endl;
	}
	cout << endl;
	cout << "FIXED_CTR2 unhalted reference cycles" << endl;
	for (UINT i = 0; i < indx; i++) {
		for (UINT j = 0; j < ncpu; j++)
			cout << ((j) ? "/" : "") << fixedCtr2[i*ncpu + j];
		cout << endl;
	}
	cout << endl;
	cout << "PMC0 RTM RETIRED START" << endl;
	for (UINT i = 0; i < indx; i++) {
		for (UINT j = 0; j < ncpu; j++)
			cout << ((j) ? "/" : "") << pmc0[i*ncpu + j];
		cout << endl;
	}
	cout << endl;
	cout << "PMC1 RTM RETIRED COMMIT" << endl;
	for (UINT i = 0; i < indx; i++) {
		for (UINT j = 0; j < ncpu; j++)
			cout << ((j) ? "/" : "") << pmc1[i*ncpu + j];
		cout << endl;
	}
	cout << endl;
	cout << "PMC2 unhalted core cycles in committed transactions" << endl;
	for (UINT i = 0; i < indx; i++) {
		for (UINT j = 0; j < ncpu; j++)
			cout << ((j) ? "/" : "") << pmc2[i*ncpu + j];
		cout << endl;
	}
	cout << endl;
	cout << "PMC3 unhalted core cycles in committed and aborted transactions" << endl;
	for (UINT i = 0; i < indx; i++) {
		for (UINT j = 0; j < ncpu; j++)
			cout << ((j) ? "/" : "") << pmc3[i*ncpu + j];
		cout << endl;
	}

	closePMS();                 // close PMS counters

#endif

	quit();

	return 0;

}

// eof