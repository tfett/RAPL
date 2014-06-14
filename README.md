RAPL
====

Running Average Power Limit - Power Measurements

We extended rapl-read to measure the energy consumed during the execution of any executable.  This was done by modifying rapl-read to take a command line argument which it then runs via a system call. When the executed argument returns, the total energy consumed during its execution at the CPU package level is printed to stdout.

This works is a modification from the project found here: https://github.com/deater/uarch-configure/tree/master/rapl-read
