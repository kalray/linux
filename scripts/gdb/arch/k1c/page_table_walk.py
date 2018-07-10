import gdb
import re

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

class LxPageTableWalk(gdb.Command):
    """Looks for entries in the page table. The base address of the PGD is
       the argument.
    """

    def __init__(self):
        super(LxPageTableWalk, self).__init__("lx-page-table-walk", gdb.COMMAND_USER)

    def invoke(self, argument, from_tty):
        argv = gdb.string_to_argv(argument)
        if len(argv) != 1:
            print "The address of PGD is not provided"
            return

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
                    print "Failed to find a working base for PGD"
                    return
                m = extract_hexa(gdb_output)

        pgd = m[0]
        print "> Looking for PGD base 0x{:016x}".format(pgd)

        for pgd_pair in do_lookup(pgd):
            print "[{}] -> Entry[0x{:016x}]".format(
                    (pgd_pair[0] - pgd)/PTRSIZE,
                    pgd_pair[1])
            print "\t> Looking for PMD base 0x{:016x}".format(pgd_pair[1])
            for pmd_pair in do_lookup(pgd_pair[1]):
                print "\t[{}] -> Entry[0x{:016x}]".format(
                        (pmd_pair[0] - pgd_pair[1])/8,
                        pmd_pair[1])
                print "\t\t> Looking for PTE base 0x{:016x}".format(pmd_pair[1])
                for pte_pair in do_lookup(pmd_pair[1]):
                    print "\t\t[{}] -> PTE [0x{:016x}]".format(
                            (pte_pair[0] - pmd_pair[1])/8, pte_pair[1])

LxPageTableWalk()
