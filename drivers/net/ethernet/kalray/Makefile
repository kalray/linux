#
# Makefile for the Kalray device drivers.
#
kvx_eth-y := 	kvx-net.o kvx-phy-hw.o \
		kvx-net-hw.o v1/kvx-net-hw.o v2/kvx-net-hw.o \
		kvx-mac-hw.o v1/kvx-mac-hw.o v2/kvx-mac-hw.o \
		kvx-net-parsing.o v1/kvx-net-parsing.o v2/kvx-net-parsing.o\
		kvx-ethtool.o v1/kvx-ethtool.o v2/kvx-ethtool.o \
		kvx-net-fs.o v1/kvx-net-fs.o v2/kvx-net-fs.o \
		v1/kvx-net-tx.o v2/kvx-net-tx.o \
		kvx-net-def-filters.o

kvx_eth-$(CONFIG_DCB) += kvx-dcb.o

obj-$(CONFIG_NET_KVX_SOC) += kvx_eth.o

kvx_qsfp-y := kvx-qsfp.o

obj-$(CONFIG_NET_KVX_QSFP) += kvx_qsfp.o
