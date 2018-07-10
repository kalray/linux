# SPDX-License-Identifier: GPL-2.0
always := gdb-scripts

SRCTREE := $(abspath $(srctree))

$(obj)/gdb-scripts:
ifdef building_out_of_srctree
	$(Q)ln -fsn $(SRCTREE)/$(obj)/*.py $(objtree)/$(obj)
endif
