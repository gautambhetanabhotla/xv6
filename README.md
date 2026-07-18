# Enhancing xv6

## New system call - `int getsyscount(int syscall_no)`

Returns the number of times the system call with number `syscall_no` was called by the calling process.

A user program (`syscount`) has been added to test this functionality.
