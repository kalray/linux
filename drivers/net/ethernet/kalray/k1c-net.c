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
#include <linux/dma/k1c-dma-api.h>

#include "k1c-net.h"
#include "k1c-net-hw.h"
#include "k1c-net-regs.h"
#include "k1c-net-hdr.h"

static void k1c_eth_alloc_rx_buffers(struct k1c_eth_netdev *ndev, int count);

/* k1c_eth_desc_unused() - Gets the number of remaining unused buffers in ring
 * @r: Current ring
 *
 * Return: number of usable buffers
 */
static int k1c_eth_desc_unused(struct k1c_eth_ring *r)
{
	if (r->next_to_clean > r->next_to_use)
		return 0;
	return (r->count - (r->next_to_use - r->next_to_clean + 1));
}

/* k1c_eth_up() - Interface up
 * @netdev: Current netdev
 */
void k1c_eth_up(struct net_device *netdev)
{
	struct k1c_eth_netdev *ndev = netdev_priv(netdev);

	k1c_eth_alloc_rx_buffers(ndev, k1c_eth_desc_unused(&ndev->rx_ring));

	napi_enable(&ndev->napi);
	netif_start_queue(netdev);

	netif_carrier_on(netdev);
}

/* k1c_eth_link_change() - Link change callback
 * @netdev: Current netdev
 */
static void k1c_eth_link_change(struct net_device *netdev)
{
	struct k1c_eth_netdev *ndev = netdev_priv(netdev);
	struct phy_device *phydev = netdev->phydev;

	if (phydev->link != ndev->cfg.link ||
	    phydev->speed != ndev->cfg.speed ||
	    phydev->duplex != ndev->cfg.duplex) {
		ndev->cfg.link = phydev->link;
		ndev->cfg.speed = phydev->speed;
		ndev->cfg.duplex = phydev->duplex;
		phy_print_status(phydev);
	}
}

/* k1c_eth_netdev_open() - Open ops
 * @netdev: Current netdev
 */
static int k1c_eth_netdev_open(struct net_device *netdev)
{
	struct k1c_eth_netdev *ndev = netdev_priv(netdev);

	phy_start(ndev->phy);

	k1c_eth_up(netdev);
	return 0;
}

/* k1c_eth_down() - Interface down
 * @netdev: Current netdev
 */
void k1c_eth_down(struct net_device *netdev)
{
	struct k1c_eth_netdev *ndev = netdev_priv(netdev);

	netif_carrier_off(netdev);
	napi_disable(&ndev->napi);
	netif_stop_queue(netdev);
}

/* k1c_eth_netdev_close() - Close ops
 * @netdev: Current netdev
 */
static int k1c_eth_netdev_close(struct net_device *netdev)
{
	k1c_eth_down(netdev);

	if (netdev->phydev) {
		phy_stop(netdev->phydev);
		phy_disconnect(netdev->phydev);
	}

	return 0;
}

/* k1c_eth_init_netdev() - Init netdev generic settings
 * @ndev: Current k1c_eth_netdev
 *
 * Return: 0 - OK
 */
static int k1c_eth_init_netdev(struct k1c_eth_netdev *ndev)
{
	ndev->hw->max_frame_size = ndev->netdev->mtu +
		(2 * K1C_ETH_HEADER_SIZE) + K1C_ETH_FCS;
	/* Takes into account alignement offsets (footers) */
	ndev->rx_buffer_len = ALIGN(ndev->hw->max_frame_size,
				    K1C_ETH_PKT_ALIGN);

	ndev->cfg.speed = SPEED_1000;
	ndev->cfg.duplex = DUPLEX_FULL;

	return 0;
}

/* k1c_eth_unmap_skb() - Unmap skb
 * @dev: Current device (with dma settings)
 * @tx: Tx ring descriptor
 */
static void k1c_eth_unmap_skb(struct device *dev,
			      const struct k1c_eth_netdev_tx *tx)
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

/* k1c_eth_unmap_skb() - Map skb (build sg with corresponding IOVA)
 * @dev: Current device (with dma settings)
 * @tx: Tx ring descriptor
 *
 * Return: 0 on success, -ENOMEM on error.
 */
static int k1c_eth_map_skb(struct device *dev, struct k1c_eth_netdev_tx *tx)
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

/* k1c_eth_clean_tx_irq() - Clears completed tx skb
 * @ndev: Current k1c_eth_netdev
 *
 * Return: 0 on success
 */
static int k1c_eth_clean_tx_irq(struct k1c_eth_netdev *ndev)
{
	struct k1c_eth_ring *txr = &ndev->tx_ring;
	struct device *dev = ndev->dev;
	u32 tx_r = txr->next_to_clean;
	struct k1c_eth_netdev_tx *tx;
	int bytes_completed = 0;
	int pkt_completed = 0;

	tx = &txr->tx_buf[tx_r];

	/* napi_consume_skb */
	k1c_eth_unmap_skb(dev, tx);
	if (tx->skb) {
		bytes_completed += tx->len;
		++pkt_completed;
		dev_consume_skb_irq(tx->skb);
		tx->skb = NULL;
	}

	++tx_r;
	if (tx_r == txr->count)
		tx_r = 0;

	txr->next_to_clean = tx_r;
	netdev_completed_queue(ndev->netdev, pkt_completed, bytes_completed);

	if (netif_carrier_ok(ndev->netdev) && netif_queue_stopped(ndev->netdev))
		if (netif_carrier_ok(ndev->netdev) &&
		    (k1c_eth_desc_unused(txr) > (MAX_SKB_FRAGS + 1)))
			netif_wake_queue(ndev->netdev);

	return 0;
}

/* k1c_eth_netdev_dma_callback_tx() - tx completion callback
 * @param: Callback dma_noc parameters
 */
static void k1c_eth_netdev_dma_callback_tx(void *param)
{
	struct k1c_callback_param *p = param;
	struct k1c_eth_netdev_tx *tx = p->cb_param;
	struct k1c_eth_netdev *ndev = tx->ndev;

	tx->len = p->len;
	if (tx->skb)
		netdev_dbg(ndev->netdev, "%s Sent skb: 0x%llx len: %d\n",
			   __func__, (u64)tx->skb, tx->skb->len);
	k1c_eth_clean_tx_irq(tx->ndev);
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
 * @ndev: Current k1c_eth_netdev
 * @skb: skb to handle
 * @ip_mode: ip version
 * @crc_mode: supported protocols
 *
 * Return: computed crc
 */
u32 compute_header_checksum(struct k1c_eth_netdev *ndev, struct sk_buff *skb,
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

/* k1c_eth_pseudo_hdr_cks() - Compute pseudo CRC on skb
 * @skb: skb to handle
 *
 * Return: computed crc
 */
static u16 k1c_eth_pseudo_hdr_cks(struct sk_buff *skb)
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

/* k1c_eth_tx_add_hdr() - Adds tx header (fill correpsonding metadata)
 * @ndev: Current k1c_eth_netdev
 * @skb: skb to handle
 *
 * Return: skb on success, NULL on error.
 */
static struct sk_buff *k1c_eth_tx_add_hdr(struct k1c_eth_netdev *ndev,
					  struct sk_buff *skb)
{
	union tx_metadata *hdr, h;
	size_t hdr_len = sizeof(h);
	struct ethhdr *eth_h = eth_hdr(skb);
	struct iphdr *iph = ip_hdr(skb);
	int pkt_size = skb->len;
	enum tx_ip_mode ip_mode = NO_IP_MODE;
	enum tx_crc_mode crc_mode = NO_CRC_MODE;

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
	k1c_eth_tx_status(ndev->hw, &ndev->cfg);

	h._.pkt_size = skb->len - sizeof(h);
	h._.lane = ndev->cfg.id;
	h._.nocx_en = ndev->cfg.tx_f.nocx_en;

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
		h._.udp_tcp_cksum = k1c_eth_pseudo_hdr_cks(skb);
		if (c != h._.udp_tcp_cksum)
			netdev_err(ndev->netdev, "CRC FAILS (0x%x != 0x%x)\n",
				c, h._.udp_tcp_cksum);
	}

	put_unaligned(h.dword[0], &hdr->dword[0]);
	put_unaligned(h.dword[1], &hdr->dword[1]);

	return skb;
}

/* k1c_eth_netdev_start_xmit() - xmit ops
 * @skb: skb to handle
 * @netdev: Current netdev
 *
 * Return: transmit status
 */
static netdev_tx_t k1c_eth_netdev_start_xmit(struct sk_buff *skb,
					     struct net_device *netdev)
{
	struct k1c_eth_netdev *ndev = netdev_priv(netdev);
	struct device *dev = ndev->dev;
	struct dma_async_tx_descriptor *txd;
	struct k1c_eth_ring *txr = &ndev->tx_ring;
	u32 tx_w = txr->next_to_use;
	struct k1c_eth_netdev_tx *tx = &txr->tx_buf[tx_w];

	netif_trans_update(netdev);

	if (skb->len <= 0) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (k1c_eth_tx_has_header(ndev->hw, &ndev->cfg))
		skb = k1c_eth_tx_add_hdr(ndev, skb);

	tx->skb = skb;
	tx->len = 0;
	netdev_dbg(netdev, "%s Sending skb: 0x%llx len: %d data_len: %d\n",
		   __func__, (u64)skb, skb->len, skb->data_len);

	/* prepare sg */
	if (k1c_eth_map_skb(dev, tx)) {
		net_err_ratelimited("tx[%d]: Map skb failed\n", tx_w);
		goto busy;
	}
	txd = dmaengine_prep_slave_sg(txr->chan, tx->sg, tx->sg_len,
				      DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT);
	if (!txd) {
		netdev_err(netdev, "Failed to get dma desc tx[%d]:\n", tx_w);
		k1c_eth_unmap_skb(dev, tx);
		tx->skb = NULL;
		goto busy;
	}

	txd->callback = k1c_eth_netdev_dma_callback_tx;
	tx->cb_p.cb_param = tx;
	txd->callback_param = &tx->cb_p;

	skb_orphan(skb);

	/* submit and issue descriptor */
	tx->cookie = dmaengine_submit(txd);
	dma_async_issue_pending(txr->chan);

	netdev_sent_queue(netdev, skb->len);

	tx_w++;
	txr->next_to_use = (tx_w < txr->count) ? tx_w : 0;

	skb_tx_timestamp(skb);
	return NETDEV_TX_OK;

busy:
	return NETDEV_TX_BUSY;
}

/* k1c_eth_alloc_rx_buffers() - Allocation rx buffers
 * @ndev: Current k1c_eth_netdev
 * @count: number of buffers to allocate
 */
static void k1c_eth_alloc_rx_buffers(struct k1c_eth_netdev *ndev, int count)
{
	struct net_device *netdev = ndev->netdev;
	struct device *dev = ndev->dev;
	struct k1c_eth_ring *rxr = &ndev->rx_ring;
	struct k1c_dma_config *dma_cfg = &ndev->dma_cfg;
	u32 rx_w = rxr->next_to_use;
	struct sk_buff *skb;
	u32 unused_desc = k1c_eth_desc_unused(rxr);
	struct k1c_eth_netdev_rx *rx = &rxr->rx_buf[rx_w];
	size_t dma_len = 0;
	int ret = 0;

	while (--unused_desc > K1C_ETH_MIN_RX_BUF_THRESHOLD && count--) {
		skb = rx->skb;
		if (skb) { /* Reuse existing skb */
			skb_trim(skb, 0);
		} else {
			skb = netdev_alloc_skb_ip_align(netdev,
							ndev->rx_buffer_len);
			if (unlikely(!skb)) /* retry next time */
				break;

			rx->skb = skb;
			netdev_dbg(netdev, "Alloc rx skb[%d]: 0x%llx\n",
				   rx_w, (u64)skb);
		}

		rx->len = 0;
		sg_set_buf(rx->sg, rx->skb->data, ndev->rx_buffer_len);
		dma_len = dma_map_sg(dev, &rx->sg[0], 1, DMA_FROM_DEVICE);
		ret = dma_mapping_error(dev, sg_dma_address(&rx->sg[0]));
		if (dma_len == 0 || ret) {
			netdev_err(netdev, "Failed to map dma rx[%d]: %d\n",
				   rx_w, ret);
			break;
		}

		ret = k1c_dma_enqueue_rx_buffer(dma_cfg->pdev,
					  dma_cfg->rx_chan_id.start,
					  sg_dma_address(&rx->sg[0]),
					  ndev->rx_buffer_len);
		if (ret) {
			netdev_err(netdev, "Failed to enqueue buffer in rx chan[%d]: %d\n",
				   dma_cfg->rx_chan_id.start, ret);
			dma_unmap_sg(dev, &rx->sg[0], 1, DMA_FROM_DEVICE);
			break;
		}

		++rx_w;
		if (rx_w == rxr->count)
			rx_w = 0;
		rx = &rxr->rx_buf[rx_w];
	}

	if (likely(rxr->next_to_use != rx_w))
		rxr->next_to_use = rx_w;
}

/* k1c_eth_clean_rx_irq() - Clears received RX buffers
 *
 * Called from napi poll:
 *  - handles RX metadata
 *  - RX buffer re-allocation if needed
 * @ndev: Current k1c_eth_netdev
 * @work_done: returned nb of buffers completed
 * @work_left: napi budget
 *
 * Return: 0 on success
 */
static int k1c_eth_clean_rx_irq(struct k1c_eth_netdev *ndev,
				int *work_done, int work_left)
{
	struct k1c_dma_config *dma_cfg = &ndev->dma_cfg;
	struct k1c_eth_ring *rxr = &ndev->rx_ring;
	struct net_device *netdev = ndev->netdev;
	struct device *dev = ndev->dev;
	struct k1c_dma_pkt_full_desc pkt;
	u32 rx_r = rxr->next_to_clean;
	struct k1c_eth_netdev_rx *rx;
	struct rx_metadata *hdr = NULL;
	size_t hdr_size = sizeof(*hdr);
	struct sk_buff *skb;
	int rx_count = 0;

	*work_done = 0;
	while (!k1c_dma_get_rx_completed(dma_cfg->pdev,
					 dma_cfg->rx_chan_id.start, &pkt)) {
		if (*work_done >= work_left)
			break;

		rx = &rxr->rx_buf[rx_r];
		if (pkt.base != sg_dma_address(&rx->sg[0])) {
			netdev_err(netdev, "%s pkt.base 0x%llx != rx->sg[0] 0x%llx pkt.byte: %lld skb data: 0x%llx\n",
			   __func__,  pkt.base, sg_dma_address(&rx->sg[0]),
			   pkt.byte, (u64)rx->skb->data);
			break;
		}

		rx->len = pkt.byte;
		(*work_done)++;
		skb = rx->skb;
		rx->skb = NULL;

		prefetch(skb->data - NET_IP_ALIGN);
		dma_unmap_sg(dev, rx->sg, 1, DMA_FROM_DEVICE);
		++rx_count;
		skb->ip_summed = CHECKSUM_NONE;
		skb_put(skb, rx->len);
		if (k1c_eth_lb_has_header(ndev->hw, &ndev->cfg)) {
			netdev_dbg(netdev, "%s header rx (skb->len: %d data_len: %d)\n",
				   __func__, skb->len, skb->data_len);
			hdr = (struct rx_metadata *)skb->data;
			skb_pull(skb, hdr_size);
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		}
		if (k1c_eth_lb_has_footer(ndev->hw, &ndev->cfg)) {
			netdev_dbg(netdev, "%s footer rx (skb->len: %d data_len: %d)\n",
				   __func__, skb->len, skb->data_len);
			hdr = (struct rx_metadata *)(skb_tail_pointer(skb) -
						     hdr_size);
			k1c_eth_dump_rx_hdr(ndev->hw, hdr);
			skb_trim(skb, skb->len - hdr_size);
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		}

		skb->protocol = eth_type_trans(skb, netdev);
		netdev_dbg(netdev, "%s skb: 0x%llx protocol: 0x%x len: %d/%d data_len:%d\n",
			    __func__, (u64)skb, skb->protocol,
			    (int)rx->len, skb->len, skb->data_len);
		napi_gro_receive(&ndev->napi, skb);
		if (unlikely(rx_count >= K1C_ETH_MIN_RX_WRITE)) {
			k1c_eth_alloc_rx_buffers(ndev, rx_count);
			rx_count = 0;
		}
		++rx_r;
		rx_r = (rx_r < rxr->count) ? rx_r : 0;
	}
	rxr->next_to_clean = rx_r;
	rx_count = k1c_eth_desc_unused(rxr);
	if (rx_count)
		k1c_eth_alloc_rx_buffers(ndev, rx_count);

	return 0;
}

/* k1c_eth_netdev_poll() - NAPI polling callback
 * @napi: NAPI pointer
 * @budget: internal budget def
 *
 * Return: Number of buffers completed
 */
static int k1c_eth_netdev_poll(struct napi_struct *napi, int budget)
{
	struct k1c_eth_netdev *ndev = container_of(napi,
						   struct k1c_eth_netdev, napi);
	struct k1c_dma_config *dma_cfg = &ndev->dma_cfg;
	int work_done = 0;

	k1c_dma_disable_irq(dma_cfg->pdev, dma_cfg->rx_chan_id.start);
	k1c_eth_clean_rx_irq(ndev, &work_done, budget);

	if (work_done < budget) {
		napi_complete_done(napi, work_done);
		k1c_dma_enable_irq(dma_cfg->pdev, dma_cfg->rx_chan_id.start);
	}

	return work_done;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void k1c_eth_netdev_poll_controller(struct net_device *netdev)
{
	struct k1c_eth_netdev *ndev = netdev_priv(netdev);

	napi_schedule(&ndev->napi);
}
#endif

/* k1c_eth_set_mac_addr() - Sets HW address
 * @netdev: Current netdev
 * @p: HW addr
 *
 * Return: 0 on success, -EADDRNOTAVAIL if mac addr NOK
 */
static int k1c_eth_set_mac_addr(struct net_device *netdev, void *p)
{
	struct k1c_eth_netdev *ndev = netdev_priv(netdev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	memcpy(ndev->cfg.mac_f.addr, addr->sa_data, netdev->addr_len);

	k1c_mac_set_addr(ndev->hw, &ndev->cfg);

	return 0;
}

/* k1c_eth_change_mtu() - Change the Maximum Transfer Unit
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Return: 0 on success
 */
static int k1c_eth_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct k1c_eth_netdev *ndev = netdev_priv(netdev);
	int max_frame_len = new_mtu + (2 * K1C_ETH_HEADER_SIZE) + K1C_ETH_FCS;

	if (netif_running(netdev))
		k1c_eth_down(netdev);

	ndev->rx_buffer_len = ALIGN(max_frame_len, K1C_ETH_PKT_ALIGN);
	ndev->hw->max_frame_size = max_frame_len;
	netdev->mtu = new_mtu;

	k1c_eth_hw_change_mtu(ndev->hw, ndev->cfg.id, max_frame_len);
	if (netif_running(netdev))
		k1c_eth_up(netdev);

	return 0;
}

/* k1c_eth_netdev_get_stats64() - Update stats
 * @netdev: Current netdev
 * @stats: Statistic struct
 */
static void k1c_eth_netdev_get_stats64(struct net_device *netdev,
				       struct rtnl_link_stats64 *stats)
{
	struct k1c_eth_netdev *ndev = netdev_priv(netdev);

	k1c_eth_update_stats64(ndev->hw, ndev->cfg.id, &ndev->stats);

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

static const struct net_device_ops k1c_eth_netdev_ops = {
	.ndo_open               = k1c_eth_netdev_open,
	.ndo_stop               = k1c_eth_netdev_close,
	.ndo_start_xmit         = k1c_eth_netdev_start_xmit,
	.ndo_get_stats64        = k1c_eth_netdev_get_stats64,
	.ndo_validate_addr      = eth_validate_addr,
	.ndo_set_mac_address    = k1c_eth_set_mac_addr,
	.ndo_change_mtu         = k1c_eth_change_mtu,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller    = k1c_eth_netdev_poll_controller,
#endif
};

static void k1c_eth_dma_irq_rx(void *data)
{
	struct k1c_eth_ring *ring = data;
	struct k1c_eth_netdev *ndev;

	ndev = netdev_priv(ring->netdev);

	napi_schedule(&ndev->napi);
}

/* k1c_eth_alloc_rx_res() - Allocate RX resources
 * @netdev: Current netdev
 *
 * Return: 0 on success, < 0 on failure
 */
int k1c_eth_alloc_rx_res(struct net_device *netdev)
{
	int i, ret = 0;
	struct k1c_eth_netdev *ndev = netdev_priv(netdev);
	struct k1c_eth_ring *ring = &ndev->rx_ring;
	struct k1c_dma_config *dma_cfg = &ndev->dma_cfg;

	ring->netdev = ndev->netdev;
	ring->next_to_use = 0;
	ring->next_to_clean = 0;
	ring->count = K1C_ETH_MAX_RX_BUF;
	ring->rx_buf = kcalloc(ring->count, sizeof(*ring->rx_buf), GFP_KERNEL);
	if (!ring->rx_buf) {
		netdev_err(netdev, "RX ring allocation failed\n");
		ret = -ENOMEM;
		goto exit;
	}

	for (i = 0; i < ring->count; ++i) {
		sg_init_table(ring->rx_buf[i].sg, 1);
		ring->rx_buf[i].ndev = ndev;
	}
	memset(&ring->config, 0, sizeof(ring->config));
	ret = k1c_dma_reserve_rx_chan(dma_cfg->pdev, dma_cfg->rx_chan_id.start,
				      dma_cfg->rx_cache_id, k1c_eth_dma_irq_rx,
				      ring);
	if (ret != 0)
		goto chan_failed;

	return 0;

chan_failed:
	kfree(ring->rx_buf);
	ring->rx_buf = NULL;
exit:
	return ret;
}

/* k1c_eth_release_rx_res() - Release RX resources
 * @netdev: Current netdev
 */
void k1c_eth_release_rx_res(struct net_device *netdev)
{
	struct k1c_eth_netdev *ndev = netdev_priv(netdev);
	struct k1c_dma_config *dma_cfg = &ndev->dma_cfg;
	struct k1c_eth_ring *ring = &ndev->rx_ring;

	k1c_dma_release_rx_chan(dma_cfg->pdev, dma_cfg->rx_chan_id.start);
	kfree(ring->rx_buf);
	ring->rx_buf = NULL;
}

/* k1c_eth_alloc_tx_res() - Allocate TX resources (including dma_noc channel)
 * @netdev: Current netdev
 *
 * Return: 0 on success, < 0 on failure
 */
int k1c_eth_alloc_tx_res(struct net_device *netdev)
{
	int i, ret = 0;
	struct k1c_eth_netdev *ndev = netdev_priv(netdev);
	struct k1c_eth_ring *ring = &ndev->tx_ring;

	ring->netdev = ndev->netdev;
	ring->next_to_use = 0;
	ring->next_to_clean = 0;
	ring->count = K1C_ETH_MAX_TX_BUF;
	ring->tx_buf = kcalloc(ring->count, sizeof(*ring->tx_buf), GFP_KERNEL);
	if (!ring->tx_buf) {
		netdev_err(netdev, "TX ring allocation failed\n");
		ret = -ENOMEM;
		goto exit;
	}
	for (i = 0; i < ring->count; ++i) {
		/* initialize scatterlist to the maximum size */
		sg_init_table(ring->tx_buf[i].sg, MAX_SKB_FRAGS + 1);
		ring->tx_buf[i].ndev = ndev;
	}
	memset(&ring->config, 0, sizeof(ring->config));
	ring->config.cfg.direction = DMA_MEM_TO_DEV;
	ring->config.trans_type = K1C_DMA_TYPE_MEM2ETH;
	ring->config.dir = K1C_DMA_DIR_TYPE_TX;
	ring->config.noc_route = noc_route_c2eth(K1C_ETH0, k1c_cluster_id());
	ring->config.qos_id = 0;

	ring->chan = of_dma_request_slave_channel(ndev->dev->of_node, "tx");
	if (!ring->chan) {
		netdev_err(netdev, "Request dma TX chan failed\n");
		ret = -EINVAL;
		goto chan_failed;
	}
	/* Config dma */
	ret = dmaengine_slave_config(ring->chan, &ring->config.cfg);
	if (ret)
		goto config_failed;

	return 0;

config_failed:
	dma_release_channel(ring->chan);
chan_failed:
	kfree(ring->tx_buf);
	ring->tx_buf = NULL;
exit:
	return ret;
}

/* k1c_eth_release_tx_res() - Release TX resources
 * @netdev: Current netdev
 */
void k1c_eth_release_tx_res(struct net_device *netdev)
{
	struct k1c_eth_netdev *ndev = netdev_priv(netdev);
	struct k1c_eth_ring *ring = &ndev->tx_ring;

	dma_release_channel(ring->chan);
	kfree(ring->tx_buf);
	ring->tx_buf = NULL;
}

/* k1c_eth_parse_dt() - Parse device tree inputs
 *
 * Sets dma properties accordingly (dma_mem and iommu nodes)
 *
 * @pdev: platform device
 * @ndev: Current k1c_eth_netdev
 * Return: 0 on success, < 0 on failure
 */
int k1c_eth_parse_dt(struct platform_device *pdev, struct k1c_eth_netdev *ndev)
{
	struct k1c_dma_config *dma_cfg = &ndev->dma_cfg;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *np_dma;
	int ret = 0;

	np_dma = of_parse_phandle(np, "dmas", 0);
	if (!np_dma) {
		dev_err(&pdev->dev, "Failed to get dma\n");
		return -EINVAL;
	}
	dma_cfg->pdev = of_find_device_by_node(np_dma);
	if (!dma_cfg->pdev) {
		dev_err(&pdev->dev, "Failed to dma_noc platform_device\n");
		return -EINVAL;
	}

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
	if (of_property_read_u32_array(np, "kalray,dma-rx-channel-ids",
			       (u32 *)&dma_cfg->rx_chan_id, 2) != 0) {
		dev_err(ndev->dev, "Unable to get dma-rx-channel-ids\n");
		return -EINVAL;
	}
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

	ndev->phy = of_phy_get_and_connect(ndev->netdev, np,
					   k1c_eth_link_change);
	if (!ndev->phy) {
		dev_err(ndev->dev, "Unable to get phy\n");
		return -EINVAL;
	}

	return 0;
}

/* k1c_eth_create_netdev() - Create new netdev
 * @pdev: Platform device
 * @dev: parent device
 * @cfg: configuration (duplicated to k1c_eth_netdev)
 *
 * Return: new k1c_eth_netdev on success, NULL on failure
 */
static struct k1c_eth_netdev*
k1c_eth_create_netdev(struct platform_device *pdev, struct k1c_eth_dev *dev)
{
	struct k1c_eth_netdev *ndev;
	struct net_device *netdev;
	int ret, i = 0;
	struct list_head *n;

	netdev = alloc_etherdev(sizeof(struct k1c_eth_netdev));
	if (!netdev) {
		dev_err(&pdev->dev, "Failed to alloc netdev\n");
		return NULL;
	}
	SET_NETDEV_DEV(netdev, &pdev->dev);
	ndev = netdev_priv(netdev);
	memset(ndev, 0, sizeof(*ndev));
	netdev->netdev_ops = &k1c_eth_netdev_ops;
	netdev->mtu = ETH_DATA_LEN;
	ndev->dev = &pdev->dev;
	ndev->netdev = netdev;
	ndev->hw = &dev->hw;
	ndev->cfg.hw = ndev->hw;

	ret = k1c_eth_parse_dt(pdev, ndev);
	if (ret)
		return NULL;

	netif_napi_add(netdev, &ndev->napi,
		       k1c_eth_netdev_poll, NAPI_POLL_WEIGHT);
	eth_hw_addr_random(netdev);
	memcpy(ndev->cfg.mac_f.addr, netdev->dev_addr, ETH_ALEN);
	/* As of now keep tx_fifo = lane_id -> needs to be updated */
	ndev->cfg.tx_fifo = ndev->cfg.id % TX_FIFO_NB;

	/* Allocate RX/TX rings */
	ret = k1c_eth_alloc_rx_res(netdev);
	if (ret)
		goto exit;

	ret = k1c_eth_alloc_tx_res(netdev);
	if (ret)
		goto tx_chan_failed;

	k1c_set_ethtool_ops(netdev);
	/* Register the network device */
	ret = register_netdev(netdev);
	if (ret) {
		netdev_err(netdev, "Failed to register netdev %d\n", ret);
		ret = -ENODEV;
		goto err;
	}

	list_for_each(n, &dev->list)
		i++;
	ndev->cfg.id = i;
	/* Populate list of netdev */
	INIT_LIST_HEAD(&ndev->node);
	list_add(&ndev->node, &dev->list);

	return ndev;

err:
	k1c_eth_release_tx_res(netdev);
tx_chan_failed:
	k1c_eth_release_rx_res(netdev);
exit:
	netdev_err(netdev, "Failed to create netdev\n");
	netif_napi_del(&ndev->napi);
	return NULL;
}

/* k1c_eth_free_netdev() - Releases netdev
 * @ndev: Current k1c_eth_netdev
 *
 * Return: 0
 */
static int k1c_eth_free_netdev(struct k1c_eth_netdev *ndev)
{
	list_del(&ndev->node);
	unregister_netdev(ndev->netdev);
	netif_napi_del(&ndev->napi);
	k1c_eth_release_tx_res(ndev->netdev);
	k1c_eth_release_rx_res(ndev->netdev);
	free_netdev(ndev->netdev);
	return 0;
}

/* k1c_netdev_probe() - Probe netdev
 * @pdev: Platform device
 *
 * Return: 0 on success, < 0 on failure
 */
static int k1c_netdev_probe(struct platform_device *pdev)
{
	struct device_node *np_dev = of_get_parent(pdev->dev.of_node);
	struct platform_device *ppdev = of_find_device_by_node(np_dev);
	struct k1c_eth_netdev *ndev = NULL;
	struct k1c_eth_dev *dev;
	int ret = 0;

	dev = platform_get_drvdata(ppdev);
	/* Config DMA */
	dmaengine_get();

	ndev = k1c_eth_create_netdev(pdev, dev);
	if (!ndev) {
		ret = -ENODEV;
		goto err;
	}

	platform_set_drvdata(pdev, ndev);
	ret = k1c_eth_init_netdev(ndev);
	if (ret)
		goto err;

	ret = k1c_eth_mac_cfg(&dev->hw, &ndev->cfg);
	if (ret) {
		dev_err(&pdev->dev, "Failed to init MAC\n");
		goto err;
	}

	k1c_mac_set_addr(&dev->hw, &ndev->cfg);
	k1c_eth_tx_set_default(&ndev->cfg);
	k1c_eth_lb_set_default(&dev->hw, &ndev->cfg);
	k1c_eth_pfc_f_set_default(&dev->hw, &ndev->cfg);
	k1c_eth_lb_f_cfg(&dev->hw, &ndev->cfg);
	k1c_eth_fill_dispatch_table(&dev->hw, &ndev->cfg,
				    ndev->dma_cfg.rx_chan_id.start);
	k1c_eth_tx_f_cfg(&dev->hw, &ndev->cfg);

	ret = k1c_eth_sysfs_init(ndev);
	if (ret)
		netdev_warn(ndev->netdev, "Failed to initialize sysfs\n");

	dev_err(&pdev->dev, "K1C netdev[%d] probed\n", ndev->cfg.id);

	return 0;

err:
	if (ndev)
		k1c_eth_free_netdev(ndev);
	dmaengine_put();
	return ret;
}

/* k1c_netdev_remove() - Remove netdev
 * @pdev: Platform device
 *
 * Return: 0
 */
static int k1c_netdev_remove(struct platform_device *pdev)
{
	struct k1c_eth_netdev *ndev = platform_get_drvdata(pdev);

	k1c_eth_sysfs_remove(ndev);
	k1c_eth_free_netdev(ndev);
	dmaengine_put();

	return 0;
}

static const struct of_device_id k1c_netdev_match[] = {
	{ .compatible = "kalray,k1c-net" },
	{ }
};
MODULE_DEVICE_TABLE(of, k1c_netdev_match);

static struct platform_driver k1c_netdev_driver = {
	.probe = k1c_netdev_probe,
	.remove = k1c_netdev_remove,
	.driver = {
		.name = K1C_NETDEV_NAME,
		.of_match_table = k1c_netdev_match,
	},
};

module_platform_driver(k1c_netdev_driver);

static const char *k1c_eth_res_names[K1C_ETH_NUM_RES] = {"phy", "mac", "eth"};

/* k1c_eth_probe() - Prove generic device
 * @pdev: Platform device
 *
 * Return: 0 on success, < 0 on failure
 */
static int k1c_eth_probe(struct platform_device *pdev)
{
	struct k1c_eth_dev *dev;
	struct resource *res = NULL;
	struct k1c_eth_res *hw_res;
	int i, ret = 0;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENODEV;
	platform_set_drvdata(pdev, dev);
	dev->pdev = pdev;
	INIT_LIST_HEAD(&dev->list);

	for (i = 0; i < K1C_ETH_NUM_RES; ++i) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   k1c_eth_res_names[i]);
		if (!res) {
			dev_err(&pdev->dev, "Failed to get resources\n");
			ret = -ENODEV;
			goto err;
		}
		hw_res = &dev->hw.res[i];
		hw_res->name = k1c_eth_res_names[i];
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

	dev->hw.dev = &pdev->dev;

	ret = k1c_eth_mac_reset(&dev->hw);
	if (ret)
		goto err;

	dev_info(&pdev->dev, "K1C network driver\n");
	return devm_of_platform_populate(&pdev->dev);

err:
	platform_set_drvdata(pdev, NULL);
	return ret;
}

/* k1c_eth_remove() - Remove generic device
 * @pdev: Platform device
 *
 * Return: 0
 */

static int k1c_eth_remove(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id k1c_eth_match[] = {
	{ .compatible = "kalray,k1c-eth" },
	{ }
};
MODULE_DEVICE_TABLE(of, k1c_eth_match);

static struct platform_driver k1c_eth_driver = {
	.probe = k1c_eth_probe,
	.remove = k1c_eth_remove,
	.driver = {
		.name = K1C_NET_DRIVER_NAME,
		.of_match_table = k1c_eth_match
	},
};

module_platform_driver(k1c_eth_driver);

MODULE_AUTHOR("Kalray");
MODULE_LICENSE("GPL");
