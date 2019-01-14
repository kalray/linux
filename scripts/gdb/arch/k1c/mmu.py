import gdb
import re
import ctypes
from arch.k1c import constants
from linux import cpus

ps_value = {
    constants.LX_TLB_PS_4K: '4K',
    constants.LX_TLB_PS_64K: '64K',
    constants.LX_TLB_PS_2M: '2M',
    constants.LX_TLB_PS_512M: '512M',
}

ps_shift = {
    constants.LX_TLB_PS_4K: 12,
    constants.LX_TLB_PS_64K: 16,
    constants.LX_TLB_PS_2M: 21,
    constants.LX_TLB_PS_512M: 29,
}

g_value = {
    constants.LX_TLB_G_USE_ASN: 'Use ASN',
    constants.LX_TLB_G_GLOBAL: 'Global',
}

es_value = {
    constants.LX_TLB_ES_INVALID: 'Invalid',
    constants.LX_TLB_ES_PRESENT: 'Present',
    constants.LX_TLB_ES_MODIFIED: 'Modified',
    constants.LX_TLB_ES_A_MODIFIED: 'A-Modified',
}

cp_value = {
    constants.LX_TLB_CP_D_U: 'Device/Uncached',
    constants.LX_TLB_CP_U_U: 'Uncached/Uncached',
    constants.LX_TLB_CP_W_C: 'WriteThrough/Cached',
    constants.LX_TLB_CP_U_C: 'Unchached/Cached',
}

pa_value = {
    constants.LX_TLB_PA_NA_NA: 'NA_NA',
    constants.LX_TLB_PA_NA_R: 'NA_R',
    constants.LX_TLB_PA_NA_RW: 'NA_RW',
    constants.LX_TLB_PA_NA_RX: 'NA_RX',
    constants.LX_TLB_PA_NA_RWX: 'NA_RWX',
    constants.LX_TLB_PA_R_R: 'R_R',
    constants.LX_TLB_PA_R_RW: 'R_RW',
    constants.LX_TLB_PA_R_RX: 'R_RX',
    constants.LX_TLB_PA_R_RWX: 'R_RWX',
    constants.LX_TLB_PA_RW_RW: 'RW_RW',
    constants.LX_TLB_PA_RW_RWX: 'RW_RWX',
    constants.LX_TLB_PA_RX_RX: 'RX_RX',
    constants.LX_TLB_PA_RX_RWX: 'RX_RWX',
    constants.LX_TLB_PA_RWX_RWX: 'RWX_RWX',
}

access_type = {
    constants.LX_K1C_TLB_ACCESS_READ: "READ",
    constants.LX_K1C_TLB_ACCESS_WRITE: "WRITE",
    constants.LX_K1C_TLB_ACCESS_PROBE: "PROBE",
}

def tlb_access_get_idx(idx):
    return idx & ((1 << constants.LX_CONFIG_K1C_DEBUG_TLB_ACCESS_BITS) - 1)

def computed_set(tel, teh):
    # For a page of size 2^n (with n >= 12), the index of the set is
    # determined by the value of the bit slice comprising bits n to n+5 of
    # the virtual address corresponding to the start of the page
    page_size = int(tel.cast(gdb.lookup_type("struct tlb_entry_low"))['ps'])
    teh_val = long(teh.cast(gdb.lookup_type("uint64_t")))
    return (teh_val >> ps_shift[page_size]) & ((1 << 5) -1)

def tlb_access_objdump(obj, name):
    gdb.write("  {}\t|".format(name))
    for f in obj.type.fields():
        try:
            val = int(obj[f.name])
            gdb.write('\t{}:0x{:x}'.format(f.name, val))
        except:
            gdb.write('\t{}:{}'.format(f.name, obj[f.name]))

    gdb.write("\n")

# This return the tuple (type, mmc, tel, teh)
def tlb_access_get_val(cpu, idx):
    var_ptr = gdb.parse_and_eval("&k1c_tlb_access_rb")
    return (
            cpus.per_cpu(var_ptr, cpu)[idx]['type'],
            cpus.per_cpu(var_ptr, cpu)[idx]['mmc'],
            cpus.per_cpu(var_ptr, cpu)[idx]['entry']['tel'],
            cpus.per_cpu(var_ptr, cpu)[idx]['entry']['teh']
            )

class TLBaccess(gdb.Command):
    """Dump the last commands exectued when accessing the TLB. The number of
    commands to dump is passed as the first argument and the CPU is given
    as the second argument. By default the number of entries dump is 10 and
    CPU is 0.
    """

    def __init__(self):
        super(TLBaccess, self).__init__("lx-k1c-tlb-access", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):

        dump = 10
        cpu = 0
        argv = gdb.string_to_argv(arg)

        try:
            constants.LX_CONFIG_K1C_DEBUG_TLB_ACCESS_BITS
        except AttributeError:
            gdb.write("Kernel was not compiled with K1C_DEBUG_TLB_ACCESS_BITS.\n")
            gdb.write("This is required to get information about access to TLB.\n")
            return

        return

        if (len(argv) >= 1):
            try:
                dump = int(argv[0])
            except:
                gdb.write("WARNING: dump given as argument is not an integer, 10 is used\n")

        if (len(argv) >= 2):
            try:
                cpu = int(argv[1])
            except:
                gdb.write("WARNING: cpu given as argument is not an integer, 0 is used\n")

        # access_idx is the next id that will be written. There is no value at
        # this index.
        try:
            var_ptr = gdb.parse_and_eval("&k1c_tlb_access_idx")
            access_idx = int(cpus.per_cpu(var_ptr, cpu))
        except:
            gdb.write("Failed to get current index for the buffer. Abort\n")
            return

        for i in range(0, dump):
            current_idx = access_idx - i

            # When idx 0 is reach we need to check if we wrapped or not. If we
            # wrapped then there is no more operations logged.
            if (current_idx == 0) and (current_idx == tlb_access_get_idx(current_idx)):
                gdb.write("no more operations occured in TLB for cpu {}\n".format(cpu))
                break

            real_idx = tlb_access_get_idx(current_idx - 1)

            e_type, e_mmc, e_tel, e_teh = tlb_access_get_val(cpu, real_idx)
            access_type_str = access_type.get(int(e_type), "UNKNOWN")

            gdb.write('CPU: {} \n'.format(cpu))
            if (access_type_str == "WRITE"):
                gdb.write("  TYPE\t|\tWRITE - computed set {}\n".format(computed_set(e_tel, e_teh)))
            else:
                gdb.write("  TYPE\t|\t{}\n".format(access_type_str))

            mmc = e_mmc.cast(gdb.lookup_type("struct mmc_t"))
            tlb_access_objdump(mmc, "MMC")

            tel = e_tel.cast(gdb.lookup_type("struct tlb_entry_low"))
            tlb_access_objdump(tel, "TEL")

            teh = e_teh.cast(gdb.lookup_type("struct tlb_entry_high"))
            tlb_access_objdump(teh, "TEH")

            gdb.write('\n')

TLBaccess()

class LxDecodetlb(gdb.Command):
    """Decode $tel and $teh values
    """

    def __init__(self):
        super(LxDecodetlb, self).__init__("lx-k1c-tlb-decode", gdb.COMMAND_DATA)

    def invoke(self, argument, from_tty):
        tel_val = gdb.parse_and_eval("$tel")
        gdb.write('tel:\n\tes:\t{es}\n\tcp:\t{cp}\n\tpa:\t{pa}\n\tps:\t{ps}\n\tfn:\t0x{fn:016x}\n'.format(
                              es=es_value[int(tel_val["es"])],
                              cp=cp_value[int(tel_val["cp"])],
                              pa=pa_value[int(tel_val["pa"])],
                              ps=ps_value[int(tel_val["ps"])],
                              fn=int(tel_val["fn"]))
                          )

        teh_val = gdb.parse_and_eval("$teh")
        gdb.write('teh:\n\tasn:\t{asn}\n\tg:\t{g}\n\tvs:\t{vs}\n\tpn:\t0x{pn:016x}\n'.format(
                              asn=teh_val["asn"],
                              g=g_value[int(teh_val["g"])],
                              vs=teh_val["vs"],
                              pn=int(teh_val["pn"]))
                          )

LxDecodetlb()
