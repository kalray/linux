import gdb
import re
import ctypes
from arch.k1c import constants
#
# PTE format:
# | 63   8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0
#   PFN      D   A   G   U   X   W   R   V
#

class pte_bits(ctypes.LittleEndianStructure):
    _fields_ = [
            ("P", ctypes.c_uint8, 1),
            ("R", ctypes.c_uint8, 1),
            ("W", ctypes.c_uint8, 1),
            ("X", ctypes.c_uint8, 1),
            ("U", ctypes.c_uint8, 1),
            ("G", ctypes.c_uint8, 1),
            ("A", ctypes.c_uint8, 1),
            ("D", ctypes.c_uint8, 1),
            ("res", ctypes.c_uint8, 4),
            ("pfn", ctypes.c_uint64, 52),
        ]

class Pte(ctypes.Union):
    _fields_ = [("bf", pte_bits),
                ("value", ctypes.c_uint64)]

PTRSIZE = 8

def extract_hexa(s):
    return map(lambda x:int(x, 16), re.findall(r'(0x[0-9a-f]+)',s))

def do_lookup(base):
    """Run x/512gx to look for the first 512 values. PGD, PMD and PTE are using
       a page to store values of 8 bytes. Currently our pages are 4096 bytes so
       we can store 512 values.
       It returns a list of pairs (adresses, value) found in the table.
    """
    gdb_output = gdb.execute("x/512gx 0x{:016x}".format(base), True, True)

    res = []

    if gdb_output:
        for line in gdb_output.split('\n'):
            # line is something like:
            #   0xffffff001f5ccff0: 0x0000000000000000 0xffffff001f5ce000
            m = extract_hexa(line.lower())
            if len(m) == 3:
                if m[1] != 0x0:
                    res.append((m[0], m[1]))
                if m[2] != 0x0:
                    res.append((m[0] + PTRSIZE, m[2]))

    return res

def get_pte_bits(pte_entry):
    """Decode a pte_entry using bits defined in pgtable-bits
       It returns a string describing the pte entry
    """
    pte_val = Pte()
    pte_val.value = pte_entry
    pte_str = "\t\tPFN: 0x{:016x}, bits: ".format(pte_val.bf.pfn << constants.LX_PAGE_SHIFT)

    for field in pte_val.bf._fields_:
        if field[0] == "res":
            break
        if long(getattr(pte_val.bf, field[0])) != 0:
            pte_str += field[0]

    pte_str += "\n"
    return pte_str


class LxPageTableWalk(gdb.Command):
    """Looks for entries in the page table. The base address of the PGD is
       the argument.
    """

    def __init__(self):
        super(LxPageTableWalk, self).__init__("lx-k1c-page-table-walk", gdb.COMMAND_DATA)

    def invoke(self, argument, from_tty):
        argv = gdb.string_to_argv(argument)
        if len(argv) != 1:
            raise gdb.GdbError("PGD address is not provided.")

        # We suppose that an hexa is given as parameter
        m = extract_hexa(argv[0].lower())
        if len(m) == 0:
                # Try to run it has a command
                try:
                    gdb_output = gdb.execute("{}".format(argv[0]), True, True)
                except:
                    # Let's try something else
                    m = []
                else:
                    m = extract_hexa(gdb_output)

        if len(m) == 0:
                # Try to print what the user gives us as parameter
                try:
                    gdb_output = gdb.execute("p/x {}".format(argv[0]), True, True)
                except:
                    raise gdb.GdbError("Failed to find a working base for PGD.")
                m = extract_hexa(gdb_output)

        pgd = m[0]
        print "> Looking for PGD base 0x{:016x}\n".format(pgd)

        for pgd_pair in do_lookup(pgd):
            gdb.write("[{}] -> Entry[0x{:016x}]\n".format(
                    (pgd_pair[0] - pgd)/PTRSIZE,
                    pgd_pair[1]))
            gdb.write("\t> Looking for PMD base 0x{:016x}\n".format(pgd_pair[1]))
            for pmd_pair in do_lookup(pgd_pair[1]):
                gdb.write("\t[{}] -> Entry[0x{:016x}]\n".format(
                        (pmd_pair[0] - pgd_pair[1])/8,
                        pmd_pair[1]))
                gdb.write("\t\t> Looking for PTE base 0x{:016x}\n".format(pmd_pair[1]))
                for pte_pair in do_lookup(pmd_pair[1]):
                    gdb.write("\t\t[{}] -> PTE [0x{:016x}]\n".format(
                            (pte_pair[0] - pmd_pair[1])/8, pte_pair[1]))
                    gdb.write(get_pte_bits(pte_pair[1]))

LxPageTableWalk()
