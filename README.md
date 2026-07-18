# Enhancing xv6

## New system call - `int getsyscount(int syscall_no)`

Returns the number of times the system call with number `syscall_no` was called by the calling process.

A user program (`syscount`) has been added to test this functionality.

## New system calls - `sigalarm` and `sigreturn`

Set alarms and alarm handlers. Calling `sigalarm(interval, handler)` lets you makes the OS notify you every `interval` ticks, by calling `handler`.
In your handler, call `sigreturn` to return to your regular code execution context.

[Webpage](https://pdos.csail.mit.edu/6.1810/2024/labs/traps.html)
