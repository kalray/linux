.. SPDX-License-Identifier: GPL-2.0

=====
IOMMU
=====

General Overview
----------------

To exchange data between device and users through memory, the driver
has to  set up a buffer by doing some kernel allocation. The buffer uses
virtual address and the physical address is obtained through the MMU.
When the device wants to access the same physical memory space it uses
the bus address which is obtained by using the DMA mapping API. The
Coolidge SoC includes several IOMMUs for clusters, PCIe peripherals,
SoC peripherals, and more; that will translate this "bus address" into
a physical one for DMA operations.

Bus addresses are IOVA (I/O Virtual Address) or DMA addresses. These
addresses can be obtained by calling the allocation functions of the DMA APIs.
It can also be obtained through classical kernel allocation of physical
contiguous memory and then calling mapping functions of the DMA API.

In order to be able to use the kvx IOMMU we have implemented the IOMMU DMA
interface in arch/kvx/mm/dma-mapping.c. DMA functions are registered by
implementing arch_setup_dma_ops() and generic IOMMU functions. Generic IOMMU
are calling our specific IOMMU functions that adds or remove mappings between
DMA addresses and physical addresses in the IOMMU TLB.

Specific IOMMU functions are defined in the kvx IOMMU driver. The kvx IOMMU
driver manage two physical hardware IOMMU: one used for TX and one for RX.
In the next section we described the HW IOMMUs.

Cluster IOMMUs
--------------

IOMMUs on cluster are used for DMA and cryptographic accelerators.
There are six IOMMUs connected to the:

 - cluster DMA tx
 - cluster DMA rx
 - first non secure cryptographic accelerator
 - second non secure cryptographic accelerator
 - first secure cryptographic accelerator
 - second secure cryptographic accelerator

SoC peripherals IOMMUs
----------------------

Since SoC peripherals are connected to an AXI bus, two IOMMUs are used: one for
each AXI channel (read and write). These two IOMMUs are shared between all master
devices and DMA. These two IOMMUs will have the same entries but need to be configured
independently.

PCIe IOMMUs
-----------

There is a slave IOMMU (read and write from the MPPA to the PCIe endpoint)
and a master IOMMU (read and write from a PCIe endpoint to system DDR).
The PCIe root complex and the MSI/MSI-X controller have been designed to use
the IOMMU feature when enabled. (For example for supporting endpoint that
support only 32 bits addresses and allow them to access any memory in a
64 bits address space). For security reason it is highly recommended to
activate the IOMMU for PCIe.

IOMMU implementation
--------------------

The kvx is providing several IOMMUs. Here is a simplified view of all IOMMUs
and translations that occurs between memory and devices::

  +---------------------------------------------------------------------+
  | +------------+     +---------+                          | CLUSTER X |
  | | Cores 0-15 +---->+ Crypto  |                          +-----------|
  | +-----+------+     +----+----+                                      |
  |       |                 |                                           |
  |       v                 v                                           |
  |   +-------+        +------------------------------+                 |
  |   |  MMU  |   +----+ IOMMU x4 (secure + insecure) |                 |
  |   +---+---+   |    +------------------------------+                 |
  |       |       |                                                     |
  +--------------------+                                                |
         |        |    |                                                |
         v        v    |                                                |
     +---+--------+-+  |                                                |
     |    MEMORY    |  |     +----------+     +--------+     +-------+  |
     |              +<-|-----+ IOMMU Rx |<----+ DMA Rx |<----+       |  |
     |              |  |     +----------+     +--------+     |       |  |
     |              |  |                                     |  NoC  |  |
     |              |  |     +----------+     +--------+     |       |  |
     |              +--|---->| IOMMU Tx +---->| DMA Tx +---->+       |  |
     |              |  |     +----------+     +--------+     +-------+  |
     |              |  +------------------------------------------------+
     |              |
     |              |     +--------------+     +------+
     |              |<--->+ IOMMU Rx/Tx  +<--->+ PCIe +
     |              |     +--------------+     +------+
     |              |
     |              |     +--------------+     +------------------------+
     |              |<--->+ IOMMU Rx/Tx  +<--->+ master Soc Peripherals |
     |              |     +--------------+     +------------------------+
     +--------------+


There is also an IOMMU dedicated to the crypto module but this module will not
be accessed by the operating system.

We will provide one driver to manage IOMMUs RX/TX. All of them will be
described in the device tree to be able to get their particularities. See
the example below that describes the relation between IOMMU, DMA and NoC in
the cluster.

IOMMU is related to a specific bus like PCIe we will be able to specify that
all peripherals will go through this IOMMU.

IOMMU Page table
----------------

We need to be able to know which IO virtual addresses (IOVA) are mapped in the
TLB in order to be able to remove entries when a device finishes a transfer and
release memory. This information could be extracted when needed by computing all
sets used by the memory and then reads all sixteen ways and compare them to the
IOVA but it won't be efficient. We also need to be able to translate an IOVA
to a physical address as required by the iova_to_phys IOMMU ops that is used
by DMA. Like previously it can be done by extracting the set from the address
and comparing the IOVA to each sixteen entries of the given set.

A solution is to keep a page table for the IOMMU. But this method is not
efficient for reloading an entry of the TLB without the help of an hardware
page table. So to prevent the need of a refill we will update the TLB when a
device request access to memory and if there is no more slot available in the
TLB we will just fail and the device will have to try again later. It is not
efficient but at least we won't need to manage the refill of the TLB.

This limits the total amount of memory that can be used for transfer between
device and memory (see Limitations section below).
To be able to manage bigger transfer we can implement the huge page table in
the Linux kernel and use a page table that match the size of huge page table
for a given IOMMU (typically the PCIe IOMMU).

As we won't refill the TLB we know that we won't have more than 128*16 entries.
In this case we can simply keep a table with all possible entries.

Maintenance interface
---------------------

It is possible to have several "maintainers" for the same IOMMU. The driver is
using two of them. One that writes the TLB and another interface reads TLB. For
debug purpose it is possible to display the content of the tlb by using the
following command in gdb::

  gdb> p kvx_iommu_dump_tlb( <iommu addr>, 0)

Since different management interface are used for read and write it is safe to
execute the above command at any moment.

Interrupts
----------

IOMMU can have 3 kind of interrupts that corresponds to 3 different types of
errors (no mapping. protection, parity). When the IOMMU is shared between
clusters (SoC periph and PCIe) then fifteen IRQs are generated according to the
configuration of an association table. The association table is indexed by the
ASN number (9 bits) and the entry of the table is a subscription mask with one
bit per destination. Currently this is not managed by the driver.

The driver is only managing interrupts for the cluster. The mode used is the
stall one. So when an interrupt occurs it is managed by the driver. All others
interrupts that occurs are stored and the IOMMU is stalled. When driver cleans
the first interrupt others will be managed one by one.

ASN (Address Space Number)
--------------------------

This is also know as ASID in some other architecture. Each device will have a
given ASN that will be given through the device tree. As address space is
managed at the IOMMU domain level we will use one group and one domain per ID.
ASN are coded on 9 bits.

Device tree
-----------

Relationships between devices, DMAs and IOMMUs are described in the
device tree (see `Documentation/devicetree/bindings/iommu/kalray,kvx-iommu.txt`
for more details).

Limitations
-----------

Only supporting 4KB page size will limit the size of mapped memory to 8MB
because the IOMMU TLB can have at most 128*16 entries.
