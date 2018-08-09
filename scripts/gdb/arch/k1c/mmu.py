import gdb
import re
import ctypes
from arch.k1c import constants

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

def get_reg_value(field):
        val = gdb.parse_and_eval(field)
        return long(val.cast(gdb.lookup_type('unsigned long')))

class LxDecodetlb(gdb.Command):
    """Decode $tel and $teh values
    """

    def __init__(self):
        super(LxDecodetlb, self).__init__("lx-k1c-tlb-decode", gdb.COMMAND_DATA)

    def invoke(self, argument, from_tty):
        page_size_value = get_reg_value("$teh.ps")
        page_size_shift = ps_shift[page_size_value]
        gdb.write('tel:\n\tes:\t{es}\n\tcp:\t{cp}\n\tpa:\t{pa}\n\tfn:\t0x{fn:016x}\n'.format(
                              es=es_value[get_reg_value("$tel.es")],
                              cp=cp_value[get_reg_value("$tel.cp")],
                              pa=pa_value[get_reg_value("$tel.pa")],
                              fn=get_reg_value("$tel.fn") << page_size_shift,
                          ))

        gdb.write('teh:\n\tasn:\t{asn}\n\tg:\t{toto}\n\tps:\t{ps}\n\tpn:\t0x{pn:016x}\n'.format(
                              asn=get_reg_value("$teh.asn"),
                              toto=g_value[get_reg_value("$teh.g")],
                              ps=ps_value[page_size_value],
                              pn=get_reg_value("$teh.pn") << page_size_shift,
                          ))

LxDecodetlb()
