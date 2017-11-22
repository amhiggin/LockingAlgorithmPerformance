#include "MCSLock.h"
#include <intrin.h>
using namespace std;

template <class QNode>

void* ALIGNEDMA<QNode>::operator new(size_t sz) { // aligned memory allocator
	sz = (sz + lineSz - 1) / lineSz * lineSz; // make sz a multiple of lineSz
	return _aligned_malloc(sz, lineSz); // allocate on a lineSz boundary
}

void ALIGNEDMA<QNode>::operator delete(void *p) {
	_aligned_free(p); // free object
}

MCSLock::MCSLock(){
	lock = NULL;
}

void MCSLock::acquire(QNode **lock) {
	volatile QNode *qn = (QNode*)TlsGetValue(tlsIndex);
	qn->next = NULL;
	volatile QNode *pred = (QNode*)InterlockedExchangePointer((PVOID*)lock, (PVOID)qn);
	if (pred == NULL)
		return; // have lock
	qn->waiting = 1;
	pred->next = qn;
	while (qn->waiting);
}

void MCSLock::release(QNode **lock) {
	volatile QNode *qn = (QNode*)TlsGetValue(tlsIndex);
	volatile QNode *succ;
	if (!(succ = qn->next)) {
		if (InterlockedCompareExchangePointer((PVOID*)lock, NULL, (PVOID)qn) == qn)
			return;
		while ((succ = qn->next) == NULL); 
	}
	succ->waiting = 0;
}