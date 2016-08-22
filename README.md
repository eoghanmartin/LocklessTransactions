# LocklessTransactions

An investigation into the efficiency of the use of the TSX extension for Intel IA32 Haswell hardware transactional memory.
Random operations were run on a binary search tree (either an add or remove of a random node) using HLE and RTM transactions and a TestAndTestAndSet lock on each operation.
The operations per second were recorded and the results are contained in the .csv files.

Run command:
```
g++ -o outputFile sharing.cpp helper.cpp -mrtm -mrdrnd -O3 -pthread
```

**TODO:** Fix RTM operation.

This project is comprised of three main parts:
1. binary search tree protected by a lock
2. lockless binary search tree using HLE
3. lockless binary tree using RTM.

## Binary Search Tree and TestAndTestAndSet Lock

This binary search tree uses an iterative implementation over a recursive one. The tree is initialised as a global BST variable called `BinarySearchTree` and the remove and add operations are used on the tree in a random fashion with the acquire and release functions for the implementation of the TATAS lock. The `runOp()` function passes a random value to `add()` or `remove()` from the tree and a random bit that determines if the add or remove functions should be used. The tree is not pre-filled. If a value that is not contained in the tree is used in the remove function, the function will just return and no changes will be made. This will however still count as an operation.
A BST class is used for the tree and a Node class is used for the nodes. Within these classes, the variables had to be made volatile as other threads may be interacting with them. This is a source of reducing the efficiency of the program.

## HLE Implementation

The HLE implementation is similar to that of the `TestAndTestAndSet` lock however instead of the atomic function `InterlockedExchange(...)` being used, the relative hardware lock elision function is used from the TSX interface. This is the same for releasing the lock.
```
void BST::acquireHLE() {
    while (_InterlockedExchange_HLEAcquire(&lock, 1) == 1){
        do {
            _mm_pause();
        } while (lock == 1);
    }
}

void BST::releaseHLE() {
    _Store_HLERelease(&lock, 0);
}
```
## RTM Implementation

The RTM lockless implementation of this algorithm is a bit more tricky. In exploring restricted transactional memory, the code was fully fleshed out for the `add()` and `remove()` functions manually. Some poor code practice here involves a duplicate of the critical section for each function, one for the transactional path and one for the non transactional path.

**TODO:** Fix RTM operation.

![alt RTM Implementation](https://github.com/eoghanmartin/LocklessTransactions/blob/master/images/RTMImplementation.png)

As a result of the messy nature of the code used to implement the BST operations with RTM, the chart above may make the implementation method somewhat more clear. If the lock is set, the transaction aborts. If it `reaches _xend()` or `lock = 0`, it has completed the operation successfully. The transactionState variable is used to decide whether the execution can enter the critical section transactionally or whether it must use the TATAS lock to get the lock and then enter the critical section.

## Results

The outputted results for these implementations do not match those to be expected. I would have expected the RTM implementation to be much faster however the results show it to be very similar to the TATAS implementation. This may suggest that the RTM implementation was entering the non transactional path a bit too much and was not using the optimistic transactions to carry out the operations enough.

![alt tag](http://url/to/img.png)

![alt tag](http://url/to/img.png)

![alt tag](http://url/to/img.png)

![alt tag](http://url/to/img.png)

![alt tag](http://url/to/img.png)
