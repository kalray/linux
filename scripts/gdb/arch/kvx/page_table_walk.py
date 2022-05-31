import gdb
import re
import ctypes
from arch.kvx import constants
#
# PTE format:
#  +---------+--------+----+--------+---+---+---+---+---+---+------+---+---+
#  | 63..23  | 22..13 | 12 | 11..10 | 9 | 8 | 7 | 6 | 5 | 4 | 3..2 | 1 | 0 |
#  +---------+--------+----+--------+---+---+---+---+---+---+------+---+---+
#      PFN     Unused   S    PageSZ   H   G   X   W   R   D    CP    A   P

class pte_bits(ctypes.LittleEndianStructure):
    _fields_ = [
            ("P", ctypes.c_uint8, 1),
            ("A", ctypes.c_uint8, 1),
            ("CP", ctypes.c_uint8, 2),
            ("D", ctypes.c_uint8, 1),
            ("R", ctypes.c_uint8, 1),
            ("W", ctypes.c_uint8, 1),
            ("X", ctypes.c_uint8, 1),
            ("G", ctypes.c_uint8, 1),
            ("H", ctypes.c_uint8, 1),
            ("PageSZ", ctypes.c_uint8, 2),
            ("S", ctypes.c_uint8, 1),
            ("Unused", ctypes.c_uint16, 10),
            ("PFN", ctypes.c_uint64, 41),
        ]

class Pte(ctypes.Union):
    _fields_ = [("bf", pte_bits),
                ("value", ctypes.c_uint64)]

    def __init__(self, pteValue):
        self.value = pteValue

    def phys_addr(self):
        return self.bf.PFN << constants.LX_PAGE_SHIFT

    def __str__(self):
        pte_str = "PFN: 0x{:016x}, ".format(self.phys_addr())
        pte_str += "PS: " + page_sz_str[self.bf.PageSZ] + ", bits: "
        ignore_fields = ["PFN", "Unused", "PageSZ"]

        for field in reversed(self.bf._fields_):
            if field[0] in ignore_fields:
                continue

            if long(getattr(self.bf, field[0])) != 0:
                pte_str += field[0] + " "

        return pte_str

SIGN_EXT_BITS = 64 - constants.LX_PAGE_SHIFT - constants.LX_PTE_BITS - constants.LX_PMD_BITS - constants.LX_PGDIR_BITS

class virt_addr_bits(ctypes.LittleEndianStructure):
    _fields_ = [
            ("page_off", ctypes.c_uint64, constants.LX_PAGE_SHIFT),
            ("pte_idx", ctypes.c_uint64, constants.LX_PTE_BITS),
            ("pmd_idx", ctypes.c_uint64, constants.LX_PMD_BITS),
            ("pgd_idx", ctypes.c_uint64, constants.LX_PGDIR_BITS),
            ("sign_extension", ctypes.c_uint64, SIGN_EXT_BITS),
        ]

class VirtAddr(ctypes.Union):
    _fields_ = [("bf", virt_addr_bits),
                ("value", ctypes.c_uint64)]

    def __init__(self, virtAddr):
        self.value = virtAddr

PGD_ENTRIES = 1 << constants.LX_PGDIR_BITS
PMD_ENTRIES = 1 << constants.LX_PMD_BITS
PTE_ENTRIES = 1 << constants.LX_PTE_BITS

page_sz_str = {
    constants.LX_TLB_PS_4K : "4 Ko",
    constants.LX_TLB_PS_64K: "64 Ko",
    constants.LX_TLB_PS_2M: "2 Mo",
    constants.LX_TLB_PS_512M: "512 Mo"
}

unsigned_long_ptr = gdb.lookup_type('unsigned long').pointer()

def unsigned_long(gdb_val):
    """Convert a unsigned long gdb.Value to a displayable python value
    """
    return long(gdb_val) & 0xFFFFFFFFFFFFFFFF

def do_lookup(base, size=512):
    """Lookup a page table and return an array of pair with index:entry_value
    """
    base_ptr = base.cast(unsigned_long_ptr)

    res = []

    for entry in range(size):
        entry_val = base_ptr.dereference()
        base_ptr += 1
        if entry_val != 0:
            res.append((entry, entry_val))

    return res

def phys_to_virt(virt):
    return virt + constants.LX_PA_TO_VA_OFFSET

class LxPageTableWalk(gdb.Command):
    """Looks for entries in the page table. The base address of the PGD is
       the argument.
    """

    def __init__(self):
        super(LxPageTableWalk, self).__init__("lx-kvx-page-table-walk", gdb.COMMAND_DATA)

    def invoke(self, argument, from_tty):
        argv = gdb.string_to_argv(argument)

        if (len(argv) == 1):
            pgd = argv[0]
        else:
            pgd = "$lx_current().active_mm.pgd"

        val = gdb.parse_and_eval(pgd)
        pgd = unsigned_long(val.cast(unsigned_long_ptr))

        print "> Looking for PGD base 0x{:016x}\n".format(pgd)

        for pgd_pair in do_lookup(val, PGD_ENTRIES):
            gdb.write("[{}] -> Entry[0x{:016x}]\n".format(
                    pgd_pair[0], unsigned_long(pgd_pair[1])))
            gdb.write("\t> Looking for PMD base 0x{:016x}\n".format(unsigned_long(pgd_pair[1])))
            for pmd_pair in do_lookup(phys_to_virt(pgd_pair[1]), PMD_ENTRIES):
                gdb.write("\t[{}] -> Entry[0x{:016x}]\n".format(
                        pmd_pair[0], unsigned_long(pmd_pair[1])))
                # Check if the PMD value read is a huge page or a pointer to a
                # PTE base.
                if (unsigned_long(pmd_pair[1]) & constants.LX__PAGE_HUGE):
                    gdb.write("\t\t> Huge PTE: ")
                    gdb.write(str(Pte(pmd_pair[1])) + "\n")
                else:
                    gdb.write("\t\t> Looking for PTE base 0x{:016x}\n".format(unsigned_long(pmd_pair[1])))
                    for pte_pair in do_lookup(phys_to_virt(pmd_pair[1]), PTE_ENTRIES):
                        gdb.write("\t\t[{}] -> PTE [0x{:016x}]\n".format(
                                pte_pair[0], unsigned_long(pte_pair[1])))
                        gdb.write("\t\t" + str(Pte(pte_pair[1]))+ "\n")

LxPageTableWalk()

ps_shift = {
    constants.LX_TLB_PS_4K: 12,
    constants.LX_TLB_PS_64K: 16,
    constants.LX_TLB_PS_2M: 21,
    constants.LX_TLB_PS_512M: 29,
}

def mask_addr(addr, shift):
    return addr & ((1 << shift) - 1)

def lookup_entry(base, idx):
    entry_addr = base.cast(unsigned_long_ptr) + idx
    res_addr = entry_addr.dereference()
    if res_addr == 0:
        raise

    return res_addr

class LxVirtToPhys(gdb.Command):
    """Translate a virtual address to a physical one by walking the page table
    """

    def __init__(self):
        super(LxVirtToPhys, self).__init__("lx-kvx-virt-to-phys", gdb.COMMAND_DATA)

    def invoke(self, argument, from_tty):
        argv = gdb.string_to_argv(argument)

        if (len(argv) == 0):
            gdb.write("Missing arguments for command: <virt_addr> [<pgd_addr>]\n")
            return

        addr = gdb.parse_and_eval(argv[0])
        if (len(argv) == 2):
            pgd = argv[1]
        else:
            pgd = "$lx_current().active_mm.pgd"

        pgd = gdb.parse_and_eval(pgd)

        addr = unsigned_long(addr.cast(unsigned_long_ptr))
        gdb.write("Trying to find phys_address for 0x{:016x} \n".format(addr))

        virt_split = VirtAddr(addr)

        try:
            pmd_addr = lookup_entry(pgd, virt_split.bf.pgd_idx)
            pte_addr = lookup_entry(phys_to_virt(pmd_addr), virt_split.bf.pmd_idx)
            pte_value = lookup_entry(phys_to_virt(pte_addr), virt_split.bf.pte_idx)
        except:
            raise gdb.GdbError("No page table entry for address 0x{:016x}".format(addr))

        pte_val = Pte(pte_value)
        gdb.write(str(pte_val)+ "\n")

        low_addr_bits = mask_addr(addr, ps_shift[pte_val.bf.PageSZ])
        gdb.write("Physical address: 0x{:016x}\n".format(pte_val.phys_addr() + low_addr_bits))

LxVirtToPhys()
