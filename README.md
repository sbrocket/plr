# Process Level Redundancy (PLR)
An Implementation of Software Transient Fault Tolerance (Detection &amp; Recovery) through Process Level Redundancy (PLR)

More complete description TBD

## Limitations
* Only supports 3 redundant processes right now. 2 process (i.e. detection w/o recovery) mode will be forthcoming.
* Only supports single-threaded programs.
* Programs which make system calls directly (using 'int 0x80' or 'syscall') rather than passing through glibc will likely work incorrectly, or at best have incomplete protection. This is because syscalls are intercepted at the glibc level using LD_PRELOAD rather than hooking them in the kernel.

## Todo List
* Figure out how to emulate syscalls like read() or write()
  * For read(), should all processes call read() or just one and replicate the return value? For write(), only the latter option will work.
  * However, how will side effects of kernel data - like updating the file offset - be applied to all processes if syscall only called on one? Do the other processes have to lseek() instead of read()/write()?
* Add "inside PLR code" start/end functions inside overridden syscalls
* Use "inside PLR code" per-process flag to determine whether to inject faults
* STDIN & STDOUT redirection between figurehead to/from redundant processes
* Clean up fault cases like multiple faulted processes to exit PLR gracefully

## Known Issues
* Bug: crc32c on open() pathname argument miscompare periodically, despite same string value being crc'd
* Bug: pthread_cond_destroy() fails in plrSD_freeProcData() if proc is currently waiting, e.g. killing detected bad process
