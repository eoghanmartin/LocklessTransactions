//
// sharing.cpp
//
// Copyright (C) 2013 - 2015 jones@scss.tcd.ie
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

#include "stdafx.h"                             // pre-compiled headers
#include <iostream>
#include <iomanip>                              // setprecision
#include "helper.h"
#include <math.h>
#include <fstream> 

using namespace std;

#define K           1024
#define GB          (K*K*K)
#define NOPS        1000
#define NSECONDS    1                           // run each test for NSECONDS

#define COUNTER64                               // comment for 32 bit counter

#ifdef COUNTER64
#define VINT    UINT64                          //  64 bit counter
#else
#define VINT    UINT                            //  32 bit counter
#endif

#ifdef FALSESHARING
#define GINDX(n)    (g+n)
#else
#define GINDX(n)    (g+n*lineSz/sizeof(VINT))
#endif

#define ALIGNED_MALLOC(sz, align) _aligned_malloc(sz, align)

UINT64 tstart;                                  // start of test in ms
int sharing;
int lineSz;                                     // cache line size
int maxThread;                                  // max # of threads

THREADH *threadH;                               // thread handles
UINT64 *ops;                                    // for ops per thread

//ALIGN(64) volatile long lock = 0;

class Node {
    public:
        INT64 volatile key;
        Node* volatile left;
        Node* volatile right;
        Node() {key = 0; right = left = NULL;} // default constructor
};

class BST {
    public:
        Node* volatile root; // root of BST, initially NULL
        ALIGN(64) volatile long lock;
        BST() {root = NULL, lock = 0;} // default constructor
        void add(Node *nn); // add node to tree
        void destroy(volatile Node *nextNode);
        void remove(INT64 key); // remove key from tree
        void releaseTATAS();  //HLE functionality added to BST class
        void acquireTATAS();
};

BST *BinarySearchTree = new BST;

void BST::add (Node *n)
{
    acquireTATAS();
    Node* volatile* volatile pp = &root;
    Node* volatile p = root;
    while (p) {
        if (n->key < p->key) {
        pp = &p->left;
        } else if (n->key > p->key) {
            pp = &p->right;
            } else {
                releaseTATAS();
                return;
            }
        p = *pp;
    }
    *pp = n;
    releaseTATAS();
}

void BST::remove(INT64 key)
{
    acquireTATAS();
    Node* volatile* volatile pp = &root;
    Node* volatile p = root;
    while (p) {
        if (key < p->key) {
            pp = &p->left;
        } else if (key > p->key) {
            pp = &p->right;
            } else {
                break;
            }
        p = *pp;
    }
    if (p == NULL)
        releaseTATAS();
        return;
    if (p->left == NULL && p->right == NULL) {
        *pp = NULL; // NO children
    } else if (p->left == NULL) {
        *pp = p->right; // ONE child
    } else if (p->right == NULL) {
        *pp = p->left; // ONE child
    } else {
        Node *r = p->right; // TWO children
        Node* volatile* volatile ppr = &p->right; // find min key in right sub tree
        while (r->left) {
            ppr = &r->left;
            r = r->left;
        }
        p->key = r->key; // could move...
        p = r; // node instead
        *ppr = r->right;
    }
    releaseTATAS();
}

void BST::destroy(volatile Node *nextNode)
{
    if (nextNode != NULL)
    {
        destroy(nextNode->left);
        destroy(nextNode->right);
    }
}

void BST::acquireTATAS() {
    while (InterlockedExchange(&lock, 1) == 1){
        do {
            _mm_pause();
        } while (lock == 1);
    }
}

void BST::releaseTATAS() {
    lock = 0;
}

typedef struct {
    int sharing;                                // sharing
    int nt;                                     // # threads
    UINT64 rt;                                  // run time (ms)
    UINT64 ops;                                 // ops
    UINT64 incs;                                // should be equal ops
} Result;

Result *r;                                      // results
UINT indx;                                      // results index

volatile VINT *g;                               // NB: position of volatile

void runOp(UINT randomValue, UINT randomBit) {
    if (randomBit) {
        Node *addNode = new Node;
        addNode->key = randomValue;
        addNode->left = NULL;
        addNode->right = NULL;
        BinarySearchTree->add(addNode);
    }
    else {
        BinarySearchTree->remove(randomValue);
    }
}
//
// worker
//
WORKER worker(void *vthread)
{
    int thread = (int)((size_t) vthread);

    UINT64 n = 0;

    runThreadOnCPU(thread % ncpu);

    UINT *chooseRandom  = new UINT;
    UINT randomValue;
    UINT randomBit;

    while (1) {
        for(int y=0; y<NOPS; y++) {
            randomBit = 0;
            *chooseRandom = rand(*chooseRandom);
            randomBit = *chooseRandom % 2;
            switch (sharing) {
                case 0:
                    runOp(*chooseRandom % 16, randomBit);
                    break;
                case 1:
                    randomValue = *chooseRandom % 256;
                    runOp(randomValue, randomBit);
                    break;
                case 2:
                    randomValue = *chooseRandom % 4096;
                    runOp(randomValue, randomBit);
                    break;
                case 3:
                    randomValue = *chooseRandom % 65536;
                    runOp(randomValue, randomBit);
                    break;
                case 4:
                    randomValue = *chooseRandom % 1048576;
                    runOp(randomValue, randomBit);
                    break;
            }
        }
        n += NOPS;
        //
        // check if runtime exceeded
        //
        if ((getWallClockMS() - tstart) > NSECONDS*1000)
            break;
    }
    ops[thread] = n;
    BinarySearchTree->destroy(BinarySearchTree->root); //Recursively destroy BST
    BinarySearchTree->root = NULL;
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
    // get cache info
    //
    lineSz = getCacheLineSz();
    //
    // allocate global variable
    //
    // NB: each element in g is stored in a different cache line to stop false sharing
    //
    threadH = (THREADH*) ALIGNED_MALLOC(maxThread*sizeof(THREADH), lineSz);             // thread handles
    ops = (UINT64*) ALIGNED_MALLOC(maxThread*sizeof(UINT64), lineSz);                   // for ops per thread

    g = (VINT*) ALIGNED_MALLOC((maxThread + 1)*lineSz, lineSz);                         // local and shared global variables

    r = (Result*) ALIGNED_MALLOC(5*maxThread*sizeof(Result), lineSz);                   // for results
    memset(r, 0, 5*maxThread*sizeof(Result));                                        // zero

    indx = 0;
    //
    // use thousands comma separator
    //
    setCommaLocale();
    //
    // header
    //
    cout << setw(13) << "BST";
    cout << setw(10) << "nt";
    cout << setw(10) << "rt";
    cout << setw(20) << "ops";
    cout << setw(10) << "rel";
    cout << endl;

    cout << setw(13) << "---";       // random count
    cout << setw(10) << "--";        // nt
    cout << setw(10) << "--";        // rt
    cout << setw(20) << "---";       // ops
    cout << setw(10) << "---";       // rel
    cout << endl;

    //
    // run tests
    //
    UINT64 ops1 = 1;

    for (sharing = 0; sharing < 5; sharing++) {
        for (int nt = 1; nt <= maxThread; nt+=1, indx++) {
            //
            //  zero shared memory
            //
            for (int thread = 0; thread < nt; thread++)
                *(GINDX(thread)) = 0;   // thread local
            *(GINDX(maxThread)) = 0;    // shared
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
            if ((sharing == 0) && (nt == 1))
                ops1 = r[indx].ops;
            r[indx].sharing = sharing;
            r[indx].nt = nt;
            r[indx].rt = rt;

            cout << setw(13) << pow(16,sharing+1);
            cout << setw(10) << nt;
            cout << setw(10) << fixed << setprecision(2) << (double) rt / 1000;
            cout << setw(20) << r[indx].ops;
            cout << setw(10) << fixed << setprecision(2) << (double) r[indx].ops / ops1;
            cout << endl;

            ofstream metrics;
            metrics.open("metricsTATAS.txt", ios_base::app);

            metrics << pow(16,sharing+1) << ", ";
            metrics << nt << ", ";
            metrics << fixed << setprecision(2) << (double)rt / 1000 << ", ";
            metrics << r[indx].ops << ", ";
            metrics << fixed << setprecision(2) << (double)r[indx].ops / ops1;
            metrics << endl;

            metrics.close();

            //
            // delete thread handles
            //
            for (int thread = 0; thread < nt; thread++) {
                closeThread(threadH[thread]);
            }
        }
    }

    cout << endl;
    quit();

    return 0;

}

// eof