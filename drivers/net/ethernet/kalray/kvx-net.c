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

#include "kvx-net.h"
#include "kvx-net-hw.h"
#include "kvx-net-regs.h"
#include "kvx-net-hdr.h"

static const char *rtm_prop_name[RTM_NB] = {
	[RTM_RX] = "kalray,rtmrx",
	[RTM_TX] = "kalray,rtmtx",
};

#define KVX_RX_HEADROOM  (NET_IP_ALIGN + NET_SKB_PAD)
#define KVX_SKB_PAD	(SKB_DATA_ALIGN(sizeof(struct skb_shared_info) + \
					KVX_RX_HEADROOM))
#define KVX_SKB_SIZE(len)	(SKB_DATA_ALIGN(len) + KVX_SKB_PAD)
#define KVX_MAX_RX_BUF_SIZE	(PAGE_SIZE - KVX_SKB_PAD)

#define KVX_DEV(ndev) container_of(ndev->hw, struct kvx_eth_dev, hw)

static void kvx_eth_alloc_rx_buffers(struct kvx_eth_ring *ring, int count);

/* kvx_eth_desc_unused() - Gets the number of remaining unused buffers in ring
 * @r: Current ring
 *
 * Return: number of usable buffers
 */
static int kvx_eth_desc_unused(struct kvx_eth_ring *r)
{
	if (r->next_to_clean > r->next_to_use)
		return 0;
	return (r->count - (r->next_to_use - r->next_to_clean + 1));
}

static struct netdev_queue *get_txq(const struct kvx_eth_ring *ring)
{
	return netdev_get_tx_queue(ring->netdev, ring->qidx);
}

/* kvx_eth_up() - Interface up
 * @netdev: Current netdev
 */
void kvx_eth_up(struct net_device *netdev)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_eth_ring *r;
	int i;

	for (i = 0; i < ndev->dma_cfg.rx_chan_id.nb; i++) {
		r = &ndev->rx_ring[i];
		kvx_eth_alloc_rx_buffers(r, kvx_eth_desc_unused(r));
		napi_enable(&r->napi);
	}

	netif_tx_start_all_queues(netdev);
	phylink_start(ndev->phylink);
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
void kvx_eth_down(struct net_device *netdev)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	int i;

	netif_tx_stop_all_queues(netdev);
	for (i = 0; i < ndev->dma_cfg.rx_chan_id.nb; i++)
		napi_disable(&ndev->rx_ring[i].napi);

	phylink_stop(ndev->phylink);
}

/* kvx_eth_netdev_close() - Close ops
 * @netdev: Current netdev
 */
static int kvx_eth_netdev_close(struct net_device *netdev)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	kvx_eth_down(netdev);
	phylink_disconnect_phy(ndev->phylink);

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
		(2 * KVX_ETH_HEADER_SIZE) + KVX_ETH_FCS;
	/* Takes into account alignement offsets (footers) */
	ndev->rx_buffer_len = ALIGN(ndev->hw->max_frame_size,
				    KVX_ETH_PKT_ALIGN);

	ndev->cfg.speed = SPEED_UNKNOWN;
	ndev->cfg.duplex = DUPLEX_UNKNOWN;
	ndev->hw->fec_en = 0;
	kvx_eth_mac_f_init(ndev->hw, &ndev->cfg);
	kvx_eth_dt_f_init(ndev->hw, &ndev->cfg);
	kvx_eth_lb_f_init(ndev->hw, &ndev->cfg);
	kvx_eth_pfc_f_init(ndev->hw, &ndev->cfg);

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
	int len, count = 1;
	dma_addr_t handler;

	sg_init_table(tx->sg, MAX_SKB_FRAGS + 1);
	handler = dma_map_single(dev, tx->skb->data,
				 skb_headlen(tx->skb), DMA_TO_DEVICE);
	if (dma_mapping_error(dev, handler))
		goto out_err;
	sg_dma_address(&tx->sg[0]) = handler;
	tx->len = sg_dma_len(&tx->sg[0]) = skb_headlen(tx->skb);

	si = skb_shinfo(tx->skb);
	end = &si->frags[si->nr_frags];
	for (fp = si->frags; fp < end; fp++, count++) {
		handler = skb_frag_dma_map(dev, fp, 0, skb_frag_size(fp),
					   DMA_TO_DEVICE);
		if (dma_mapping_error(dev, handler))
			goto unwind;

		sg_dma_address(&tx->sg[count]) = handler;
		len = skb_frag_size(fp);
		sg_dma_len(&tx->sg[count]) = len;
		tx->len += len;
	}
	sg_mark_end(&tx->sg[count - 1]);
	tx->sg_len = count;
	dev_dbg(dev, "%s tx->len=%d= %d - %d si->nr_frags: %d\n", __func__,
		(int)tx->len, tx->skb->len, tx->skb->data_len, si->nr_frags);
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
	int bytes_completed = 0;
	int pkt_completed = 0;
	int ret = 0;

	tx = &txr->tx_buf[tx_r];
	tx->len = desc_len;
	if (unlikely(!tx->skb)) {
		ret = -EINVAL;
		netdev_err(netdev, "No skb in descriptor\n");
		goto exit;
	}
	netdev_dbg(netdev, "Sent skb: 0x%llx len: %d/%d qidx: %d\n",
		   (u64)tx->skb, (int)tx->len, tx->skb->len, txr->qidx);

	/* consume_skb */
	kvx_eth_unmap_skb(ndev->dev, tx);
	bytes_completed += tx->len;
	++pkt_completed;
	dev_consume_skb_irq(tx->skb);
	tx->skb = NULL;

exit:
	netdev_tx_completed_queue(get_txq(txr), pkt_completed, bytes_completed);
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

static u32 ipaddr_checksum(u8 *ip_addr, int idx)
{
	return ((((u16)ip_addr[2 * idx]) << 8) | ((u16)ip_addr[(2 * idx) + 1]));
}

static u32 align_checksum(u32 cks)
{
	u32 c = cks;

	while (c > 0xffff)
		c = (c >> 16) + (c & 0xffff);
	return c;
}

/* compute_header_checksum() - Compute CRC depending on protocols (debug only)
 * @ndev: Current kvx_eth_netdev
 * @skb: skb to handle
 * @ip_mode: ip version
 * @crc_mode: supported protocols
 *
 * Return: computed crc
 */
u32 compute_header_checksum(struct kvx_eth_netdev *ndev, struct sk_buff *skb,
			    enum tx_ip_mode ip_mode, enum tx_crc_mode crc_mode)
{
	int i = 0;
	u32 cks = 0;
	u8 protocol;
	u8 *src_ip_ptr = 0;
	struct ethhdr *eth_h = eth_hdr(skb);
	struct iphdr *iph = ip_hdr(skb);
	u16 payload_length = skb_tail_pointer(skb) - (unsigned char *)eth_h;

	if (crc_mode != UDP_MODE && crc_mode != TCP_MODE) {
		netdev_err(ndev->netdev, "CRC mode not supported\n");
		return 0;
	}
	protocol = (crc_mode == UDP_MODE ? 0x11 : 0x6);
	if (ip_mode == IP_V4_MODE) {
		src_ip_ptr = (unsigned char *)iph + 12;
		for (i = 0; i < 4; i++)
			cks += ipaddr_checksum(src_ip_ptr, i);
	} else if (ip_mode == IP_V6_MODE) {
		src_ip_ptr = (unsigned char *)iph + 8;
		for (i = 0; i < 16; ++i)
			cks += ipaddr_checksum(src_ip_ptr, i);
	}

	cks += protocol;
	cks += payload_length;
	netdev_dbg(ndev->netdev, "%s proto: 0x%x len: %d src_ip_ptr: 0x%x %x %x %x\n",
		   __func__, protocol, payload_length, src_ip_ptr[0],
		   src_ip_ptr[1], src_ip_ptr[2], src_ip_ptr[3]);

	return align_checksum(cks);
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
					  struct sk_buff *skb, int tx_fifo_id)
{
	union tx_metadata *hdr, h;
	size_t hdr_len = sizeof(h);
	struct ethhdr *eth_h = eth_hdr(skb);
	struct iphdr *iph = ip_hdr(skb);
	int pkt_size = skb->len;
	enum tx_ip_mode ip_mode = NO_IP_MODE;
	enum tx_crc_mode crc_mode = NO_CRC_MODE;
	struct kvx_eth_lane_cfg *cfg = &ndev->cfg;

	memset(&h, 0, hdr_len);
	if (skb_headroom(skb) < hdr_len) {
		struct sk_buff *skb_new = skb_realloc_headroom(skb, hdr_len);

		dev_kfree_skb_any(skb);
		if (!skb_new)
			return NULL;
		skb = skb_new;
	}

	hdr = (union tx_metadata *)skb_push(skb, hdr_len);

	netdev_dbg(ndev->netdev, "%s skb->len: %d pkt_size: %d skb->data: 0x%lx\n",
		   __func__, skb->len, pkt_size, (uintptr_t)skb->data);

	h._.pkt_size = skb->len - sizeof(h);
	h._.lane = cfg->id;
	h._.nocx_en = ndev->hw->tx_f[tx_fifo_id].nocx_en;

	if (eth_h->h_proto == ETH_P_IP)
		ip_mode = IP_V4_MODE;
	else if (eth_h->h_proto == ETH_P_IPV6)
		ip_mode = IP_V6_MODE;

	if (iph) {
		if (iph->protocol == IPPROTO_TCP)
			crc_mode = TCP_MODE;
		else if (iph->protocol == IPPROTO_UDP)
			crc_mode = UDP_MODE;
	}
	if (ip_mode && crc_mode) {
		u32 c = compute_header_checksum(ndev, skb, ip_mode, crc_mode);

		h._.ip_mode  = ip_mode;
		h._.crc_mode = crc_mode;
		h._.index    = (u16)skb->transport_header;
		h._.udp_tcp_cksum = kvx_eth_pseudo_hdr_cks(skb);
		if (c != h._.udp_tcp_cksum)
			netdev_err(ndev->netdev, "CRC FAILS (0x%x != 0x%x)\n",
				c, h._.udp_tcp_cksum);
	}

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

	if (skb->len <= 0) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (kvx_eth_tx_has_header(ndev->hw, txr->config.rx_tag))
		skb = kvx_eth_tx_add_hdr(ndev, skb, txr->config.rx_tag);

	tx->skb = skb;
	tx->len = 0;
	netdev_dbg(netdev, "%s Sending skb: 0x%llx len: %d data_len: %d\n",
		   __func__, (u64)skb, skb->len, skb->data_len);

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
	dma_async_issue_pending(txr->chan);

	netdev_tx_sent_queue(get_txq(txr), skb->len);

	skb_orphan(skb);

	tx_w++;
	txr->next_to_use = (tx_w < txr->count) ? tx_w : 0;

	unused_tx = kvx_eth_desc_unused(txr);
	if (unlikely(unused_tx == 0))
		netif_tx_stop_queue(get_txq(txr));

	skb_tx_timestamp(skb);
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

	while (--unused_desc > KVX_ETH_MIN_RX_BUF_THRESHOLD && count--) {
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
		ret = kvx_dma_enqueue_rx_buffer(dma_cfg->pdev,
					  dma_cfg->rx_chan_id.start + rxr->qidx,
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
	enum dma_data_direction dma_dir;
	void *data, *data_end, *va = NULL;
	struct page *page = NULL;
	struct kvx_qdesc *qdesc = &rxr->pool.qdesc[qdesc_idx];
	size_t data_len;

	page = qdesc->va;
	if (KVX_SKB_SIZE(len) > PAGE_SIZE) {
		netdev_err(netdev, "Rx buffer exceeds PAGE_SIZE\n");
		return -ENOBUFS;
	}
	dma_dir = page_pool_get_dma_dir(rxr->pool.pagepool);
	dma_sync_single_for_cpu(ndev->dev, buf, len, dma_dir);
	data_len = len - ETH_FCS_LEN;
	va = page_address(page);
	/* Prefetch header */
	prefetch(va);
	data = va + KVX_RX_HEADROOM;
	data_end = data + data_len;

	rxr->skb = build_skb(va, KVX_SKB_SIZE(len));
	if (unlikely(!rxr->skb)) {
		rxr->stats.skb_alloc_err++;
		return -ENOBUFS;
	}

	/* Release descriptor */
	page_pool_release_page(rxr->pool.pagepool, page);
	qdesc->va = NULL;
	qdesc->dma_addr = 0;

	skb_reserve(rxr->skb, data - va);
	skb_put(rxr->skb, data_end - data);

	kvx_eth_rx_hdr(ndev, rxr->skb);
	rxr->skb->protocol = eth_type_trans(rxr->skb, netdev);

	return 0;
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
	int chan_id = dma_cfg->rx_chan_id.start + rxr->qidx;
	struct kvx_dma_pkt_full_desc pkt;
	u32 rx_r = rxr->next_to_clean;
	int work_done = 0;
	int rx_count = 0;
	int ret = 0;

	while (!kvx_dma_get_rx_completed(dma_cfg->pdev, chan_id, &pkt)) {
		work_done++;
		rx_count++;

		ret = kvx_eth_rx_frame(rxr, rx_r, (dma_addr_t)pkt.base,
				       (size_t)pkt.byte, pkt.notif);
		/* Still some RX segments pending */
		if (likely(!ret && pkt.notif)) {
			napi_gro_receive(napi, rxr->skb);
			rxr->skb = NULL;
		}

		if (unlikely(rx_count > KVX_ETH_MIN_RX_WRITE)) {
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
	if (rx_count > KVX_ETH_MIN_RX_WRITE)
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

	if (work_done < budget) {
		if (!napi_complete_done(napi, work_done))
			napi_reschedule(napi);
	}

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
	int max_frame_len = new_mtu + (2 * KVX_ETH_HEADER_SIZE) + KVX_ETH_FCS;

	if (netif_running(netdev))
		kvx_eth_down(netdev);

	ndev->rx_buffer_len = ALIGN(max_frame_len, KVX_ETH_PKT_ALIGN);
	ndev->hw->max_frame_size = max_frame_len;
	netdev->mtu = new_mtu;

	kvx_eth_hw_change_mtu(ndev->hw, ndev->cfg.id, max_frame_len);
	if (netif_running(netdev))
		kvx_eth_up(netdev);

	return 0;
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

	stats->rx_packets = ndev->stats.rx.etherstatspkts;
	stats->tx_packets = ndev->stats.tx.framestransmittedok;
	stats->rx_bytes = ndev->stats.rx.etherstatsoctets;
	stats->tx_bytes = ndev->stats.tx.etherstatsoctets;
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
kvx_eth_get_phys_port_name(struct net_device *dev,
						   char *name, size_t len)
{
	struct kvx_eth_netdev *ndev = netdev_priv(dev);
	int n;

	n = snprintf(name, len, "eth%d", ndev->hw->eth_id);

	if (n >= len)
		return -EINVAL;

	return 0;
}

static const struct net_device_ops kvx_eth_netdev_ops = {
	.ndo_open               = kvx_eth_netdev_open,
	.ndo_stop               = kvx_eth_netdev_close,
	.ndo_start_xmit         = kvx_eth_netdev_start_xmit,
	.ndo_get_stats64        = kvx_eth_netdev_get_stats64,
	.ndo_validate_addr      = eth_validate_addr,
	.ndo_set_mac_address    = kvx_eth_set_mac_addr,
	.ndo_change_mtu         = kvx_eth_change_mtu,
	.ndo_get_phys_port_name = kvx_eth_get_phys_port_name,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller    = kvx_eth_netdev_poll_controller,
#endif
};

static void kvx_eth_dma_irq_rx(void *data)
{
	struct kvx_eth_ring *ring = data;

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
	page_pool_destroy(r->pool.pagepool);
	kfree(r->pool.qdesc);
}

int kvx_eth_alloc_rx_ring(struct kvx_eth_netdev *ndev, struct kvx_eth_ring *r)
{
	struct kvx_dma_config *dma_cfg = &ndev->dma_cfg;
	int ret = 0;

	r->count = KVX_ETH_RX_BUF_NB;
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
		memset(&r->config, 0, sizeof(r->config));
		/* All RX queues share the same rx_cache */
		ret = kvx_dma_reserve_rx_chan(dma_cfg->pdev,
					dma_cfg->rx_chan_id.start + r->qidx,
					dma_cfg->rx_cache_id,
					kvx_eth_dma_irq_rx, r);
		if (ret)
			goto chan_failed;
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
 * @r: Rx ring to be release
 * @keep_dma_chan: do not release dma channel
 */
void kvx_eth_release_rx_ring(struct kvx_eth_ring *r, int keep_dma_chan)
{
	struct kvx_eth_netdev *ndev = netdev_priv(r->netdev);
	struct kvx_dma_config *dma_cfg = &ndev->dma_cfg;

	netif_napi_del(&r->napi);
	if (!keep_dma_chan)
		kvx_dma_release_rx_chan(dma_cfg->pdev, r->qidx +
					dma_cfg->rx_chan_id.start);
	kvx_eth_release_rx_pool(r);
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
		r->count = KVX_ETH_TX_BUF_NB;
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
	r->config.rx_tag = ndev->dma_cfg.tx_chan_id.start + r->qidx;
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

	for (qidx = 0; qidx < ndev->dma_cfg.tx_chan_id.nb; qidx++) {
		r = &ndev->tx_ring[qidx];
		r->qidx = qidx;
		tx_f = &ndev->hw->tx_f[ndev->dma_cfg.tx_chan_id.start + qidx];
		tx_f->lane_id = ndev->cfg.id;
		list_add_tail(&tx_f->node, &ndev->cfg.tx_fifo_list);

		ret = kvx_eth_alloc_tx_ring(ndev, r);
		if (ret) {
			list_del_init(&tx_f->node);
			goto alloc_failed;
		}
	}

	return 0;

alloc_failed:
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
	if (txq->start + txq->nb > TX_FIFO_NB) {
		dev_err(&pdev->dev, "TX channels (%d) limited by TX fifo number (%d)\n",
			txq->start + txq->nb, TX_FIFO_NB);
		return -EINVAL;
	}

	if (of_property_read_u32_array(np, "kalray,dma-rx-channel-ids",
				       (u32 *)rxq, 2)) {
		dev_err(&pdev->dev, "Unable to get dma-rx-channel-ids\n");
		return -EINVAL;
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

/* kvx_eth_parse_dt() - Parse device tree inputs
 *
 * Sets dma properties accordingly (dma_mem and iommu nodes)
 *
 * @pdev: platform device
 * @ndev: Current kvx_eth_netdev
 * Return: 0 on success, < 0 on failure
 */
int kvx_eth_parse_dt(struct platform_device *pdev, struct kvx_eth_netdev *ndev)
{
	struct kvx_dma_config *dma_cfg = &ndev->dma_cfg;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *np_dma;
	struct device_node *rtm_node;
	int ret = 0;
	int rtm;

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

	for (rtm = 0; rtm < RTM_NB; rtm++) {
		rtm_node = of_parse_phandle(pdev->dev.of_node,
				rtm_prop_name[rtm], 0);
		if (rtm_node) {
			ndev->rtm[rtm] = of_find_i2c_device_by_node(rtm_node);
			if (!ndev->rtm[rtm])
				return -EPROBE_DEFER;
		}
	}

	/* Default tx eq. parameter tuning */
	ndev->cfg.phy_param.swing = 40;
	if (of_property_read_u32_array(np, "kalray,phy-param",
				   (u32 *)&ndev->cfg.phy_param, 5) != 0)
		dev_info(ndev->dev, "No kalray,phy-param set\n");

	return 0;
}

static void kvx_phylink_validate(struct phylink_config *cfg,
			 unsigned long *supported,
			 struct phylink_link_state *state)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };

	phylink_set(mask, Autoneg);
	phylink_set_port_modes(mask);
	phylink_set(mask, Pause);
	phylink_set(mask, Asym_Pause);

	phylink_set(mask, 100000baseKR4_Full);
	phylink_set(mask, 100000baseCR4_Full);
	phylink_set(mask, 40000baseKR4_Full);
	phylink_set(mask, 40000baseCR4_Full);
	phylink_set(mask, 40000baseSR4_Full);
	phylink_set(mask, 40000baseLR4_Full);

	bitmap_and(supported, supported, mask, __ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_and(state->advertising, state->advertising, mask,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static void kvx_phylink_mac_link_state(struct phylink_config *cfg,
				      struct phylink_link_state *state)
{
	struct net_device *netdev = to_net_dev(cfg->dev);
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	int ret = 0;

	netdev_dbg(netdev, "%s\n", __func__);

	ret = kvx_eth_mac_status(ndev->hw, &ndev->cfg);
	state->link = ndev->cfg.link;
}


static int configure_rtm(struct kvx_eth_netdev *ndev, unsigned int rtm,
		unsigned int speed)
{
	int first_lane = ndev->hw->eth_id * KVX_ETH_LANE_NB;
	int last_lane = first_lane + KVX_ETH_LANE_NB;
	int i;

	if (ndev->rtm[rtm]) {
		netdev_info(ndev->netdev, "Setting retimer%d speed to %d\n",
				rtm, ndev->cfg.speed);

		switch (speed) {
		case SPEED_100000:
			/* Set all lanes */
			for (i = first_lane; i < last_lane; i++)
				ti_retimer_set_speed(ndev->rtm[rtm], i,
						SPEED_25000);
			break;
		case SPEED_40000:
			/* Set all lanes */
			for (i = first_lane; i < last_lane; i++)
				ti_retimer_set_speed(ndev->rtm[rtm], i,
						SPEED_10000);
			break;
		case SPEED_50000:
			/* Set two lanes */
			ti_retimer_set_speed(ndev->rtm[rtm], first_lane,
					SPEED_25000);
			ti_retimer_set_speed(ndev->rtm[rtm], first_lane + 1,
					SPEED_25000);
			break;
		case SPEED_25000:
		case SPEED_10000:
			/* Set only one lane */
			ti_retimer_set_speed(ndev->rtm[rtm], first_lane, speed);
			break;
		default:
			netdev_err(ndev->netdev, "Unsupported speed %d\n",
					speed);
			return -EINVAL;
		}
	}

	return 0;
}

static void kvx_phylink_mac_config(struct phylink_config *cfg,
				   unsigned int mode,
				   const struct phylink_link_state *state)
{
	struct net_device *netdev = to_net_dev(cfg->dev);
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_eth_dev *dev = container_of(ndev->hw,
					       struct kvx_eth_dev, hw);
	bool update_serdes = false;
	int i, ret = 0;

	netdev_dbg(netdev, "%s netdev: %p hw: %p\n", __func__,
		   netdev, ndev->hw);

	/* Prevent kvx_eth_phy_serdes_init being called again */
	if (ndev->cfg.speed != state->speed ||
	    ndev->cfg.duplex != state->duplex)
		update_serdes = true;

	if (state->speed != SPEED_UNKNOWN)
		ndev->cfg.speed = state->speed;
	if (state->duplex != DUPLEX_UNKNOWN)
		ndev->cfg.duplex = state->duplex;

	if (update_serdes) {
		ret = kvx_eth_phy_serdes_init(ndev->hw, &ndev->cfg);
		if (ret) {
			netdev_err(ndev->netdev, "Failed to initialize serdes\n");
			return;
		}
	}

	for (i = 0; i < RTM_NB; i++) {
		ret = configure_rtm(ndev, i, ndev->cfg.speed);
		if (ret) {
			netdev_err(netdev, "Failed to configure retimer %i\n",
					i);
			return;
		}
	}
	/* Setup PHY + serdes */
	if (dev->type->phy_cfg) {
		ret = dev->type->phy_cfg(ndev->hw, &ndev->cfg);
		if (ret)
			netdev_err(netdev, "Failed to configure PHY/MAC\n");
	}

	if (!ret) {
		ret = kvx_eth_mac_cfg(ndev->hw, &ndev->cfg);
		if (ret)
			netdev_err(netdev, "Failed to configure MAC\n");
	}
}

static void kvx_phylink_mac_an_restart(struct phylink_config *cfg)
{
	pr_debug("%s\n", __func__);
}

static void kvx_phylink_mac_link_down(struct phylink_config *cfg,
				      unsigned int mode,
				      phy_interface_t interface)
{
	pr_debug("%s\n", __func__);
}

static void kvx_phylink_mac_link_up(struct phylink_config *cfg,
				    unsigned int mode,
				    phy_interface_t interface,
				    struct phy_device *phy)
{
	pr_debug("%s\n", __func__);
}

const static struct phylink_mac_ops kvx_phylink_ops = {
	.validate          = kvx_phylink_validate,
	.mac_pcs_get_state = kvx_phylink_mac_link_state,
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
	struct list_head *n;
	int ret, i = 0;
	int phy_mode;

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
	ndev->dev = &pdev->dev;
	ndev->netdev = netdev;
	ndev->hw = &dev->hw;
	ndev->cfg.hw = ndev->hw;
	ndev->phylink_cfg.dev = &netdev->dev;
	ndev->phylink_cfg.type = PHYLINK_NETDEV;
	INIT_LIST_HEAD(&ndev->cfg.tx_fifo_list);

	phy_mode = fwnode_get_phy_mode(pdev->dev.fwnode);
	if (phy_mode < 0) {
		dev_err(&pdev->dev, "phy mode not set\n");
		return NULL;
	}

	ret = kvx_eth_parse_dt(pdev, ndev);
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

	ret = phylink_of_phy_connect(ndev->phylink, pdev->dev.of_node, 0);
	if (ret) {
		netdev_err(netdev, "Unable to get phy (%i)\n", ret);
		goto phylink_err;
	}

	eth_hw_addr_random(netdev);
	memcpy(ndev->cfg.mac_f.addr, netdev->dev_addr, ETH_ALEN);
	list_for_each(n, &dev->list)
		i++;
	ndev->cfg.id = i;

	/* Allocate RX/TX rings */
	ret = kvx_eth_alloc_rx_res(netdev);
	if (ret)
		goto exit;

	ret = kvx_eth_alloc_tx_res(netdev);
	if (ret)
		goto tx_chan_failed;

	kvx_set_ethtool_ops(netdev);
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
phylink_err:
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
	list_del(&ndev->node);
	kvx_eth_release_tx_res(ndev->netdev, 0);
	kvx_eth_release_rx_res(ndev->netdev, 0);
	phylink_destroy(ndev->phylink);
	unregister_netdev(ndev->netdev);
	free_netdev(ndev->netdev);
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

	kvx_mac_set_addr(&dev->hw, &ndev->cfg);
	kvx_eth_lb_set_default(&dev->hw, &ndev->cfg);
	kvx_eth_pfc_f_set_default(&dev->hw, &ndev->cfg);
	kvx_eth_lb_set_default(&dev->hw, &ndev->cfg);
	kvx_eth_fill_dispatch_table(&dev->hw, &ndev->cfg,
				    ndev->dma_cfg.rx_chan_id.start);
	kvx_eth_tx_fifo_cfg(&dev->hw, &ndev->cfg);
	kvx_eth_lb_f_cfg(&dev->hw, &ndev->hw->lb_f[ndev->cfg.id]);

	ret = kvx_eth_sysfs_init(ndev);
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

	kvx_eth_sysfs_remove(ndev);
	for (rtm = 0; rtm < RTM_NB; rtm++) {
		if (ndev->rtm[rtm])
			put_device(&ndev->rtm[rtm]->dev);
	}
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

module_platform_driver(kvx_netdev_driver);

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

	if (of_property_read_u32(pdev->dev.of_node, "cell-index",
				 &dev->hw.eth_id)) {
		dev_warn(&pdev->dev, "Default kvx ethernet index to 0\n");
		dev->hw.eth_id = KVX_ETH0;
	}
	dev->hw.dev = &pdev->dev;

	if (dev->type->phy_init) {
		ret = dev->type->phy_init(&dev->hw);
		if (ret) {
			dev_err(&pdev->dev, "Mac/Phy init failed (ret: %d)\n",
				ret);
			goto err;
		}
	}

	kvx_eth_tx_init(&dev->hw);
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

module_platform_driver(kvx_eth_driver);

MODULE_AUTHOR("Kalray");
MODULE_LICENSE("GPL");
