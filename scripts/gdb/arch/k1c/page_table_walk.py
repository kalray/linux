import gdb
import re
import ctypes
from arch.k1c import constants
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

    def __str__(self):
        pte_str = "PFN: 0x{:016x}, ".format(self.bf.PFN << constants.LX_PAGE_SHIFT)
        pte_str += "PS: " + page_sz_str[self.bf.PageSZ] + ", bits: "
        ignore_fields = ["PFN", "Unused", "PageSZ"]

        for field in reversed(self.bf._fields_):
            if field[0] in ignore_fields:
                continue

            if long(getattr(self.bf, field[0])) != 0:
                pte_str += field[0] + " "

        return pte_str


PGD_ENTRIES = 1 << constants.LX_PGDIR_BITS
PMD_ENTRIES = 1 << constants.LX_PMD_BITS
PTE_ENTRIES = 1 << constants.LX_PTE_BITS

page_sz_str = {
    constants.LX_TLB_PS_4K : "4 Ko",
    constants.LX_TLB_PS_64K: "64 Ko",
    constants.LX_TLB_PS_2M: "2 Mo",
    constants.LX_TLB_PS_512M: "512 Mo"
}

def unsigned_long(gdb_val):
    """Convert a unsigned long gdb.Value to a displayable python value
    """
    return long(gdb_val) & 0xFFFFFFFFFFFFFFFF

def do_lookup(base, size=512):
    """Lookup a page table and return an array of pair with index:entry_value
    """
    unsigned_long_ptr = gdb.lookup_type('unsigned long').pointer()
    base_ptr = base.cast(unsigned_long_ptr)

    res = []

    for entry in range(size):
        entry_val = base_ptr.dereference()
        base_ptr += 1
        if entry_val != 0:
            res.append((entry, entry_val))

    return res

class LxPageTableWalk(gdb.Command):
    """Looks for entries in the page table. The base address of the PGD is
       the argument.
    """

    def __init__(self):
        super(LxPageTableWalk, self).__init__("lx-k1c-page-table-walk", gdb.COMMAND_DATA)

    def invoke(self, argument, from_tty):
        argv = gdb.string_to_argv(argument)

        if (len(argv) == 1):
            pgd = argv[0]
        else:
            pgd = "$lx_current().active_mm.pgd"

        unsigned_long_ptr = gdb.lookup_type('unsigned long').pointer()
        val = gdb.parse_and_eval(pgd)
        pgd = unsigned_long(val.cast(unsigned_long_ptr))

        print "> Looking for PGD base 0x{:016x}\n".format(pgd)

        for pgd_pair in do_lookup(val, PGD_ENTRIES):
            gdb.write("[{}] -> Entry[0x{:016x}]\n".format(
                    pgd_pair[0], unsigned_long(pgd_pair[1])))
            gdb.write("\t> Looking for PMD base 0x{:016x}\n".format(unsigned_long(pgd_pair[1])))
            for pmd_pair in do_lookup(pgd_pair[1] + constants.LX_PA_TO_VA_OFFSET, PMD_ENTRIES):
                gdb.write("\t[{}] -> Entry[0x{:016x}]\n".format(
                        pmd_pair[0], unsigned_long(pmd_pair[1])))
                # Check if the PMD value read is a huge page or a pointer to a
                # PTE base.
                if (unsigned_long(pmd_pair[1]) & constants.LX__PAGE_HUGE):
                    gdb.write("\t\t> Huge PTE: ")
                    gdb.write(str(Pte(pmd_pair[1])) + "\n")
                else:
                    gdb.write("\t\t> Looking for PTE base 0x{:016x}\n".format(unsigned_long(pmd_pair[1])))
                    for pte_pair in do_lookup(pmd_pair[1] + constants.LX_PA_TO_VA_OFFSET, PTE_ENTRIES):
                        gdb.write("\t\t[{}] -> PTE [0x{:016x}]\n".format(
                                pte_pair[0], unsigned_long(pte_pair[1])))
                        gdb.write("\t\t" + str(Pte(pte_pair[1]))+ "\n")

LxPageTableWalk()
