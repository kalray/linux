==========
Exceptions
==========

On kvx, handlers are set using ``$ev`` (exception vector) register which
specifies a base address.
An offset is added to ``$ev`` upon exception and the result is used as
"Next $pc".
The offset depends on which exception vector the cpu wants to jump to:

  ============== =========
  ``$ev + 0x00`` debug
  ``$ev + 0x40`` trap
  ``$ev + 0x80`` interrupt
  ``$ev + 0xc0`` syscall
  ============== =========

Then, handlers are laid in the following order::

             _____________
            |             |
            |   Syscall   |
            |_____________|
            |             |
            |  Interrupts |
            |_____________|
            |             |
            |    Traps    |
            |_____________|
            |             | ^
            |    Debug    | | Stride
    BASE -> |_____________| v


Interrupts, and traps are serviced similarly, ie:

 - Jump to handler
 - Save all registers
 - Prepare the call (do_IRQ or trap_handler)
 - restore all registers
 - return from exception

entry.S file is (as for other architectures) the entry point into the kernel.
It contains all assembly routines related to interrupts/traps/syscall.

Syscall handling
----------------

When executing a syscall, it must be done using "scall $r6"
where $r6 contains the syscall number. Using this convention allow to
modify and restart a syscall from the kernel.

Syscalls are handled differently than interrupts/exceptions. From an ABI
point of view, scalls are like function calls: any caller saved register
can be clobbered by the syscall. However, syscall parameters are passed
using registers r0 through r7. These registers must be preserved to avoid
cloberring them before the actual syscall function.

On syscall from userspace (scall instruction), the processor will put
the syscall number in $es.sn and switch from user to kernel privilege
mode. kvx_syscall_handler will be called in kernel mode.

The following steps are then taken:

 1. Switch to kernel stack
 2. Extract syscall number
 3. Check that the syscall number is not bogus

    - If so, set syscall func to a not implemented one

 4. Check if tracing is enabled

    - If so, jump to trace_syscall_enter
    - Save syscall arguments (r0 -> r7) on stack in pt_regs
    - Call do_trace_syscall_enter function

 5. Restore syscall arguments since they have been modified by C call
 6. Call the syscall function
 7. Save $r0 in pt_regs since it can be cloberred afterward
 8. If tracing was enabled, call trace_syscall_exit
 9. Call work_pending
 10. Return to user !

The trace call is handled out of the fast path. All slow path handling
is done in another part of code to avoid messing with the cache.

Signals
-------

Signals are handled when exiting kernel before returning to user.
When handling a signal, the path is the following:

 1. User application is executing normally
    Then any exception happens (syscall, interrupt, trap)
 2. The exception handling path is taken
    and before returning to user, pending signals are checked
 3. Signal are handled by do_signal
    Registers are saved and a special part of the stack is modified
    to create a trampoline to call rt_sigreturn
    $spc is modified to jump to user signal handler
    $ra is modified to jump to sigreturn trampoline directly after
    returning from user signal handler.
 4. User signal handler is called after rfe from exception
    when returning, $ra is retored to $pc, resulting in a call
    to the syscall trampoline.
 5. syscall trampoline is executed, leading to rt_sigreturn syscall
 6. rt_sigreturn syscall is executed
    Previous registers are restored to allow returning to user correctly
 7. User application is restored at the exact point it was interrupted
    before.

::

        +----------+
        |    1     |
        | User app | @func
        |  (user)  |
        +---+------+
            |
            | it/trap/scall
            |
        +---v-------+
        |    2      |
        | exception |
        | handling  |
        | (kernel)  |
        +---+-------+
            |
            | Check if signal are pending, if so, handle signals
            |
        +---v--------+
        |    3       |
        | do_signal  |
        |  handling  |
        |  (kernel)  |
        +----+-------+
             |
             | Return to user signal handler
             |
        +----v------+
        |    4      |
        |  signal   |
        |  handler  |
        |  (user)   |
        +----+------+
             |
             | Return to sigreturn trampoline
             |
        +----v-------+
        |    5       |
        |  syscall   |
        |rt_sigreturn|
        |  (user)    |
        +----+-------+
             |
             | Syscall to rt_sigreturn
             |
        +----v-------+
        |    6       |
        |  sigreturn |
        |  handler   |
        |  (kernel)  |
        +----+-------+
             |
             | Modify context to return to original func
             |
        +----v-----+
        |    7     |
        | User app | @func
        |  (user)  |
        +----------+


Registers handling
------------------

MMU is disabled in all exceptions paths, during register save and restoration.
This will prevent from triggering MMU fault (such as TLB miss) which could
clobber the current register state. Such event can occurs when RWX mode is
enabled and the memory accessed to save register can trigger a TLB miss.
Aside from that which is common for all exceptions path, registers are saved
differently regarding the type of exception.

Interrupts and traps
--------------------

When interrupt and traps are triggered, we only save the caller-saved registers.
Indeed, we rely on the fact that C code will save and restore callee-saved and
hence, there is no need to save them. This path is the following::

       +------------+          +-----------+        +---------------+
  IT   | Save caller| C Call   | Execute C |  Ret   | Restore caller| Ret from IT
  +--->+   saved    +--------->+  handler  +------->+     saved     +----->
       | registers  |          +-----------+        |   registers   |
       +------------+                               +---------------+

However, when returning to user, we check if there is work_pending. If a signal
is pending and there is a signal handler to be called, then we need all
registers to be saved on the stack in the pt_regs before executing the signal
handler and restored after that. Since we only saved caller-saved registers, we
need to also save callee-saved registers to restore them correctly when
returning to user. This path is the following (a bit more complicated !)::

        +------------+
        | Save caller|          +-----------+  Ret   +------------+
   IT   |   saved    | C Call   | Execute C | to asm | Check work |
   +--->+ registers  +--------->+  handler  +------->+   pending  |
        | to pt_regs |          +-----------+        +--+---+-----+
        +------------+                                  |   |
                          Work pending                  |   | No work pending
           +--------------------------------------------+   |
           |                                                |
           |                                   +------------+
           v                                   |
    +------+------+                            v
    | Save callee |                    +-------+-------+
    |   saved     |                    | Restore caller|  RFE from IT
    | registers   |                    |     saved     +------->
    | to pt_regs  |                    |   registers   |
    +--+-------+--+                    | from pt_regs  |
       |       |                       +-------+-------+
       |       |         +---------+           ^
       |       |         | Execute |           |
       |       +-------->+ needed  +-----------+
       |                 |  work   |
       |                 +---------+
       |Signal handler ?
       v
  +----+----------+ RFE to user +-------------+       +--------------+
  |   Copy all    | handler     |  Execute    |  ret  | rt_sigreturn |
  |   registers   +------------>+ user signal +------>+ trampoline   |
  | from pt_regs  |             |  handler    |       |  to kernel   |
  | to user stack |             +-------------+       +------+-------+
  +---------------+                                          |
                           syscall rt_sigreturn              |
           +-------------------------------------------------+
           |
           v
  +--------+-------+                      +-------------+
  |   Recopy all   |                      | Restore all |  RFE
  | registers from +--------------------->+    saved    +------->
  |   user stack   |       Return         |  registers  |
  |   to pt_regs   |    from sigreturn    |from pt_regs |
  +----------------+  (via ret_from_fork) +-------------+


Syscalls
--------

As explained before, for syscalls, we can use whatever callee-saved registers
we want since syscall are seen as a "classic" call from ABI pov.
Only different path is the one for clone. For this path, since the child expects
to find same callee-registers content than his parent, we must save them before
executing the clone syscall and restore them after that for the child. This is
done via a redefinition of __sys_clone in assembly which will be called in place
of the standard sys_clone. This new call will save callee saved registers
in pt_regs. Parent will return using the syscall standard path. Freshly spawned
child however will be woken up via ret_from_fork which will restore all
registers (even if caller saved are not needed).
