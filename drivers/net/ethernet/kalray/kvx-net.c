// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/nvmem-consumer.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/iommu.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_dma.h>
#include <linux/of_platform.h>
#include <net/checksum.h>
#include <linux/dma/kvx-dma-api.h>
#include <linux/ti-retimer.h>
#include <linux/rtnetlink.h>
#include <linux/hash.h>
#include <linux/firmware.h>

#include "kvx-net.h"
#include "kvx-net-hw.h"
#include "kvx-net-regs.h"
#include "kvx-net-hdr.h"
#include "kvx-mac-regs.h"

#define KVX_PHY_FW_NAME "dwc_phy.bin"

#define KVX_RX_HEADROOM  (NET_IP_ALIGN + NET_SKB_PAD)
#define KVX_SKB_PAD      (SKB_DATA_ALIGN(sizeof(struct skb_shared_info) + \
					KVX_RX_HEADROOM))
#define KVX_SKB_SIZE(len)       (SKB_DATA_ALIGN(len) + KVX_SKB_PAD)
#define KVX_MAX_RX_BUF_SIZE     (PAGE_SIZE - KVX_SKB_PAD)

/* Min/max constraints on last segment for skbuff data */
#define KVX_MIN_LAST_SEG_SIZE   32
#define KVX_MAX_LAST_SEG_SIZE   256
/* Max segment size sent to DMA */
#define KVX_SEG_SIZE            1024
#define LINK_POLL_TIMER_IN_MS   1000

#define KVX_DEV(ndev) container_of(ndev->hw, struct kvx_eth_dev, hw)

#define KVX_TEST_BIT(name, bitmap)	test_bit(ETHTOOL_LINK_MODE_ ## name ## _BIT, \
				 bitmap)

static bool load_phy_fw = true;
module_param(load_phy_fw, bool, 0);
MODULE_PARM_DESC(load_phy_fw, "Update PHY firmware ("KVX_PHY_FW_NAME")");

/* Device tree related entries */
static const char *rtm_prop_name[RTM_NB] = {
	[RTM_RX] = "kalray,rtmrx",
	[RTM_TX] = "kalray,rtmtx",
};

static const char *rtm_channels_prop_name[RTM_NB] = {
	[RTM_RX] = "kalray,rtmrx-channels",
	[RTM_TX] = "kalray,rtmtx-channels",
};


static void kvx_eth_alloc_rx_buffers(struct kvx_eth_ring *ring, int count);

enum kvx_eth_speed_units_idx {
	KVX_ETH_UNITS_GBPS,
	KVX_ETH_UNITS_MBPS,
	KVX_ETH_UNITS_NB,
};

char *kvx_eth_speed_units[KVX_ETH_UNITS_NB] = {
	[KVX_ETH_UNITS_GBPS] = "Gbps",
	[KVX_ETH_UNITS_MBPS] = "Mbps",
};

/* kvx_eth_get_formated_speed() - Convert int speed to a displayable format
 * @speed: the speed to parse in mbps
 * @speed_fmt: formatted speed value
 * @unit: matching unit string
 */
void kvx_eth_get_formated_speed(int speed, int *speed_fmt,
		char **unit)
{
	if (speed > 1000) {
		*speed_fmt = speed / 1000;
		*unit = kvx_eth_speed_units[KVX_ETH_UNITS_GBPS];
	} else {
		*speed_fmt = speed;
		*unit = kvx_eth_speed_units[KVX_ETH_UNITS_MBPS];
	}
}

/* kvx_eth_desc_unused() - Gets the number of remaining unused buffers in ring
 * @r: Current ring
 *
 * Return: number of usable buffers
 */
int kvx_eth_desc_unused(struct kvx_eth_ring *r)
{
	if (r->next_to_clean > r->next_to_use)
		return 0;
	return (r->count - (r->next_to_use - r->next_to_clean + 1));
}

static struct netdev_queue *get_txq(const struct kvx_eth_ring *ring)
{
	return netdev_get_tx_queue(ring->netdev, ring->qidx);
}

static void kvx_eth_poll_link(struct timer_list *t)
{
	struct kvx_eth_netdev *ndev = container_of(t, struct kvx_eth_netdev,
						   link_poll);
	bool link_los, link;

	/* No link checks for BERT and SGMII modes (handled @phy/mac level) */
	if (kvx_eth_phy_is_bert_en(ndev->hw) || ndev->cfg.speed == SPEED_1000 ||
	    ndev->cfg.transceiver.id == 0)
		goto exit;
	link = kvx_eth_mac_getlink(ndev->hw, &ndev->cfg);
	if (link != netif_carrier_ok(ndev->netdev)) {
		/* Reschedule mac config (consider link down) */
		phylink_mac_change(ndev->phylink, link);
	} else {
		link_los = kvx_eth_pmac_linklos(ndev->hw, &ndev->cfg);
		if (link_los)
			phylink_mac_change(ndev->phylink, false);
	}

exit:
	mod_timer(t, jiffies + msecs_to_jiffies(LINK_POLL_TIMER_IN_MS));
}

/* kvx_eth_netdev_init() - Init netdev (called once)
 * @netdev: Current netdev
 */
static int kvx_eth_netdev_init(struct net_device *netdev)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	int ret = phylink_of_phy_connect(ndev->phylink, ndev->dev->of_node, 0);

	if (ret) {
		netdev_err(netdev, "Unable to get phy (%i)\n", ret);
		return ret;
	}

	return 0;
}

/* kvx_eth_netdev_uninit() - Stop all netdev queues
 * @netdev: Current netdev
 */
static void kvx_eth_netdev_uninit(struct net_device *netdev)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	phylink_disconnect_phy(ndev->phylink);
}

/* kvx_eth_up() - Interface up
 * @netdev: Current netdev
 */
static void kvx_eth_up(struct net_device *netdev)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_eth_ring *r;
	int i;

	phylink_start(ndev->phylink);
	for (i = 0; i < ndev->dma_cfg.rx_chan_id.nb; i++) {
		r = &ndev->rx_ring[i];
		kvx_eth_alloc_rx_buffers(r, kvx_eth_desc_unused(r));
		napi_enable(&r->napi);
	}

	netif_tx_start_all_queues(netdev);

	mod_timer(&ndev->link_poll, jiffies +
		  msecs_to_jiffies(LINK_POLL_TIMER_IN_MS));
}

/* kvx_eth_netdev_open() - Open ops
 * @netdev: Current netdev
 */
static int kvx_eth_netdev_open(struct net_device *netdev)
{
	kvx_eth_up(netdev);
	return 0;
}

/* kvx_eth_down() - Interface down
 * @netdev: Current netdev
 */
static void kvx_eth_down(struct net_device *netdev)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	int i;

	del_timer_sync(&ndev->link_poll);
	phylink_stop(ndev->phylink);

	netif_tx_stop_all_queues(netdev);
	for (i = 0; i < ndev->dma_cfg.rx_chan_id.nb; i++)
		napi_disable(&ndev->rx_ring[i].napi);
}

/* kvx_eth_netdev_stop() - Stop all netdev queues
 * @netdev: Current netdev
 */
static int kvx_eth_netdev_stop(struct net_device *netdev)
{
	kvx_eth_down(netdev);

	return 0;
}

/* kvx_eth_init_netdev() - Init netdev generic settings
 * @ndev: Current kvx_eth_netdev
 *
 * Return: 0 - OK
 */
static int kvx_eth_init_netdev(struct kvx_eth_netdev *ndev)
{
	ndev->hw->max_frame_size = ndev->netdev->mtu +
		(2 * KVX_ETH_HEADER_SIZE);
	/* Takes into account alignement offsets (footers) */
	ndev->rx_buffer_len = ALIGN(ndev->hw->max_frame_size,
				    KVX_ETH_PKT_ALIGN);

	ndev->cfg.speed = SPEED_UNKNOWN;
	ndev->cfg.duplex = DUPLEX_UNKNOWN;
	ndev->cfg.fec = 0;
	kvx_eth_mac_f_init(ndev->hw, &ndev->cfg);
	kvx_eth_dt_f_init(ndev->hw, &ndev->cfg);
	kvx_eth_lb_f_init(ndev->hw, &ndev->cfg);
	kvx_eth_pfc_f_init(ndev->hw, &ndev->cfg);
	kvx_eth_parser_f_init(ndev->hw, &ndev->cfg);
	kvx_net_init_dcb(ndev->netdev);

	return 0;
}

/* kvx_eth_unmap_skb() - Unmap skb
 * @dev: Current device (with dma settings)
 * @tx: Tx ring descriptor
 */
static void kvx_eth_unmap_skb(struct device *dev,
			      const struct kvx_eth_netdev_tx *tx)
{
	const skb_frag_t *fp, *end;
	const struct skb_shared_info *si;
	int count = 1;

	dma_unmap_single(dev, sg_dma_address(&tx->sg[0]),
			 skb_headlen(tx->skb), DMA_TO_DEVICE);

	si = skb_shinfo(tx->skb);
	if (si) {
		end = &si->frags[si->nr_frags];
		for (fp = si->frags; fp < end; fp++, count++) {
			dma_unmap_page(dev, sg_dma_address(&tx->sg[count]),
				       skb_frag_size(fp), DMA_TO_DEVICE);
		}
	}
}

/* kvx_eth_skb_split() - build dma segments within boundaries
 *
 * Return: number of segments actually built
 */
static int kvx_eth_skb_split(struct device *dev, struct scatterlist *sg,
			     dma_addr_t dma_addr, size_t len)
{
	u8 *buf = (u8 *)dma_addr;
	int i = 0, s, l = len;

	do {
		if (l > KVX_SEG_SIZE + KVX_MIN_LAST_SEG_SIZE)
			s = KVX_SEG_SIZE;
		else if (l > KVX_SEG_SIZE)
			s = l + KVX_MAX_LAST_SEG_SIZE - KVX_SEG_SIZE;
		else if (l > KVX_MAX_LAST_SEG_SIZE)
			s = l - KVX_MAX_LAST_SEG_SIZE + KVX_MIN_LAST_SEG_SIZE;
		else
			s = l;

		if (s < KVX_MIN_LAST_SEG_SIZE) {
			dev_err(dev, "Segment size %d < %d\n", s, KVX_MIN_LAST_SEG_SIZE);
			break;
		}
		sg_dma_address(&sg[i]) = (dma_addr_t)buf;
		sg_dma_len(&sg[i]) = s;
		l -= s;
		buf += s;
		i++;
	} while (l > 0 && i <= MAX_SKB_FRAGS);
	return i;
}

/* kvx_eth_map_skb() - Map skb (build sg with corresponding IOVA)
 * @dev: Current device (with dma settings)
 * @tx: Tx ring descriptor
 *
 * Return: 0 on success, -ENOMEM on error.
 */
static int kvx_eth_map_skb(struct device *dev, struct kvx_eth_netdev_tx *tx)
{
	const skb_frag_t *fp, *end;
	const struct skb_shared_info *si;
	dma_addr_t handler;
	int len, count;

	sg_init_table(tx->sg, MAX_SKB_FRAGS + 1);
	handler = dma_map_single(dev, tx->skb->data,
				 skb_headlen(tx->skb), DMA_TO_DEVICE);
	if (dma_mapping_error(dev, handler))
		goto out_err;

	count = kvx_eth_skb_split(dev, tx->sg, handler, skb_headlen(tx->skb));
	tx->len = skb_headlen(tx->skb);

	si = skb_shinfo(tx->skb);
	end = &si->frags[si->nr_frags];
	for (fp = si->frags; fp < end; fp++) {
		len = skb_frag_size(fp);
		handler = skb_frag_dma_map(dev, fp, 0, len, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, handler))
			goto unwind;

		count += kvx_eth_skb_split(dev, &tx->sg[count], handler, len);
		if (count >= MAX_SKB_FRAGS + 1) {
			dev_warn(dev, "Too many skb segments\n");
			goto unwind;
		}
		tx->len += len;
	}
	sg_mark_end(&tx->sg[count - 1]);
	tx->sg_len = count;
	dev_dbg(dev, "%s tx->len=%d= %d - %d si->nr_frags: %d\n", __func__,
		(int)tx->len, (int)tx->len, tx->skb->len, si->nr_frags);
	return 0;

unwind:
	while (fp-- > si->frags)
		dma_unmap_page(dev, sg_dma_address(&tx->sg[--count]),
			       skb_frag_size(fp), DMA_TO_DEVICE);
	dma_unmap_single(dev, sg_dma_address(&tx->sg[0]),
			 skb_headlen(tx->skb), DMA_TO_DEVICE);

out_err:
	return -ENOMEM;
}

/* kvx_eth_clean_tx_irq() - Clears completed tx skb
 * @txr: Current TX ring
 * @desc_len: actual buffer len (from DMA engine)
 *
 * Return: 0 on success
 */
static int kvx_eth_clean_tx_irq(struct kvx_eth_ring *txr, size_t desc_len)
{
	struct net_device *netdev = txr->netdev;
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	u32 tx_r = txr->next_to_clean;
	struct kvx_eth_netdev_tx *tx = &txr->tx_buf[tx_r];
	int ret = 0;

	tx = &txr->tx_buf[tx_r];
	if (unlikely(!tx->skb)) {
		ret = -EINVAL;
		netdev_err(netdev, "No skb in descriptor\n");
		goto exit;
	}
	netdev_dbg(netdev, "Sent skb[%d]: 0x%llx len: %d/%d qidx: %d\n",
		   tx_r, (u64)tx->skb, (int)tx->len, tx->skb->len, txr->qidx);

	/* consume_skb */
	kvx_eth_unmap_skb(ndev->dev, tx);
	ndev->stats.ring.tx_bytes += tx->len;
	ndev->stats.ring.tx_pkts++;
	dev_consume_skb_irq(tx->skb);
	tx->skb = NULL;

exit:
	netdev_tx_completed_queue(get_txq(txr), 1, tx->len);
	++tx_r;
	if (tx_r == txr->count)
		tx_r = 0;
	txr->next_to_clean = tx_r;
	tx = &txr->tx_buf[tx_r];

	txr->next_to_clean = tx_r;

	if (netif_carrier_ok(netdev) &&
	    __netif_subqueue_stopped(netdev, txr->qidx))
		if (netif_carrier_ok(netdev) &&
		    (kvx_eth_desc_unused(txr) > (MAX_SKB_FRAGS + 1)))
			netif_wake_subqueue(netdev, txr->qidx);

	return ret;
}

/* kvx_eth_netdev_dma_callback_tx() - tx completion callback
 * @param: Callback dma_noc parameters
 */
static void kvx_eth_netdev_dma_callback_tx(void *param)
{
	struct kvx_callback_param *p = param;
	struct kvx_eth_ring *txr = p->cb_param;

	kvx_eth_clean_tx_irq(txr, p->len);
}

static u32 align_checksum(u32 cks)
{
	u32 c = cks;

	while (c > 0xffff)
		c = (c >> 16) + (c & 0xffff);
	return c;
}

/* kvx_eth_pseudo_hdr_cks() - Compute pseudo CRC on skb
 * @skb: skb to handle
 *
 * Return: computed crc
 */
static u16 kvx_eth_pseudo_hdr_cks(struct sk_buff *skb)
{
	struct ethhdr *eth_h = eth_hdr(skb);
	struct iphdr *iph = ip_hdr(skb);
	u16 payload_len = skb_tail_pointer(skb) - (unsigned char *)eth_h;
	u32 cks = eth_h->h_proto + payload_len;

	if (eth_h->h_proto == ETH_P_IP)
		cks = csum_partial((void *)&iph->saddr, 8, cks);
	else if (eth_h->h_proto == ETH_P_IPV6)
		cks = csum_partial((void *)&iph->saddr, 32, cks);

	return align_checksum(cks);
}

/* kvx_eth_tx_add_hdr() - Adds tx header (fill correpsonding metadata)
 * @ndev: Current kvx_eth_netdev
 * @skb: skb to handle
 *
 * Return: skb on success, NULL on error.
 */
static struct sk_buff *kvx_eth_tx_add_hdr(struct kvx_eth_netdev *ndev,
					  struct sk_buff *skb)
{
	union tx_metadata *hdr, h;
	size_t hdr_len = sizeof(h);
	struct ethhdr *eth_h = eth_hdr(skb);
	struct iphdr *iph = ip_hdr(skb);
	enum tx_ip_mode ip_mode = NO_IP_MODE;
	enum tx_crc_mode crc_mode = NO_CRC_MODE;
	struct kvx_eth_lane_cfg *cfg = &ndev->cfg;
	int pkt_size;

	memset(&h, 0, hdr_len);
	if (skb_headroom(skb) < hdr_len) {
		struct sk_buff *skb_new = skb_realloc_headroom(skb, hdr_len);

		dev_kfree_skb_any(skb);
		if (!skb_new)
			return NULL;
		skb = skb_new;
		eth_h = eth_hdr(skb);
		iph = ip_hdr(skb);
	}

	/* Packet size without tx metadata */
	pkt_size = skb_pagelen(skb);
	hdr = (union tx_metadata *)skb_push(skb, hdr_len);
	h._.pkt_size = pkt_size;
	h._.lane = cfg->id;
	h._.nocx_en = ndev->hw->tx_f[ndev->cfg.tx_fifo_id].nocx_en;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		goto exit;

	if (eth_h->h_proto == ETH_P_IP)
		ip_mode = IP_V4_MODE;
	else if (eth_h->h_proto == ETH_P_IPV6)
		ip_mode = IP_V6_MODE;

	if (iph && ndev->hw->tx_f[ndev->cfg.tx_fifo_id].crc_en) {
		if (iph->protocol == IPPROTO_TCP)
			crc_mode = TCP_MODE;
		else if ((iph->protocol == IPPROTO_UDP) ||
			 (iph->protocol == IPPROTO_UDPLITE))
			crc_mode = UDP_MODE;
	}
	if (ip_mode && crc_mode) {
		h._.ip_mode  = ip_mode;
		h._.crc_mode = crc_mode;
		h._.index    = (u16)skb->transport_header;
		h._.udp_tcp_cksum = kvx_eth_pseudo_hdr_cks(skb);
	} else {
		skb_checksum_help(skb);
	}

exit:
	put_unaligned(h.dword[0], &hdr->dword[0]);
	put_unaligned(h.dword[1], &hdr->dword[1]);

	return skb;
}

/* kvx_eth_netdev_start_xmit() - xmit ops
 * @skb: skb to handle
 * @netdev: Current netdev
 *
 * Return: transmit status
 */
static netdev_tx_t kvx_eth_netdev_start_xmit(struct sk_buff *skb,
					     struct net_device *netdev)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct device *dev = ndev->dev;
	int qidx = skb_get_queue_mapping(skb);
	struct kvx_eth_ring *txr = &ndev->tx_ring[qidx];
	struct dma_async_tx_descriptor *txd;
	u32 tx_w = txr->next_to_use;
	struct kvx_eth_netdev_tx *tx = &txr->tx_buf[tx_w];
	int unused_tx;

	if (skb_padto(skb, ETH_ZLEN))
		return NETDEV_TX_OK;

	if (skb->len <= 0) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (likely(ndev->hw->tx_f[ndev->cfg.tx_fifo_id].header_en)) {
		skb = kvx_eth_tx_add_hdr(ndev, skb);
		if (!skb)
			return NETDEV_TX_OK;
	}

	tx->skb = skb;
	tx->len = 0;
	netdev_dbg(netdev, "%s Sending skb[%d]: 0x%llx len: %d data_len: %d\n",
		   __func__, tx_w, (u64)skb, skb->len, skb->data_len);

	/* prepare sg */
	if (kvx_eth_map_skb(dev, tx)) {
		net_err_ratelimited("tx[%d]: Map skb failed\n", tx_w);
		goto busy;
	}
	txd = dmaengine_prep_slave_sg(txr->chan, tx->sg, tx->sg_len,
				      DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT);
	if (!txd) {
		netdev_err(netdev, "Failed to get dma desc tx[%d]:\n", tx_w);
		kvx_eth_unmap_skb(dev, tx);
		tx->skb = NULL;
		goto busy;
	}

	txd->callback = kvx_eth_netdev_dma_callback_tx;
	tx->cb_p.cb_param = txr;
	txd->callback_param = &tx->cb_p;

	/* submit and issue descriptor */
	tx->cookie = dmaengine_submit(txd);
	netdev_dbg(netdev, "Sending skb[%d]: 0x%llx len: %d/%d qidx: %d\n",
		    tx_w, (u64)tx->skb, (int)tx->len, tx->skb->len, txr->qidx);
	netdev_tx_sent_queue(get_txq(txr), tx->len);

	skb_tx_timestamp(skb);
	dma_async_issue_pending(txr->chan);

	tx_w++;
	txr->next_to_use = (tx_w < txr->count) ? tx_w : 0;

	unused_tx = kvx_eth_desc_unused(txr);
	if (unlikely(unused_tx == 0))
		netif_tx_stop_queue(get_txq(txr));

	return NETDEV_TX_OK;

busy:
	return NETDEV_TX_BUSY;
}

/* kvx_eth_alloc_rx_buffers() - Allocation rx descriptors
 * @rxr: RX ring
 * @count: number of buffers to allocate
 */
static void kvx_eth_alloc_rx_buffers(struct kvx_eth_ring *rxr, int count)
{
	struct net_device *netdev = rxr->netdev;
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_dma_config *dma_cfg = &ndev->dma_cfg;
	u32 unused_desc = kvx_eth_desc_unused(rxr);
	u32 rx_w = rxr->next_to_use;
	struct kvx_qdesc *qdesc;
	struct page *p;
	int ret = 0;

	while (--unused_desc > rxr->refill_thres && count--) {
		qdesc = &rxr->pool.qdesc[rx_w];

		if (!qdesc->dma_addr) {
			p = page_pool_alloc_pages(rxr->pool.pagepool,
						  GFP_ATOMIC | __GFP_NOWARN);
			if (!p) {
				pr_err("page alloc failed\n");
				break;
			}
			qdesc->va = p;
			qdesc->dma_addr = page_pool_get_dma_addr(p) +
				KVX_RX_HEADROOM;
		}
		ret = kvx_dma_enqueue_rx_buffer(rxr->rx_dma_chan,
					  qdesc->dma_addr, KVX_MAX_RX_BUF_SIZE);
		if (ret) {
			netdev_err(netdev, "Failed to enqueue buffer in rx chan[%d]: %d\n",
				   dma_cfg->rx_chan_id.start + rxr->qidx, ret);
			break;
		}

		++rx_w;
		if (rx_w == rxr->count)
			rx_w = 0;
	}
	rxr->next_to_use = rx_w;
}

static int kvx_eth_rx_hdr(struct kvx_eth_netdev *ndev, struct sk_buff *skb)
{
	struct rx_metadata *hdr = NULL;
	size_t hdr_size = sizeof(*hdr);

	if (kvx_eth_lb_has_header(ndev->hw, &ndev->cfg)) {
		netdev_dbg(ndev->netdev, "%s header rx (skb->len: %d data_len: %d)\n",
			   __func__, skb->len, skb->data_len);
		hdr = (struct rx_metadata *)skb->data;
		kvx_eth_dump_rx_hdr(ndev->hw, hdr);
		skb_pull(skb, hdr_size);
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}
	if (kvx_eth_lb_has_footer(ndev->hw, &ndev->cfg)) {
		netdev_dbg(ndev->netdev, "%s footer rx (skb->len: %d data_len: %d)\n",
			   __func__, skb->len, skb->data_len);
		hdr = (struct rx_metadata *)(skb_tail_pointer(skb) -
					     hdr_size);
		kvx_eth_dump_rx_hdr(ndev->hw, hdr);
		skb_trim(skb, skb->len - hdr_size);
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}
	return 0;
}

static int kvx_eth_rx_frame(struct kvx_eth_ring *rxr, u32 qdesc_idx,
			    dma_addr_t buf, size_t len, u64 eop)
{
	struct net_device *netdev = rxr->netdev;
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_qdesc *qdesc = &rxr->pool.qdesc[qdesc_idx];
	enum dma_data_direction dma_dir;
	void *data, *data_end, *va = NULL;
	struct page *page = NULL;
	size_t data_len = len; /* Assuming no FCS fwd from MAC*/
	int ret = 0;

	page = qdesc->va;
	if (KVX_SKB_SIZE(len) > PAGE_SIZE) {
		netdev_err(netdev, "Rx buffer exceeds PAGE_SIZE\n");
		return -ENOBUFS;
	}
	dma_dir = page_pool_get_dma_dir(rxr->pool.pagepool);
	dma_sync_single_for_cpu(ndev->dev, buf, len, dma_dir);

	if (likely(!rxr->skb)) {
		va = page_address(page);
		/* Prefetch header */
		prefetch(va);
		data = va + KVX_RX_HEADROOM;
		data_end = data + data_len;
		rxr->skb = build_skb(va, KVX_SKB_SIZE(data_len));
		if (unlikely(!rxr->skb)) {
			ndev->stats.ring.skb_alloc_err++;
			ret = -ENOMEM;
			goto recycle_page;
		}
		skb_reserve(rxr->skb, data - va);
		skb_put(rxr->skb, data_end - data);
	} else {
		skb_add_rx_frag(rxr->skb, skb_shinfo(rxr->skb)->nr_frags, page,
			KVX_RX_HEADROOM, data_len, data_len);
	}

	if (eop) {
		kvx_eth_rx_hdr(ndev, rxr->skb);
		rxr->skb->dev = rxr->napi.dev;
		skb_record_rx_queue(rxr->skb, ndev->dma_cfg.rx_chan_id.start +
				    rxr->qidx);
		rxr->skb->protocol = eth_type_trans(rxr->skb, netdev);
		ndev->stats.ring.rx_pkts++;
		netdev_dbg(ndev->netdev, "%s skb->len: %d data_len: %d\n",
			   __func__, rxr->skb->len, rxr->skb->data_len);
	}
	ndev->stats.ring.rx_bytes += data_len;

	/* Release descriptor */
	page_pool_release_page(rxr->pool.pagepool, page);
	qdesc->va = NULL;
	qdesc->dma_addr = 0;

	return 0;

recycle_page:
	page_pool_recycle_direct(rxr->pool.pagepool, page);
	return ret;
}

/* kvx_eth_clean_rx_irq() - Clears received RX buffers
 *
 * Called from napi poll:
 *  - handles RX metadata
 *  - RX buffer re-allocation if needed
 * @napi: Pointer to napi struct in rx ring
 * @work_done: returned nb of buffers completed
 * @work_left: napi budget
 *
 * Return: 0 on success
 */
static int kvx_eth_clean_rx_irq(struct napi_struct *napi, int work_left)
{
	struct kvx_eth_ring *rxr = container_of(napi, struct kvx_eth_ring,
						napi);
	struct net_device *netdev = rxr->netdev;
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_dma_config *dma_cfg = &ndev->dma_cfg;
	struct kvx_dma_pkt_full_desc pkt;
	u32 rx_r = rxr->next_to_clean;
	int work_done = 0;
	int rx_count = 0;
	int ret = 0;

	while (!kvx_dma_get_rx_completed(dma_cfg->pdev, rxr->rx_dma_chan, &pkt)) {
		work_done++;
		rx_count++;

		ret = kvx_eth_rx_frame(rxr, rx_r, (dma_addr_t)pkt.base,
				       (size_t)pkt.byte, pkt.notif);
		/* Still some RX segments pending */
		if (likely(!ret && pkt.notif)) {
			napi_gro_receive(napi, rxr->skb);
			rxr->skb = NULL;
		}

		if (unlikely(rx_count > rxr->refill_thres)) {
			kvx_eth_alloc_rx_buffers(rxr, rx_count);
			rx_count = 0;
		}
		++rx_r;
		rx_r = (rx_r < rxr->count) ? rx_r : 0;

		if (work_done >= work_left)
			break;
	}
	rxr->next_to_clean = rx_r;
	rx_count = kvx_eth_desc_unused(rxr);
	if (rx_count > rxr->refill_thres)
		kvx_eth_alloc_rx_buffers(rxr, rx_count);

	return work_done;
}

/* kvx_eth_netdev_poll() - NAPI polling callback
 * @napi: NAPI pointer
 * @budget: internal budget def
 *
 * Return: Number of buffers completed
 */
static int kvx_eth_netdev_poll(struct napi_struct *napi, int budget)
{
	int work_done = kvx_eth_clean_rx_irq(napi, budget);

	if (work_done < budget)
		napi_complete_done(napi, work_done);

	return work_done;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void kvx_eth_netdev_poll_controller(struct net_device *netdev)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	napi_schedule(&ndev->rx_ring[0]->napi);
}
#endif

/* kvx_eth_set_mac_addr() - Sets HW address
 * @netdev: Current netdev
 * @p: HW addr
 *
 * Return: 0 on success, -EADDRNOTAVAIL if mac addr NOK
 */
static int kvx_eth_set_mac_addr(struct net_device *netdev, void *p)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	memcpy(ndev->cfg.mac_f.addr, addr->sa_data, netdev->addr_len);

	kvx_mac_set_addr(ndev->hw, &ndev->cfg);

	return 0;
}

/* kvx_eth_change_mtu() - Change the Maximum Transfer Unit
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Return: 0 on success
 */
static int kvx_eth_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	int max_frame_len = new_mtu + (2 * KVX_ETH_HEADER_SIZE);

	ndev->rx_buffer_len = ALIGN(max_frame_len, KVX_ETH_PKT_ALIGN);
	ndev->hw->max_frame_size = max_frame_len;
	netdev->mtu = new_mtu;

	if (netif_running(netdev))
		kvx_eth_down(netdev);
	kvx_eth_hw_change_mtu(ndev->hw, ndev->cfg.id, max_frame_len);
	if (netif_running(netdev))
		kvx_eth_up(netdev);

	return 0;
}

static void kvx_eth_change_rx_flags(struct net_device *netdev, int flags)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	ndev->cfg.mac_f.promisc_mode = (flags & IFF_PROMISC) ? true : false;

	kvx_eth_mac_init(ndev->hw, &ndev->cfg);
}

/* kvx_eth_netdev_get_stats64() - Update stats
 * @netdev: Current netdev
 * @stats: Statistic struct
 */
static void kvx_eth_netdev_get_stats64(struct net_device *netdev,
				       struct rtnl_link_stats64 *stats)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	kvx_eth_update_stats64(ndev->hw, ndev->cfg.id, &ndev->stats);

	stats->rx_packets = ndev->stats.ring.rx_pkts;
	stats->tx_packets = ndev->stats.ring.tx_pkts;
	stats->rx_bytes = ndev->stats.ring.rx_bytes;
	stats->tx_bytes = ndev->stats.ring.tx_bytes;
	stats->rx_errors = ndev->stats.rx.ifinerrors;
	stats->tx_errors = ndev->stats.tx.ifouterrors;
	stats->rx_dropped = ndev->stats.rx.etherstatsdropevents;
	stats->multicast = ndev->stats.rx.ifinmulticastpkts;

	stats->rx_length_errors = ndev->stats.rx.inrangelengtherrors;
	stats->rx_crc_errors = ndev->stats.rx.framechecksequenceerrors;
	stats->rx_frame_errors = ndev->stats.rx.alignmenterrors;
}

/* Allow userspace to determine which ethernet controller
 * is behind this netdev, independently of the netdev name
 */
static int
kvx_eth_get_phys_port_name(struct net_device *dev, char *name, size_t len)
{
	struct kvx_eth_netdev *ndev = netdev_priv(dev);
	int n;

	n = snprintf(name, len, "enmppa%d",
		     ndev->hw->eth_id * KVX_ETH_LANE_NB + ndev->cfg.id);

	if (n >= len)
		return -EINVAL;

	return 0;
}

static int
kvx_eth_get_phys_port_id(struct net_device *dev, struct netdev_phys_item_id *id)
{
	struct kvx_eth_netdev *ndev = netdev_priv(dev);

	id->id_len = 1;
	id->id[0] = ndev->hw->eth_id * KVX_ETH_LANE_NB + ndev->cfg.id;

	return 0;
}

static const struct net_device_ops kvx_eth_netdev_ops = {
	.ndo_init               = kvx_eth_netdev_init,
	.ndo_uninit             = kvx_eth_netdev_uninit,
	.ndo_open               = kvx_eth_netdev_open,
	.ndo_stop               = kvx_eth_netdev_stop,
	.ndo_start_xmit         = kvx_eth_netdev_start_xmit,
	.ndo_get_stats64        = kvx_eth_netdev_get_stats64,
	.ndo_validate_addr      = eth_validate_addr,
	.ndo_set_mac_address    = kvx_eth_set_mac_addr,
	.ndo_change_mtu         = kvx_eth_change_mtu,
	.ndo_change_rx_flags    = kvx_eth_change_rx_flags,
	.ndo_get_phys_port_name = kvx_eth_get_phys_port_name,
	.ndo_get_phys_port_id   = kvx_eth_get_phys_port_id,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller    = kvx_eth_netdev_poll_controller,
#endif
};

static void kvx_eth_dma_irq_rx(void *data)
{
	struct kvx_eth_ring *ring = data;

	netdev_dbg(ring->netdev, "%s\n", __func__);
	napi_schedule(&ring->napi);
}

static struct page_pool *kvx_eth_create_rx_pool(struct kvx_eth_netdev *ndev,
						size_t size)
{
	struct kvx_dma_config *dma_cfg = &ndev->dma_cfg;
	struct page_pool *pool = NULL;
	struct page_pool_params pp_params = {
		.order = 0,
		.flags = PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV,
		.pool_size = dma_cfg->rx_chan_id.nb * size,
		.nid = NUMA_NO_NODE,
		.dma_dir = DMA_BIDIRECTIONAL,
		.offset = KVX_RX_HEADROOM,
		.max_len = KVX_MAX_RX_BUF_SIZE,
		/* Device must be the same for dma_sync_single_for_cpu */
		.dev = ndev->dev,
	};

	pool = page_pool_create(&pp_params);
	if (IS_ERR(pool))
		dev_err(ndev->dev, "cannot create rx page pool\n");

	return pool;
}

static int kvx_eth_alloc_rx_pool(struct kvx_eth_netdev *ndev,
			       struct kvx_eth_ring *r, int cache_id)
{
	struct kvx_buf_pool *rx_pool = &r->pool;

	rx_pool->qdesc = kcalloc(r->count, sizeof(*rx_pool->qdesc), GFP_KERNEL);
	if (!rx_pool->qdesc)
		return -ENOMEM;
	rx_pool->pagepool = kvx_eth_create_rx_pool(ndev, r->count);
	if (IS_ERR(rx_pool->pagepool)) {
		kfree(rx_pool->qdesc);
		netdev_err(ndev->netdev, "Unable to allocate page pool\n");
		return -ENOMEM;
	}

	return 0;
}

static void kvx_eth_release_rx_pool(struct kvx_eth_ring *r)
{
	u32 unused_desc = kvx_eth_desc_unused(r);
	u32 rx_r = r->next_to_clean;
	struct kvx_qdesc *qdesc;

	kvx_dma_flush_rx_queue(r->rx_dma_chan);
	while (--unused_desc) {
		qdesc = &r->pool.qdesc[rx_r];

		if (rx_r == r->next_to_use)
			break;
		if (qdesc)
			page_pool_release_page(r->pool.pagepool,
					       (struct page *)(qdesc->va));
		++rx_r;
		rx_r = (rx_r < r->count) ? rx_r : 0;
	}
	page_pool_destroy(r->pool.pagepool);
	kfree(r->pool.qdesc);
}

#define REFILL_THRES(c) ((3 * (c)) / 4)

int kvx_eth_alloc_rx_ring(struct kvx_eth_netdev *ndev, struct kvx_eth_ring *r)
{
	struct kvx_dma_config *dma_cfg = &ndev->dma_cfg;
	struct kvx_eth_dt_f dt;
	int ret = 0;

	r->count = kvx_dma_get_max_nb_desc(dma_cfg->pdev);
	r->refill_thres = REFILL_THRES(r->count);
	r->next_to_use = 0;
	r->next_to_clean = 0;
	ret = kvx_eth_alloc_rx_pool(ndev, r, dma_cfg->rx_cache_id);
	if (ret) {
		netdev_err(ndev->netdev, "Failed to get RX pool\n");
		goto exit;
	}

	netif_napi_add(ndev->netdev, &r->napi,
		       kvx_eth_netdev_poll, NAPI_POLL_WEIGHT);
	r->netdev = ndev->netdev;

	/* Reserve channel only once */
	if (r->config.trans_type != KVX_DMA_TYPE_MEM2ETH) {
		/* Only RX_CACHE_NB can be used and 1 rx_cache per queue */
		if (dma_cfg->rx_cache_id + r->qidx >= RX_CACHE_NB) {
			netdev_err(ndev->netdev, "Unable to get cache id\n");
			goto chan_failed;
		}
		memset(&r->config, 0, sizeof(r->config));
		r->rx_dma_chan = kvx_dma_get_rx_phy(dma_cfg->pdev,
					dma_cfg->rx_chan_id.start + r->qidx);
		ret = kvx_dma_reserve_rx_chan(dma_cfg->pdev, r->rx_dma_chan,
					(dma_cfg->rx_cache_id + r->qidx) %
					RX_CACHE_NB, kvx_eth_dma_irq_rx, r);
		if (ret)
			goto chan_failed;
		dt.cluster_id = kvx_cluster_id();
		dt.rx_channel = dma_cfg->rx_chan_id.start + r->qidx;
		dt.split_trigger = 0;
		dt.vchan = ndev->hw->vchan;
		kvx_eth_add_dispatch_table_entry(ndev->hw, &ndev->cfg, &dt,
			 ndev->cfg.default_dispatch_entry + dt.rx_channel);
		r->config.trans_type = KVX_DMA_TYPE_MEM2ETH;
	}
	return 0;

chan_failed:
	netif_napi_del(&r->napi);
	kvx_eth_release_rx_pool(r);
exit:
	return ret;
}

/* kvx_eth_release_rx_ring() - Release RX ring
 *
 * Flush dma rx job queue and release all pending buffers previously allocated
 * @r: Rx ring to be release
 * @keep_dma_chan: do not release dma channel
 */
void kvx_eth_release_rx_ring(struct kvx_eth_ring *r, int keep_dma_chan)
{
	struct kvx_eth_netdev *ndev = netdev_priv(r->netdev);
	struct kvx_dma_config *dma_cfg = &ndev->dma_cfg;

	netif_napi_del(&r->napi);
	kvx_eth_release_rx_pool(r);
	if (!keep_dma_chan)
		kvx_dma_release_rx_chan(dma_cfg->pdev, r->rx_dma_chan);
}

/* kvx_eth_alloc_rx_res() - Allocate RX resources
 * @netdev: Current netdev
 *
 * Return: 0 on success, < 0 on failure
 */
static int kvx_eth_alloc_rx_res(struct net_device *netdev)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	int i, qidx, ret = 0;

	for (qidx = 0; qidx < ndev->dma_cfg.rx_chan_id.nb; qidx++) {
		ndev->rx_ring[qidx].qidx = qidx;
		ret = kvx_eth_alloc_rx_ring(ndev, &ndev->rx_ring[qidx]);
		if (ret)
			goto alloc_failed;
	}

	return 0;

alloc_failed:
	for (i = qidx - 1; i >= 0; i--)
		kvx_eth_release_rx_ring(&ndev->rx_ring[i], 0);

	return ret;
}

void kvx_eth_release_rx_res(struct net_device *netdev, int keep_dma_chan)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	int qidx;

	for (qidx = 0; qidx < ndev->dma_cfg.rx_chan_id.nb; qidx++)
		kvx_eth_release_rx_ring(&ndev->rx_ring[qidx], keep_dma_chan);
}

int kvx_eth_alloc_tx_ring(struct kvx_eth_netdev *ndev, struct kvx_eth_ring *r)
{
	int i, ret = 0;

	r->netdev = ndev->netdev;
	r->next_to_use = 0;
	r->next_to_clean = 0;
	if (r->count == 0)
		r->count = kvx_dma_get_max_nb_desc(ndev->dma_cfg.pdev);
	r->tx_buf = kcalloc(r->count, sizeof(*r->tx_buf), GFP_KERNEL);
	if (!r->tx_buf) {
		netdev_err(r->netdev, "TX ring allocation failed\n");
		return -ENOMEM;
	}
	for (i = 0; i < r->count; ++i) {
		/* initialize scatterlist to the maximum size */
		sg_init_table(r->tx_buf[i].sg, MAX_SKB_FRAGS + 1);
		r->tx_buf[i].ndev = ndev;
	}
	memset(&r->config, 0, sizeof(r->config));
	r->config.cfg.direction = DMA_MEM_TO_DEV;
	r->config.trans_type = KVX_DMA_TYPE_MEM2ETH;
	r->config.dir = KVX_DMA_DIR_TYPE_TX;
	r->config.noc_route = noc_route_c2eth(ndev->hw->eth_id,
					      kvx_cluster_id());
	/* rx_tag must refer to tx_fifo_id*/
	r->config.rx_tag = ndev->cfg.tx_fifo_id;
	r->config.qos_id = 0;

	/* Keep opened channel (only realloc tx_buf) */
	if (!r->chan) {
		r->chan = of_dma_request_slave_channel(ndev->dev->of_node,
						       "tx");
		if (!r->chan) {
			netdev_err(r->netdev, "Request dma TX chan failed\n");
			ret = -EINVAL;
			goto chan_failed;
		}
		/* Config dma */
		ret = dmaengine_slave_config(r->chan, &r->config.cfg);
		if (ret)
			goto config_failed;
	}

	return 0;

config_failed:
	dma_release_channel(r->chan);
chan_failed:
	kfree(r->tx_buf);
	r->tx_buf = NULL;

	return ret;
}

/* kvx_eth_release_tx_ring() - Release TX resources
 * @r: Ring to be released
 * @keep_dma_chan: do not release dma channel
 */
void kvx_eth_release_tx_ring(struct kvx_eth_ring *r, int keep_dma_chan)
{
	struct kvx_eth_netdev *ndev = netdev_priv(r->netdev);
	struct kvx_eth_tx_f *tx_f;

	if (!keep_dma_chan)
		dma_release_channel(r->chan);
	tx_f = &ndev->hw->tx_f[ndev->dma_cfg.tx_chan_id.start + r->qidx];
	list_del_init(&tx_f->node);
	kfree(r->tx_buf);
	r->tx_buf = NULL;
}

/* kvx_eth_alloc_tx_res() - Allocate TX resources (including dma_noc channel)
 * @netdev: Current netdev
 *
 * Return: 0 on success, < 0 on failure
 */
static int kvx_eth_alloc_tx_res(struct net_device *netdev)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_eth_tx_f *tx_f;
	struct kvx_eth_ring *r;
	int i, qidx, ret = 0;

	tx_f = &ndev->hw->tx_f[ndev->cfg.tx_fifo_id];
	tx_f->lane_id = ndev->cfg.id;
	list_add_tail(&tx_f->node, &ndev->cfg.tx_fifo_list);
	for (qidx = 0; qidx < ndev->dma_cfg.tx_chan_id.nb; qidx++) {
		r = &ndev->tx_ring[qidx];
		r->qidx = qidx;

		ret = kvx_eth_alloc_tx_ring(ndev, r);
		if (ret)
			goto alloc_failed;
	}

	return 0;

alloc_failed:
	list_del_init(&tx_f->node);
	for (i = qidx - 1; i >= 0; i--)
		kvx_eth_release_tx_ring(&ndev->tx_ring[i], 0);

	return ret;
}

static void kvx_eth_release_tx_res(struct net_device *netdev, int keep_dma_chan)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	int qidx;

	for (qidx = 0; qidx < ndev->dma_cfg.tx_chan_id.nb; qidx++)
		kvx_eth_release_tx_ring(&ndev->tx_ring[qidx], keep_dma_chan);
}

static int kvx_eth_get_queue_nb(struct platform_device *pdev,
				struct kvx_eth_node_id *txq,
				struct kvx_eth_node_id *rxq)
{
	struct device_node *np = pdev->dev.of_node;

	if (of_property_read_u32_array(np, "kalray,dma-tx-channel-ids",
					(u32 *)txq, 2)) {
		dev_err(&pdev->dev, "Unable to get dma-tx-channel-ids\n");
		return -EINVAL;
	}
	if (txq->nb > 1) {
		dev_err(&pdev->dev, "TX channels nb (%d) is limited to 1\n",
			txq->nb);
		return -EINVAL;
	}

	if (of_property_read_u32_array(np, "kalray,dma-rx-channel-ids",
				       (u32 *)rxq, 2)) {
		dev_err(&pdev->dev, "Unable to get dma-rx-channel-ids\n");
		return -EINVAL;
	}
	if (rxq->nb > RX_CACHE_NB) {
		dev_warn(&pdev->dev, "Limiting RX queue number to %d\n",
			 RX_CACHE_NB);
		rxq->nb = RX_CACHE_NB;
	}
	if (rxq->start + rxq->nb > KVX_ETH_RX_TAG_NB) {
		dev_err(&pdev->dev, "RX channels (%d) exceeds max value (%d)\n",
			rxq->start + rxq->nb, KVX_ETH_RX_TAG_NB);
		return -EINVAL;
	}
	return 0;
}

/* kvx_eth_check_dma() - Check dma noc driver and device correclty loaded
 *
 * @pdev: netdev platform device pointer
 * @np_dma: dma device node pointer
 * Return: dma platform device on success, NULL on failure
 */
static struct platform_device *kvx_eth_check_dma(struct platform_device *pdev,
						 struct device_node **np_dma)
{
	struct platform_device *dma_pdev;

	*np_dma = of_parse_phandle(pdev->dev.of_node, "dmas", 0);
	if (!(*np_dma)) {
		dev_err(&pdev->dev, "Failed to get dma\n");
		return NULL;
	}
	dma_pdev = of_find_device_by_node(*np_dma);
	if (!dma_pdev || !platform_get_drvdata(dma_pdev)) {
		dev_err(&pdev->dev, "Failed to get dma_noc platform_device\n");
		return NULL;
	}

	return dma_pdev;
}

/* kvx_eth_rtm_parse_dt() - Parse retimer related device tree inputs
 *
 * @pdev: platform device
 * @dev: Current kvx_eth_dev
 * Return: 0 on success, < 0 on failure
 */
int kvx_eth_rtm_parse_dt(struct platform_device *pdev, struct kvx_eth_dev *dev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *rtm_node;
	int rtm, ret;

	for (rtm = 0; rtm < RTM_NB; rtm++) {
		rtm_node = of_parse_phandle(pdev->dev.of_node,
				rtm_prop_name[rtm], 0);
		if (!rtm_node) {
			/* This board is missing retimers, throw an info and
			 * return to stop parsing other retimer parameters
			 */
			dev_info(&pdev->dev, "No node %s found\n",
					rtm_prop_name[rtm]);
			return 0;
		}
		dev->hw.rtm_params[rtm].rtm = of_find_i2c_device_by_node(rtm_node);
		if (!dev->hw.rtm_params[rtm].rtm)
			return -EPROBE_DEFER;
	}

	for (rtm = 0; rtm < RTM_NB; rtm++) {
		ret = of_property_count_u32_elems(np,
				rtm_channels_prop_name[rtm]);
		if (ret < 0) {
			dev_err(&pdev->dev, "Unable to get %s\n",
					rtm_channels_prop_name[rtm]);
			return -EINVAL;
		} else if (ret != KVX_ETH_LANE_NB) {
			dev_err(&pdev->dev, "Incorrect channels number for %s (got %d, want %d)\n",
					rtm_channels_prop_name[rtm], ret,
					KVX_ETH_LANE_NB);
			return -EINVAL;
		}
		ret = of_property_read_u32_array(np,
				rtm_channels_prop_name[rtm],
				dev->hw.rtm_params[rtm].channels,
				KVX_ETH_LANE_NB);
		if (ret) {
			dev_err(&pdev->dev, "Failed to request %s\n",
					rtm_channels_prop_name[rtm]);
			return ret;
		}
	}

	return 0;
}

/* kvx_eth_dev_parse_dt() - Parse eth device tree inputs
 *
 * @pdev: platform device
 * @dev: Current kvx_eth_dev
 * Return: 0 on success, < 0 on failure
 */
int kvx_eth_dev_parse_dt(struct platform_device *pdev, struct kvx_eth_dev *dev)
{
	struct device_node *np = pdev->dev.of_node;
	u32 tmp_rx_polarities[KVX_ETH_LANE_NB] = {0};
	u32 tmp_tx_polarities[KVX_ETH_LANE_NB] = {0};
	struct nvmem_cell *cell;
	int i, ret = 0;
	u8 *cell_data;
	size_t len;
	u32 val;

	if (of_property_read_u32(np, "cell-index", &dev->hw.eth_id)) {
		dev_warn(&pdev->dev, "Default kvx ethernet index to 0\n");
		dev->hw.eth_id = KVX_ETH0;
	}

	if (of_property_read_u32(np, "kalray,rxtx-crossed",
			&dev->hw.rxtx_crossed) != 0)
		dev->hw.rxtx_crossed = 0;
	if (of_property_read_u32(np, "kalray,parsers_tictoc",
			&dev->hw.parsers_tictoc) != 0) {
		dev_err(&pdev->dev, "kalray,parsers_tictoc property not found but required\n");
		return -EINVAL;
	}
	dev_info(&pdev->dev, "parser tictoc: %d\n", dev->hw.parsers_tictoc);

	if (of_property_read_u32_array(np, "kalray,dma-rx-chan-error",
				       (u32 *)&dev->hw.rx_chan_error, 1) != 0)
		dev->hw.rx_chan_error = 0xFF;

	of_property_read_u32_array(np, "kalray,rx-phy-polarities",
			tmp_rx_polarities, KVX_ETH_LANE_NB);
	of_property_read_u32_array(np, "kalray,tx-phy-polarities",
			tmp_tx_polarities, KVX_ETH_LANE_NB);

	for (i = 0; i < KVX_ETH_LANE_NB; i++) {
		dev->hw.phy_f.polarities[i].rx = (bool) tmp_rx_polarities[i];
		dev->hw.phy_f.polarities[i].tx = (bool) tmp_tx_polarities[i];
	}

	cell = nvmem_cell_get(&pdev->dev, "ews_fuse");
	if (!IS_ERR(cell)) {
		cell_data = nvmem_cell_read(cell, &len);
		nvmem_cell_put(cell);
		if (!IS_ERR(cell_data))
			dev->hw.mppa_id = *(u64 *)cell_data;
		kfree(cell_data);
	}

	cell = nvmem_cell_get(&pdev->dev, "ft_fuse");
	if (!IS_ERR(cell)) {
		cell_data = nvmem_cell_read(cell, &len);
		nvmem_cell_put(cell);
		if (!IS_ERR(cell_data)) {
			val = *(u32 *)cell_data;
			dev->hw.dev_id = (val >> 22) & 0x1FF;
		}
		kfree(cell_data);
	}
	ret = kvx_eth_rtm_parse_dt(pdev, dev);

	if (of_property_read_u8(np, "kalray,fom_thres", &dev->hw.fom_thres))
		dev->hw.fom_thres = FOM_THRESHOLD;

	return ret;
}

/* kvx_eth_netdev_set_hw_addr() - Use nvmem to get mac addr
 *
 * @ndev: kvx net device
 */
static void kvx_eth_netdev_set_hw_addr(struct kvx_eth_netdev *ndev)
{
	const void *addr = NULL;
	struct net_device *netdev = ndev->netdev;
	struct kvx_eth_dev *dev = KVX_DEV(ndev);
	struct device *d = &dev->pdev->dev;
	u8 *a, tmp[6];
	u64 h;

	addr = of_get_mac_address(ndev->netdev->dev.of_node);
	if (!IS_ERR(addr) && addr) {
		a = (u8 *)addr;
		/* set local assignment bit (IEEE802) */
		a[0] |= 0x02;
	} else if (false) {
		/* waiting for MAC address in fuse */
	} else if (dev->hw.mppa_id != 0) {
		h = hash_64((dev->hw.mppa_id << 9) | dev->hw.dev_id, 16);
		/* set local assignment bit (IEEE802) */
		tmp[0] = 0xA0 | 0x02;
		tmp[1] = 0x28;
		tmp[2] = 0x33;
		tmp[3] = 0xC0 | (0x0F & h);
		tmp[4] = 0xFF & (h >> 4);
		tmp[5] = (0xF0 & ((h >> 12) << 4)) |
	       ((ndev->hw->eth_id << 2 | ndev->cfg.id) & 0x0F);
	    a = tmp;
	} else {
		dev_warn(d, "Using random hwaddr\n");
		eth_hw_addr_random(netdev);
		ether_addr_copy(ndev->cfg.mac_f.addr, netdev->dev_addr);
		return;
	}

	netdev->addr_assign_type = NET_ADDR_PERM;
	ether_addr_copy(netdev->dev_addr, a);
	ether_addr_copy(ndev->cfg.mac_f.addr, a);
}

/* kvx_eth_netdev_parse_dt() - Parse netdev device tree inputs
 *
 * Sets dma properties accordingly (dma_mem and iommu nodes)
 *
 * @pdev: platform device
 * @ndev: Current kvx_eth_netdev
 * Return: 0 on success, < 0 on failure
 */
int kvx_eth_netdev_parse_dt(struct platform_device *pdev,
		struct kvx_eth_netdev *ndev)
{
	struct kvx_dma_config *dma_cfg = &ndev->dma_cfg;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *np_dma;
	char pname[20];
	int i, ret = 0;

	dma_cfg->pdev = kvx_eth_check_dma(pdev, &np_dma);
	if (!dma_cfg->pdev)
		return -ENODEV;

	ret = of_dma_configure(&pdev->dev, np_dma, true);
	if (ret) {
		dev_err(&pdev->dev, "Failed to configure dma\n");
		return -EINVAL;
	}
	if (iommu_get_domain_for_dev(&pdev->dev)) {
		struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(&pdev->dev);

		if (fwspec && fwspec->num_ids) {
			ndev->hw->asn = fwspec->ids[0];
			dev_dbg(&pdev->dev, "ASN: %d\n", ndev->hw->asn);
		} else {
			dev_err(&pdev->dev, "Unable to get ASN property\n");
			return -ENODEV;
		}
	}

	of_property_read_u32(np_dma, "kalray,dma-noc-vchan", &ndev->hw->vchan);
	if (of_property_read_u32(np, "kalray,dma-rx-cache-id",
				 &dma_cfg->rx_cache_id)) {
		dev_err(ndev->dev, "Unable to get dma-rx-cache-id\n");
		return -EINVAL;
	}
	if (dma_cfg->rx_cache_id >= RX_CACHE_NB) {
		dev_err(ndev->dev, "dma-rx-cache-id >= %d\n", RX_CACHE_NB);
		return -EINVAL;
	}
	ret = kvx_eth_get_queue_nb(pdev, &dma_cfg->tx_chan_id,
				   &dma_cfg->rx_chan_id);
	if (ret)
		return ret;

	if (of_property_read_u32_array(np, "kalray,dma-rx-comp-queue-ids",
			       (u32 *)&dma_cfg->rx_compq_id, 2) != 0) {
		dev_err(ndev->dev, "Unable to get dma-rx-comp-queue-ids\n");
		return -EINVAL;
	}

	if (dma_cfg->rx_chan_id.start != dma_cfg->rx_compq_id.start ||
	    dma_cfg->rx_chan_id.nb != dma_cfg->rx_compq_id.nb) {
		dev_err(ndev->dev, "rx_chan_id(%d,%d) != rx_compq_id(%d,%d)\n",
			dma_cfg->rx_chan_id.start, dma_cfg->rx_chan_id.nb,
			dma_cfg->rx_compq_id.start, dma_cfg->rx_compq_id.nb);
		return -EINVAL;
	}

	if (of_property_read_u32_array(np, "kalray,default-dispatch-entry",
			(u32 *)&ndev->cfg.default_dispatch_entry, 1) != 0)
	     ndev->cfg.default_dispatch_entry = KVX_ETH_DEFAULT_RULE_DTABLE_IDX;

	if (of_property_read_u32(np, "kalray,lane", &ndev->cfg.id)) {
		dev_err(ndev->dev, "Unable to get lane\n");
		return -EINVAL;
	}
	if (ndev->cfg.id >= KVX_ETH_LANE_NB) {
		dev_err(ndev->dev, "lane >= %d\n", KVX_ETH_LANE_NB);
		return -EINVAL;
	}

	/* Always the case (means that netdev can share tx dma jobq) */
	ndev->cfg.tx_fifo_id = dma_cfg->tx_chan_id.start;
	if (ndev->cfg.tx_fifo_id >= TX_FIFO_NB) {
		dev_err(ndev->dev, "tx_fifo >= %d\n", TX_FIFO_NB);
		return -EINVAL;
	}

	/* Default tx eq. parameter tuning */
	if (!of_property_read_u32_array(np, "kalray,phy-param",
			(u32 *)&ndev->hw->phy_f.param[ndev->cfg.id], 3))
		ndev->hw->phy_f.param[ndev->cfg.id].ovrd_en = true;
	/* For aggregated config, allow different params for lanes 1..3 */
	for (i = 1; i < KVX_ETH_LANE_NB; i++) {
		snprintf(pname, 20, "kalray,phy-param%d", i);
		if (!of_property_read_u32_array(np, pname,
					(u32 *)&ndev->hw->phy_f.param[i], 3))
			ndev->hw->phy_f.param[i].ovrd_en = true;
	}

	return 0;
}

static void kvx_phylink_validate(struct phylink_config *cfg,
			 unsigned long *supported,
			 struct phylink_link_state *state)
{
	struct net_device *netdev = to_net_dev(cfg->dev);
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mac_supported) = { 0, };
	__ETHTOOL_DECLARE_LINK_MODE_MASK(adv) = { 0, };
	__ETHTOOL_DECLARE_LINK_MODE_MASK(additional_prot) = { 0, };

	if (kvx_eth_get_module_transceiver(netdev, &ndev->cfg.transceiver))
		return;

	/*
	 * Indicate all capabilities supported by the MAC
	 * The type of media (fiber/copper/...) is dependant
	 * on the module, the PCS encoding (R flag) is the same
	 * so we must indicate that the MAC/PCS support them.
	 */
	phylink_set(mac_supported, Autoneg);
	phylink_set(mac_supported, Pause);
	phylink_set(mac_supported, Asym_Pause);
	phylink_set_port_modes(mac_supported);
	bitmap_copy(adv, mac_supported, __ETHTOOL_LINK_MODE_MASK_NBITS);
	phylink_set(mac_supported, 10baseT_Half);
	phylink_set(mac_supported, 10baseT_Full);
	phylink_set(mac_supported, 100baseT_Half);
	phylink_set(mac_supported, 100baseT_Full);
	phylink_set(mac_supported, 1000baseT_Full);
	phylink_set(mac_supported, 10000baseCR_Full);
	phylink_set(mac_supported, 10000baseSR_Full);
	phylink_set(mac_supported, 10000baseLR_Full);
	phylink_set(mac_supported, 10000baseER_Full);
	phylink_set(mac_supported, 25000baseCR_Full);
	phylink_set(mac_supported, 25000baseSR_Full);
	phylink_set(mac_supported, 40000baseCR4_Full);
	phylink_set(mac_supported, 40000baseSR4_Full);
	phylink_set(mac_supported, 40000baseLR4_Full);
	phylink_set(mac_supported, 100000baseKR4_Full);
	phylink_set(mac_supported, 100000baseCR4_Full);
	phylink_set(mac_supported, 100000baseSR4_Full);
	phylink_set(mac_supported, 100000baseLR4_ER4_Full);

	netdev_dbg(netdev, "%s: state->speed: %d\n", __func__, state->speed);
	/*
	 * Fill advertising with real expected speed. It *must* be different
	 * for each requested speed for change rate test cases
	 */
	switch (state->speed) {
	case SPEED_40000:
		phylink_set(adv, 40000baseCR4_Full);
		phylink_set(adv, 40000baseSR4_Full);
		phylink_set(adv, 40000baseLR4_Full);
		fallthrough;
	case SPEED_10000:
		phylink_set(adv, 10000baseCR_Full);
		phylink_set(adv, 10000baseSR_Full);
		phylink_set(adv, 10000baseLR_Full);
		phylink_set(adv, 10000baseER_Full);
		break;
	case SPEED_100000:
		phylink_set(adv, 100000baseKR4_Full);
		phylink_set(adv, 100000baseCR4_Full);
		phylink_set(adv, 100000baseSR4_Full);
		phylink_set(adv, 100000baseLR4_ER4_Full);
		fallthrough;
	case SPEED_25000:
		phylink_set(adv, 25000baseCR_Full);
		phylink_set(adv, 25000baseSR_Full);
		break;
	default:
		break;
	}

	phylink_set(additional_prot, FEC_NONE);
	phylink_set(additional_prot, FEC_RS);
	phylink_set(additional_prot, FEC_BASER);
	bitmap_or(adv, adv, additional_prot, __ETHTOOL_LINK_MODE_MASK_NBITS);

	/*
	 * Match media or module capabilities with MAC capabilities
	 * The AND operation select only capabilities supported by both
	 * the SFP/QSFP module and the MAC
	 */
	bitmap_and(supported, supported, mac_supported,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);

	if (state->an_enabled) /* Advertise all supported speeds */
		bitmap_and(state->advertising, state->advertising,
			   mac_supported, __ETHTOOL_LINK_MODE_MASK_NBITS);
	else /* Advertise only requested speed */
		bitmap_copy(state->advertising, adv, __ETHTOOL_LINK_MODE_MASK_NBITS);

	if (state->interface == PHY_INTERFACE_MODE_SGMII)
		return;

	/*
	 * With sfp/qsfp, the match is too restrictive in some cases.
	 * Handle those special cases separatelly
	 */
	if (ndev->cfg.transceiver.id == 0) {
		/*
		 * Some cable (e.g. splitters) do not have en eeprom
		 * This is user responsability to choose a proper protocol
		 */
		bitmap_or(additional_prot, additional_prot, mac_supported,
			 __ETHTOOL_LINK_MODE_MASK_NBITS);
	} else if (ndev->cfg.transceiver.qsfp) {
		/*
		 * Some cable such as mellanox to not indicate their
		 * full capabilities. As a workaround when a cable support
		 * 25GBase assume a 100G Base is supported on qsfp cage
		 * (cable designed for aggregated lane)
		 */
		if (KVX_TEST_BIT(25000baseCR_Full, supported))
			phylink_set(additional_prot, 100000baseCR4_Full);
		if (KVX_TEST_BIT(25000baseSR_Full, supported))
			phylink_set(additional_prot, 100000baseSR4_Full);
		if (KVX_TEST_BIT(40000baseCR4_Full, supported))
			phylink_set(additional_prot, 10000baseCR_Full);
		if (KVX_TEST_BIT(40000baseSR4_Full, supported))
			phylink_set(additional_prot, 10000baseSR_Full);
		if (KVX_TEST_BIT(40000baseLR4_Full, supported))
			phylink_set(additional_prot, 10000baseLR_Full);
	}

	bitmap_or(supported, supported, additional_prot, __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static void kvx_phylink_mac_pcs_state(struct phylink_config *cfg,
				      struct phylink_link_state *state)
{
	struct net_device *netdev = to_net_dev(cfg->dev);
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	if (kvx_eth_phy_is_bert_en(ndev->hw))
		state->link = false;
	else
		state->link = kvx_eth_mac_getlink(ndev->hw, &ndev->cfg);
	state->speed = ndev->cfg.speed;
	state->duplex = ndev->cfg.duplex;
	state->pause = 0;
	if (ndev->hw->lb_f[ndev->cfg.id].pfc_f.global_pause_en)
		state->pause = MLO_PAUSE_RX | MLO_PAUSE_TX;
	netdev_dbg(netdev, "%s link: %d state->speed: %d ndev->speed: %d\n",
		   __func__, state->link, state->speed, ndev->cfg.speed);
}

int kvx_eth_speed_to_nb_lanes(unsigned int speed, unsigned int *lane_speed)
{
	int nb_lanes;
	unsigned int tmp_lane_speed;

	switch (speed) {
	case SPEED_100000:
		nb_lanes = KVX_ETH_LANE_NB;
		tmp_lane_speed = SPEED_25000;
		break;
	case SPEED_40000:
		nb_lanes = KVX_ETH_LANE_NB;
		tmp_lane_speed = SPEED_10000;
		break;
	case SPEED_50000:
		nb_lanes = 2;
		tmp_lane_speed = SPEED_25000;
		break;
	case SPEED_25000:
	case SPEED_10000:
		nb_lanes = 1;
		tmp_lane_speed = speed;
		break;
	case SPEED_1000:
		nb_lanes = 1;
		tmp_lane_speed = speed;
		break;
	default:
		return 0;
	}

	if (lane_speed != NULL)
		*lane_speed = tmp_lane_speed;

	return nb_lanes;
}

int speed_to_rtm_speed_index(unsigned int speed)
{
	switch (speed) {
	case SPEED_100000:
	case SPEED_50000:
	case SPEED_25000:
		return RTM_SPEED_25G;
	case SPEED_40000:
	case SPEED_10000:
		return RTM_SPEED_10G;
	default:
		return -EINVAL;
	}
}

int configure_rtm(struct kvx_eth_hw *hw, unsigned int lane_id,
		  unsigned int rtm, unsigned int speed)
{
	struct kvx_eth_rtm_params *params = &hw->rtm_params[rtm];
	int i, lane_speed, rtm_speed_idx;
	int nb_lanes, lane;

	if (rtm > RTM_NB) {
		dev_err(hw->dev, "Unknown retimer id %d\n", rtm);
		return -EINVAL;
	}
	if (!params->rtm) {
		dev_dbg(hw->dev, "No retimers to configure\n");
		return 0;
	}

	nb_lanes = kvx_eth_speed_to_nb_lanes(speed, &lane_speed);
	if (nb_lanes < 0) {
		dev_err(hw->dev, "Unsupported speed %d\n", speed);
		return -EINVAL;
	}
	rtm_speed_idx = speed_to_rtm_speed_index(speed);
	if (rtm_speed_idx < 0) {
		dev_err(hw->dev, "Speed %d not supported by retimer\n", speed);
		return -EINVAL;
	}
	dev_dbg(hw->dev, "Setting retimer%d speed to %d\n", rtm, speed);

	for (i = lane_id; i < nb_lanes; i++) {
		lane = params->channels[i];

		ti_retimer_set_speed(params->rtm, lane, lane_speed);
	}

	return 0;
}

/**
 * kvx_eth_autoneg() - Autoneg config: set phy/serdes in 10G mode (mandatory)
 */
static int kvx_eth_autoneg(struct kvx_eth_netdev *ndev)
{
	struct kvx_eth_dev *dev = KVX_DEV(ndev);

	if (dev->hw.rxtx_crossed) {
		netdev_err(ndev->netdev, "Autonegotiation is not supported with inverted lanes\n");
		return -EINVAL;
	}

	return kvx_eth_an_execute(ndev->hw, &ndev->cfg);
}

static void kvx_phylink_mac_config(struct phylink_config *cfg,
				   unsigned int an_mode,
				   const struct phylink_link_state *state)
{
	struct net_device *netdev = to_net_dev(cfg->dev);
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_eth_pfc_f *pfc_f = &ndev->hw->lb_f[ndev->cfg.id].pfc_f;
	int an_enabled = state->an_enabled;
	u8 pause = !!(state->pause & (MLO_PAUSE_RX | MLO_PAUSE_TX));
	bool update_serdes = false;
	int speed_fmt, ret = 0;
	char *unit;

	netdev_dbg(ndev->netdev, "%s state->speed: %d ndev->speed: %d\n",
		   __func__, state->speed, ndev->cfg.speed);

	if (state->interface == PHY_INTERFACE_MODE_SGMII) {
		/*
		 * Speed might be undetermined when autoneg is enabled
		 * but has not completed yet. By setting a default speed
		 * it ensures that the minimum configuration required
		 * for autoneg to complete successfully is done
		 */
		if (state->speed == SPEED_UNKNOWN)
			ndev->cfg.speed = SPEED_1000;
		if (state->duplex == DUPLEX_UNKNOWN)
			ndev->cfg.duplex = DUPLEX_FULL;
		/*
		 * SGMII autoneg is based on clause 37 (not clause 73).
		 * This avoid a timeout and make link up faster.
		 */
		an_enabled = false;
		update_serdes = true;
	}
	/* Check if a sfp/qsfp module is inserted */
	else if (ndev->cfg.transceiver.id == 0) {
		netdev_warn(ndev->netdev, "No cable detected\n");
		return;
	}

	if (kvx_eth_phy_is_bert_en(ndev->hw)) {
		netdev_warn(ndev->netdev, "Trying to reconfigure mac while BERT is enabled\n");
		return;
	}

	if (state->interface != PHY_INTERFACE_MODE_NA)
		ndev->cfg.phy_mode = state->interface;
	ndev->cfg.an_mode = an_mode;

	update_serdes = (ndev->cfg.speed != state->speed ||
			 ndev->cfg.duplex != state->duplex);

	if (state->speed != SPEED_UNKNOWN)
		ndev->cfg.speed = state->speed;
	if (state->duplex != DUPLEX_UNKNOWN)
		ndev->cfg.duplex = state->duplex;
	ndev->cfg.fec = 0;

	if (!(pfc_f->global_pause_en && pause)) {
		pfc_f->global_pause_en = pause;
		kvx_eth_pfc_f_cfg(ndev->hw, pfc_f);
	}

	if (an_enabled && !ndev->cfg.mac_f.loopback_mode) {
		ret = kvx_eth_autoneg(ndev);
		/* If AN is successful MAC/PHY are already configured on
		 * correct mode as link training requires to be performed at
		 * nominal speed
		 */
		if (ret == 0)
			return;

		kvx_eth_get_formated_speed(ndev->cfg.speed, &speed_fmt, &unit);
		netdev_warn(netdev, "Autonegotiation failed, using default speed %i%s\n",
				speed_fmt, unit);
		update_serdes = true;
	}

	kvx_eth_mac_pcs_pma_hcd_setup(ndev->hw, &ndev->cfg, update_serdes);
	/* Force re-assess link state */
	kvx_eth_mac_getlink(ndev->hw, &ndev->cfg);
}

static void kvx_phylink_mac_an_restart(struct phylink_config *cfg)
{
	pr_debug("%s\n", __func__);
}

static void kvx_phylink_mac_link_down(struct phylink_config *config,
				      unsigned int mode,
				      phy_interface_t interface)
{
	struct net_device *netdev = to_net_dev(config->dev);

	netdev_dbg(netdev, "%s carrier: %d\n", __func__,
		   netif_carrier_ok(netdev));
}

static void kvx_phylink_mac_link_up(struct phylink_config *config,
				    struct phy_device *phy, unsigned int mode,
				    phy_interface_t interface, int speed,
				    int duplex, bool tx_pause, bool rx_pause)
{
	struct net_device *netdev = to_net_dev(config->dev);

	netdev_dbg(netdev, "%s carrier: %d\n", __func__,
		   netif_carrier_ok(netdev));
}

const static struct phylink_mac_ops kvx_phylink_ops = {
	.validate          = kvx_phylink_validate,
	.mac_pcs_get_state = kvx_phylink_mac_pcs_state,
	.mac_config        = kvx_phylink_mac_config,
	.mac_an_restart    = kvx_phylink_mac_an_restart,
	.mac_link_down     = kvx_phylink_mac_link_down,
	.mac_link_up       = kvx_phylink_mac_link_up,
};

/* kvx_eth_create_netdev() - Create new netdev
 * @pdev: Platform device
 * @dev: parent device
 * @cfg: configuration (duplicated to kvx_eth_netdev)
 *
 * Return: new kvx_eth_netdev on success, NULL on failure
 */
static struct kvx_eth_netdev*
kvx_eth_create_netdev(struct platform_device *pdev, struct kvx_eth_dev *dev)
{
	struct kvx_eth_netdev *ndev;
	struct net_device *netdev;
	struct kvx_eth_node_id txq, rxq;
	struct phylink *phylink;
	int phy_mode;
	int ret = 0;

	ret = kvx_eth_get_queue_nb(pdev, &txq, &rxq);
	if (ret)
		return NULL;
	netdev = devm_alloc_etherdev_mqs(&pdev->dev, sizeof(*ndev),
					 txq.nb, rxq.nb);
	if (!netdev) {
		dev_err(&pdev->dev, "Failed to alloc netdev\n");
		return NULL;
	}
	SET_NETDEV_DEV(netdev, &pdev->dev);
	ndev = netdev_priv(netdev);
	memset(ndev, 0, sizeof(*ndev));
	netdev->netdev_ops = &kvx_eth_netdev_ops;
	netdev->mtu = ETH_DATA_LEN;
	netdev->max_mtu = KVX_ETH_MAX_MTU;
	ndev->dev = &pdev->dev;
	ndev->netdev = netdev;
	ndev->hw = &dev->hw;
	ndev->cfg.hw = ndev->hw;
	ndev->phylink_cfg.dev = &netdev->dev;
	ndev->phylink_cfg.type = PHYLINK_NETDEV;
	ndev->phylink_cfg.pcs_poll = false;
	INIT_LIST_HEAD(&ndev->cfg.tx_fifo_list);
	timer_setup(&ndev->link_poll, kvx_eth_poll_link, 0);

	phy_mode = fwnode_get_phy_mode(pdev->dev.fwnode);
	if (phy_mode < 0) {
		dev_err(&pdev->dev, "phy mode not set\n");
		return NULL;
	}

	ret = kvx_eth_netdev_parse_dt(pdev, ndev);
	if (ret)
		return NULL;

	phylink = phylink_create(&ndev->phylink_cfg, pdev->dev.fwnode,
				       phy_mode, &kvx_phylink_ops);
	if (IS_ERR(phylink)) {
		ret = PTR_ERR(phylink);
		dev_err(&pdev->dev, "phylink_create error (%i)\n", ret);
		return NULL;
	}
	ndev->phylink = phylink;

	kvx_eth_netdev_set_hw_addr(ndev);

	/* Allocate RX/TX rings */
	ret = kvx_eth_alloc_rx_res(netdev);
	if (ret)
		goto exit;

	ret = kvx_eth_alloc_tx_res(netdev);
	if (ret)
		goto tx_chan_failed;

	kvx_set_ethtool_ops(netdev);
	kvx_set_dcb_ops(netdev);

	/* Register the network device */
	ret = register_netdev(netdev);
	if (ret) {
		netdev_err(netdev, "Failed to register netdev (%i)\n", ret);
		goto err;
	}

	/* Populate list of netdev */
	INIT_LIST_HEAD(&ndev->node);
	list_add(&ndev->node, &dev->list);

	return ndev;

err:
	kvx_eth_release_tx_res(netdev, 0);
tx_chan_failed:
	kvx_eth_release_rx_res(netdev, 0);
exit:
	netdev_err(netdev, "Failed to create netdev\n");
	phylink_destroy(ndev->phylink);
	return NULL;
}

/* kvx_eth_free_netdev() - Releases netdev
 * @ndev: Current kvx_eth_netdev
 *
 * Return: 0
 */
static int kvx_eth_free_netdev(struct kvx_eth_netdev *ndev)
{
	del_timer_sync(&ndev->link_poll);
	kvx_eth_release_tx_res(ndev->netdev, 0);
	kvx_eth_release_rx_res(ndev->netdev, 0);
	phylink_destroy(ndev->phylink);
	list_del(&ndev->node);

	return 0;
}

/* kvx_netdev_probe() - Probe netdev
 * @pdev: Platform device
 *
 * Return: 0 on success, < 0 on failure
 */
static int kvx_netdev_probe(struct platform_device *pdev)
{
	struct device_node *np_dma, *np_dev = of_get_parent(pdev->dev.of_node);
	struct platform_device *ppdev = of_find_device_by_node(np_dev);
	struct kvx_eth_dev *dev = platform_get_drvdata(ppdev);
	struct kvx_eth_netdev *ndev = NULL;
	struct platform_device *dma_pdev;
	int ret = 0;

	/* Check dma noc probed and available */
	dma_pdev = kvx_eth_check_dma(pdev, &np_dma);
	if (!dma_pdev)
		return -ENODEV;

	/* Config DMA */
	dmaengine_get();
	ndev = kvx_eth_create_netdev(pdev, dev);
	if (!ndev) {
		dev_err(&pdev->dev, "Probe defer\n");
		ret = -EPROBE_DEFER;
		goto err;
	}

	platform_set_drvdata(pdev, ndev);
	ret = kvx_eth_init_netdev(ndev);
	if (ret)
		goto err;

	kvx_eth_lb_set_default(&dev->hw, &ndev->cfg);
	kvx_eth_pfc_f_set_default(&dev->hw, &ndev->cfg);

	kvx_eth_fill_dispatch_table(&dev->hw, &ndev->cfg,
				    ndev->dma_cfg.rx_chan_id.start);
	kvx_eth_tx_fifo_cfg(&dev->hw, &ndev->cfg);
	kvx_eth_lb_f_cfg(&dev->hw, &ndev->hw->lb_f[ndev->cfg.id]);

	ret = kvx_eth_netdev_sysfs_init(ndev);
	if (ret)
		netdev_warn(ndev->netdev, "Failed to initialize sysfs\n");

	dev_info(&pdev->dev, "KVX netdev[%d] probed\n", ndev->cfg.id);

	return 0;

err:
	if (ndev)
		kvx_eth_free_netdev(ndev);
	dmaengine_put();
	return ret;
}

/* kvx_netdev_remove() - Remove netdev
 * @pdev: Platform device
 *
 * Return: 0
 */
static int kvx_netdev_remove(struct platform_device *pdev)
{
	struct kvx_eth_netdev *ndev = platform_get_drvdata(pdev);
	int rtm;

	kvx_eth_netdev_sysfs_uninit(ndev);
	for (rtm = 0; rtm < RTM_NB; rtm++) {
		if (ndev->hw->rtm_params[rtm].rtm)
			put_device(&ndev->hw->rtm_params[rtm].rtm->dev);
	}
	if (netif_running(ndev->netdev))
		kvx_eth_netdev_stop(ndev->netdev);
	kvx_eth_free_netdev(ndev);
	dmaengine_put();

	return 0;
}

static const struct of_device_id kvx_netdev_match[] = {
	{ .compatible = "kalray,kvx-net" },
	{ }
};
MODULE_DEVICE_TABLE(of, kvx_netdev_match);

static struct platform_driver kvx_netdev_driver = {
	.probe = kvx_netdev_probe,
	.remove = kvx_netdev_remove,
	.driver = {
		.name = KVX_NETDEV_NAME,
		.of_match_table = kvx_netdev_match,
	},
};

static int kvx_eth_phy_fw_update(struct platform_device *pdev)
{
	struct kvx_eth_dev *dev = platform_get_drvdata(pdev);
	const struct firmware *fw;
	int ret = 0;

	if (!load_phy_fw)
		return -EINVAL;

	dev_info(&pdev->dev, "Requesting phy firmware %s\n", KVX_PHY_FW_NAME);
	ret = request_firmware(&fw, KVX_PHY_FW_NAME, &pdev->dev);
	if (ret < 0 || fw->size == 0) {
		dev_err(&pdev->dev, "Unable to load firmware %s\n",
			KVX_PHY_FW_NAME);
		return ret;
	}

	/* Update parameters according to probbed fw informations */
	ret = kvx_phy_fw_update(&dev->hw, fw->data);
	release_firmware(fw);

	return ret;
}

static const char *kvx_eth_res_names[KVX_ETH_NUM_RES] = {
	"phy", "phymac", "mac", "eth" };

static struct kvx_eth_type kvx_haps_data = {
	.phy_init = kvx_eth_haps_phy_init,
	.phy_cfg = kvx_eth_haps_phy_cfg,
};

static struct kvx_eth_type kvx_eth_data = {
	.phy_init = kvx_eth_phy_init,
	.phy_cfg = kvx_eth_phy_cfg,
};

/* kvx_eth_probe() - Probe generic device
 * @pdev: Platform device
 *
 * Return: 0 on success, < 0 on failure
 */
static int kvx_eth_probe(struct platform_device *pdev)
{
	struct kvx_eth_dev *dev;
	struct resource *res = NULL;
	struct kvx_eth_res *hw_res;
	int i, ret = 0;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENODEV;
	platform_set_drvdata(pdev, dev);
	dev->pdev = pdev;
	dev->type = &kvx_eth_data;
	INIT_LIST_HEAD(&dev->list);
	mutex_init(&dev->hw.mac_reset_lock);

	if (of_machine_is_compatible("kalray,haps"))
		dev->type = &kvx_haps_data;

	for (i = 0; i < KVX_ETH_NUM_RES; ++i) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   kvx_eth_res_names[i]);
		if (!res) {
			dev_err(&pdev->dev, "Failed to get resources\n");
			ret = -ENODEV;
			goto err;
		}
		hw_res = &dev->hw.res[i];
		hw_res->name = kvx_eth_res_names[i];
		hw_res->base = devm_ioremap_resource(&pdev->dev, res);
		if (!hw_res->base) {
			dev_err(&pdev->dev, "Failed to map %s reg\n",
				hw_res->name);
			ret = PTR_ERR(hw_res->base);
			goto err;
		}
		dev_dbg(&pdev->dev, "map[%d] %s @ 0x%llx\n", i, hw_res->name,
			 (u64)hw_res->base);
	}

	ret = kvx_eth_dev_parse_dt(pdev, dev);
	if (ret)
		goto err;

	dev->hw.dev = &pdev->dev;

	if (dev->type->phy_init) {
		ret = dev->type->phy_init(&dev->hw, SPEED_UNKNOWN);
		if (ret) {
			dev_err(&pdev->dev, "Mac/Phy init failed (ret: %d)\n",
				ret);
			goto err;
		}
	}

	/* Try loading phy firmware */
	ret = kvx_eth_phy_fw_update(pdev);
	if (ret)
		kvx_phy_reset(&dev->hw);

	kvx_eth_init_dispatch_table(&dev->hw, 0,
			RX_DISPATCH_TABLE_ENTRY_ARRAY_SIZE);
	kvx_eth_tx_init(&dev->hw);
	kvx_eth_parsers_init(&dev->hw);
	kvx_eth_phy_f_init(&dev->hw);
	kvx_eth_hw_sysfs_init(&dev->hw);
	dev_info(&pdev->dev, "KVX network driver\n");
	return devm_of_platform_populate(&pdev->dev);

err:
	platform_set_drvdata(pdev, NULL);
	return ret;
}

/* kvx_eth_remove() - Remove generic device
 * @pdev: Platform device
 *
 * Return: 0
 */
static int kvx_eth_remove(struct platform_device *pdev)
{
	struct kvx_eth_dev *dev = platform_get_drvdata(pdev);
	struct kvx_eth_netdev *ndev;

	list_for_each_entry(ndev, &dev->list, node)
		unregister_netdev(ndev->netdev);

	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id kvx_eth_match[] = {
	{ .compatible = "kalray,kvx-eth" },
	{ },
};
MODULE_DEVICE_TABLE(of, kvx_eth_match);

static struct platform_driver kvx_eth_driver = {
	.probe = kvx_eth_probe,
	.remove = kvx_eth_remove,
	.driver = {
		.name = KVX_NET_DRIVER_NAME,
		.of_match_table = kvx_eth_match
	},
};

static struct platform_driver * const drivers[] = {
	&kvx_netdev_driver,
	&kvx_eth_driver,
};

static int kvx_eth_init(void)
{
	return platform_register_drivers(drivers, ARRAY_SIZE(drivers));
}
module_init(kvx_eth_init);

static void kvx_eth_exit(void)
{
	platform_unregister_drivers(drivers, ARRAY_SIZE(drivers));
}
module_exit(kvx_eth_exit);

/* kvx_eth_get_lut_indir() - Get LUT indirection
 * A LUT entry points to a dispatch entry entry (dt).
 * This dt entry is a route to a cluster_id / rx_channel pair.
 *
 * @netdev: Current netdev
 * @lut_id: LUT entry to request
 * @cluster_id: return value
 * @rx_channel: return value
 * @return: -1 if error, dispatch table entry otherwise
 */
int kvx_eth_get_lut_indir(struct net_device *netdev, u32 lut_id,
		u32 *cluster_id, u32 *rx_channel)
{
	struct kvx_eth_netdev *ndev;

	if (!netdev)
		return -EINVAL;
	ndev = netdev_priv(netdev);
	return kvx_eth_hw_get_lut_indir(ndev->hw, lut_id, cluster_id, rx_channel);
}
EXPORT_SYMBOL(kvx_eth_get_lut_indir);

MODULE_AUTHOR("Thomas Costis <tcostis@kalray.eu>");
MODULE_AUTHOR("Benjamin Mugnier <bmugnier@kalray.eu>");
MODULE_LICENSE("GPL");
