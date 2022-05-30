===
SMP
===

The Coolidge SoC is comprised of 5 clusters, each organized as a group
of 17 cores: 16 application core (PE) and 1 secure core (RM).
These 17 cores have their L1 cache coherent with the local Tightly
Coupled Memory (TCM or SMEM). The L2 cache is necessary for SMP support
is and implemented with a mix of HW support and SW firmware. The L2 cache
data and meta-data are stored in the TCM.
The RM core is not meant to run Linux and is reserved for implementing
hypervisor services, thus only 16 processors are available for SMP.

Booting
-------

When booting the kvx processor, only the RM is woken up. This RM will
execute a portion of code located in the section named ``.rm_firmware``.
By default, a simple power off code is embedded in this section.
To avoid embedding the firmware in kernel sources, the section is patched
using external tools to add the L2 firmware (and replace the default firmware).
Before executing this firmware, the RM boots the PE0. PE0 will then enable L2
coherency and request will be stalled until RM boots the L2 firmware.

Locking primitives
------------------

spinlock/rwlock are using the kernel standard queued spinlock/rwlocks.
These primitives are based on cmpxch and xchg. More particularly, it uses xchg16
which is implemented as a read modify write with acswap on 32bit word since
kvx does not have atomic cmpxchg instructions for less than 32 bits.

IPI
---

An IPI controller allows to communicate between CPUs using a simple
memory mapped register. This register can simply be written using a
mask to trigger interrupts directly to the cores matching the mask.

