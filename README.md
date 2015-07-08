# Process Level Redundancy (PLR)
An Implementation of Software Transient Fault Tolerance (Detection &amp; Recovery) through Process Level Redundancy (PLR)

More complete description TBD

# Limitations
* Only supports single-threaded programs.
* Programs which make system calls directly (using 'int 0x80' or 'syscall') rather than passing through glibc will likely work incorrectly, or at best have incomplete protection. This is because syscalls are intercepted at the glibc level using LD_PRELOAD rather than hooking them in the kernel.

# Todo List
* STDIN & STDOUT redirection between figurehead to/from redundant processes
