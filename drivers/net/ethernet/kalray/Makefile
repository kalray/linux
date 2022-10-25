#
# Makefile for the Kalray device drivers.
#

kvx_eth-y :=	kvx-net.o kvx-net-hw.o kvx-mac-hw.o kvx-phy-hw.o \
		kvx-net-parsing.o kvx-ethtool.o kvx-net-fs.o \
		kvx-net-def-filters.o

ifdef CONFIG_KVX_SUBARCH_KV3_1
	kvx_eth-y += kvx-net-tx.o
else
	kvx_eth-y += kvx-net-tx-cv2.o
endif

kvx_eth-$(CONFIG_DCB) += kvx-dcb.o

obj-$(CONFIG_NET_KVX_SOC) += kvx_eth.o

kvx_qsfp-y := kvx-qsfp.o

obj-$(CONFIG_NET_KVX_QSFP) += kvx_qsfp.o
