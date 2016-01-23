# LocklessTransactions
An investigation into the efficiency of the use of the TSX extension for Intel IA32 Haswell hardware transactional memory.
Random operations were run on a binary search tree (either an add or remove of a random node) using HLE and RTM transactions and a TestAndTestAndSet lock on each operation.
The operations per second were recorded and the results are contained in the .csv files.

**TODO:** Fix RTM operation.
