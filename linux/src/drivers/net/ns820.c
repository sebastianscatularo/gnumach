/* ns820.c: A Linux Gigabit Ethernet driver for the NatSemi DP83820 series. */
/*
	Written/copyright 1999-2003 by Donald Becker.
	Copyright 2002-2003 by Scyld Computing Corporation.

	This software may be used and distributed according to the terms of
	the GNU General Public License (GPL), incorporated herein by reference.
	Drivers based on or derived from this code fall under the GPL and must
	retain the authorship, copyright and license notice.  This file is not
	a complete program and may only be used when the entire operating
	system is licensed under the GPL.  License for under other terms may be
	available.  Contact the original author for details.

	The original author may be reached as becker@scyld.com, or at
	Scyld Computing Corporation
	914 Bay Ridge Road, Suite 220
	Annapolis MD 21403

	Support information and updates available at
		http://www.scyld.com/network/natsemi.html
	The information and support mailing lists are based at
		http://www.scyld.com/mailman/listinfo/
*/

/* These identify the driver base version and may not be removed. */
static const char version1[] =
"ns820.c:v1.03a 8/09/2003  Written by Donald Becker <becker@scyld.com>\n";
static const char version2[] =
"  http://www.scyld.com/network/natsemi.html\n";
/* Updated to recommendations in pci-skeleton v2.13. */

/* Automatically extracted configuration info:
probe-func: ns820_probe
config-in: tristate 'National Semiconductor DP8382x series PCI Ethernet support' CONFIG_NATSEMI820

c-help-name: National Semiconductor DP8382x series PCI Ethernet support
c-help-symbol: CONFIG_NATSEMI820
c-help: This driver is for the National Semiconductor DP83820 Gigabit Ethernet
c-help: adapter series.
c-help: More specific information and updates are available from
c-help: http://www.scyld.com/network/natsemi.html
*/

/* The user-configurable values.
   These may be modified when a driver module is loaded.*/

/* Message enable level: 0..31 = no..all messages.  See NETIF_MSG docs. */
static int debug = 2;

/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 20;

/* Maximum number of multicast addresses to filter (vs. rx-all-multicast).
   This chip uses a 2048 element hash table based on the Ethernet CRC.
   Previous natsemi chips had unreliable multicast filter circuitry.
   To work around an observed problem set this value to '0',
   which will immediately switch to Rx-all-multicast.
  */
static int multicast_filter_limit = 100;

/* Set the copy breakpoint for the copy-only-tiny-frames scheme.
   Setting to > 1518 effectively disables this feature.
   This chip can only receive into aligned buffers, so architectures such
   as the Alpha AXP might benefit from a copy-align.
*/
static int rx_copybreak = 0;

/* Used to pass the media type, etc.
   Both 'options[]' and 'full_duplex[]' should exist for driver
   interoperability, however setting full_duplex[] is deprecated.
   The media type is usually passed in 'options[]'.
    The default is autonegotation for speed and duplex.
	This should rarely be overridden.
    Use option values 0x10/0x20 for 10Mbps, 0x100,0x200 for 100Mbps.
    Use option values 0x10 and 0x100 for forcing half duplex fixed speed.
    Use option values 0x20 and 0x200 for forcing full duplex operation.
	Use 0x1000 or 0x2000 for gigabit.
*/
#define MAX_UNITS 8		/* More are supported, limit only on options */
static int options[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int full_duplex[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};

/* Operational parameters that are set at compile time. */

/* Keep the ring sizes a power of two for compile efficiency.
   Understand the implications before changing these settings!
   The compiler will convert <unsigned>'%'<2^N> into a bit mask.
   Making the Tx ring too large decreases the effectiveness of channel
   bonding and packet priority, confuses the system network buffer limits,
   and wastes memory.
   Too-large receive rings waste memory and confound network buffer limits.
*/
#define TX_RING_SIZE	16
#define TX_QUEUE_LEN	10		/* Limit ring entries actually used, min 4. */
#define RX_RING_SIZE	64

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung.
   Re-autonegotiation may take up to 3 seconds.
 */
#define TX_TIMEOUT  (6*HZ)

/* Allocation size of Rx buffers with normal sized Ethernet frames.
   Do not change this value without good reason.  This is not a limit,
   but a way to keep a consistent allocation size among drivers.
 */
#define PKT_BUF_SZ		1536

#ifndef __KERNEL__
#define __KERNEL__
#endif
#if !defined(__OPTIMIZE__)
#warning  You must compile this file with the correct options!
#warning  See the last lines of the source file.
#error You must compile this driver with "-O".
#endif

/* Include files, designed to support most kernel versions 2.0.0 and later. */
#include <linux/config.h>
#if defined(CONFIG_SMP) && ! defined(__SMP__)
#define __SMP__
#endif
#if defined(MODULE) && defined(CONFIG_MODVERSIONS) && ! defined(MODVERSIONS)
#define MODVERSIONS
#endif

#include <linux/version.h>
#if defined(MODVERSIONS)
#include <linux/modversions.h>
#endif
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#if LINUX_VERSION_CODE >= 0x20400
#include <linux/slab.h>
#else
#include <linux/malloc.h>
#endif
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <asm/processor.h>		/* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>

#ifdef INLINE_PCISCAN
#include "k_compat.h"
#else
#include "pci-scan.h"
#include "kern_compat.h"
#endif

#if (LINUX_VERSION_CODE >= 0x20100)  &&  defined(MODULE)
char kernel_version[] = UTS_RELEASE;
#endif

MODULE_AUTHOR("Donald Becker <becker@scyld.com>");
MODULE_DESCRIPTION("National Semiconductor DP83820 series PCI Ethernet driver");
MODULE_LICENSE("GPL");
MODULE_PARM(debug, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(multicast_filter_limit, "i");
MODULE_PARM_DESC(debug, "Driver message level (0-31)");
MODULE_PARM_DESC(options, "Force transceiver type or fixed speed+duplex");
MODULE_PARM_DESC(max_interrupt_work,
				 "Driver maximum events handled per interrupt");
MODULE_PARM_DESC(full_duplex,
				 "Non-zero to force full duplex, non-negotiated link "
				 "(deprecated).");
MODULE_PARM_DESC(rx_copybreak,
				 "Breakpoint in bytes for copy-only-tiny-frames");
MODULE_PARM_DESC(multicast_filter_limit,
				 "Multicast addresses before switching to Rx-all-multicast");

/*
				Theory of Operation

I. Board Compatibility

This driver is designed for National Semiconductor DP83820 10/100/1000
Ethernet NIC.  It is superficially similar to the 810 series "natsemi.c"
driver, however the register layout, descriptor layout and element
length of the new chip series is different.

II. Board-specific settings

This driver requires the PCI interrupt line to be configured.
It honors the EEPROM-set values.

III. Driver operation

IIIa. Ring buffers

This driver uses two statically allocated fixed-size descriptor lists
formed into rings by a branch from the final descriptor to the beginning of
the list.  The ring sizes are set at compile time by RX/TX_RING_SIZE.
The NatSemi design uses a 'next descriptor' pointer that the driver forms
into a list, thus rings can be arbitrarily sized.  Before changing the
ring sizes you should understand the flow and cache effects of the
full/available/empty hysteresis.

IIIb/c. Transmit/Receive Structure

This driver uses a zero-copy receive and transmit scheme.
The driver allocates full frame size skbuffs for the Rx ring buffers at
open() time and passes the skb->data field to the chip as receive data
buffers.  When an incoming frame is less than RX_COPYBREAK bytes long,
a fresh skbuff is allocated and the frame is copied to the new skbuff.
When the incoming frame is larger, the skbuff is passed directly up the
protocol stack.  Buffers consumed this way are replaced by newly allocated
skbuffs in a later phase of receives.

The RX_COPYBREAK value is chosen to trade-off the memory wasted by
using a full-sized skbuff for small frames vs. the copying costs of larger
frames.  New boards are typically used in generously configured machines
and the underfilled buffers have negligible impact compared to the benefit of
a single allocation size, so the default value of zero results in never
copying packets.  When copying is done, the cost is usually mitigated by using
a combined copy/checksum routine.  Copying also preloads the cache, which is
most useful with small frames.

A subtle aspect of the operation is that unaligned buffers are not permitted
by the hardware.  Thus the IP header at offset 14 in an ethernet frame isn't
longword aligned for further processing.  On copies frames are put into the
skbuff at an offset of "+2", 16-byte aligning the IP header.

IIId. Synchronization

The driver runs as two independent, single-threaded flows of control.  One
is the send-packet routine, which enforces single-threaded use by the
dev->tbusy flag.  The other thread is the interrupt handler, which is single
threaded by the hardware and interrupt handling software.

The send packet thread has partial control over the Tx ring and 'dev->tbusy'
flag.  It sets the tbusy flag whenever it's queuing a Tx packet. If the next
queue slot is empty, it clears the tbusy flag when finished otherwise it sets
the 'lp->tx_full' flag.

The interrupt handler has exclusive control over the Rx ring and records stats
from the Tx ring.  After reaping the stats, it marks the Tx queue entry as
empty by incrementing the dirty_tx mark. Iff the 'lp->tx_full' flag is set, it
clears both the tx_full and tbusy flags.

IV. Notes

The NatSemi 820 series PCI gigabit chips are very common on low-cost NICs.
The '821 appears to be the same as '820 chip, only with pins for the upper
32 bits marked "N/C".

IVb. References

http://www.scyld.com/expert/100mbps.html
http://www.scyld.com/expert/NWay.html
The NatSemi dp83820 datasheet is available: search www.natsemi.com

IVc. Errata

None characterised.

*/



static void *ns820_probe1(struct pci_dev *pdev, void *init_dev,
							long ioaddr, int irq, int chip_idx, int find_cnt);
static int power_event(void *dev_instance, int event);
enum chip_capability_flags {FDXActiveLow=1, InvertGbXcvrPwr=2, };
#ifdef USE_IO_OPS
#define PCI_IOTYPE (PCI_USES_MASTER | PCI_USES_IO  | PCI_ADDR0)
#else
#define PCI_IOTYPE (PCI_USES_MASTER | PCI_USES_MEM | PCI_ADDR1)
#endif

static struct pci_id_info pci_id_tbl[] = {
	{ "D-Link DGE-500T (DP83820)",
	  { 0x0022100B, 0xffffffff, 0x49001186, 0xffffffff, },
	  PCI_IOTYPE, 256, FDXActiveLow},
	{"NatSemi DP83820", { 0x0022100B, 0xffffffff },
	 PCI_IOTYPE, 256, 0},
	{0,},						/* 0 terminated list. */
};

struct drv_id_info ns820_drv_id = {
	"ns820", PCI_HOTSWAP, PCI_CLASS_NETWORK_ETHERNET<<8, pci_id_tbl,
	ns820_probe1, power_event };

/* Offsets to the device registers.
   Unlike software-only systems, device drivers interact with complex hardware.
   It's not useful to define symbolic names for every register bit in the
   device.  Please do not change these names without good reason.
*/
enum register_offsets {
	ChipCmd=0x00, ChipConfig=0x04, EECtrl=0x08, PCIBusCfg=0x0C,
	IntrStatus=0x10, IntrMask=0x14, IntrEnable=0x18, IntrHoldoff=0x1C,
	TxRingPtr=0x20, TxRingPtrHi=0x24, TxConfig=0x28,
	RxRingPtr=0x30, RxRingPtrHi=0x34, RxConfig=0x38,
	WOLCmd=0x40, PauseCmd=0x44, RxFilterAddr=0x48, RxFilterData=0x4C,
	BootRomAddr=0x50, BootRomData=0x54, ChipRevReg=0x58,
	StatsCtrl=0x5C, RxPktErrs=0x60, RxMissed=0x68, RxCRCErrs=0x64,
};

/* Bits in ChipCmd. */
enum ChipCmdBits {
	ChipReset=0x100, SoftIntr=0x80, RxReset=0x20, TxReset=0x10,
	RxOff=0x08, RxOn=0x04, TxOff=0x02, TxOn=0x01,
};

/* Bits in ChipConfig. */
enum ChipConfigBits {
	CfgLinkGood=0x80000000, CfgFDX=0x10000000,
	CfgXcrReset=0x0400, CfgXcrOff=0x0200,
};

/* Bits in the interrupt status/mask registers. */
enum intr_status_bits {
	IntrRxDone=0x0001, IntrRxIntr=0x0002, IntrRxErr=0x0004, IntrRxEarly=0x0008,
	IntrRxIdle=0x0010, IntrRxOverrun=0x0020,
	IntrTxDone=0x0040, IntrTxIntr=0x0080, IntrTxErr=0x0100,
	IntrTxIdle=0x0200, IntrTxUnderrun=0x0400,
	StatsMax=0x0800, IntrDrv=0x1000, WOLPkt=0x2000, LinkChange=0x4000,
	RxStatusOverrun=0x10000,
	RxResetDone=0x00200000, TxResetDone=0x00400000,
	IntrPCIErr=0x001E0000,
	IntrNormalSummary=0x0251, IntrAbnormalSummary=0xED20,
};

/* Bits in the RxMode register. */
enum rx_mode_bits {
	AcceptErr=0x20, AcceptRunt=0x10,
	AcceptBroadcast=0xC0000000,
	AcceptMulticast=0x00200000, AcceptAllMulticast=0x20000000,
	AcceptAllPhys=0x10000000, AcceptMyPhys=0x08000000,
};

/* The Rx and Tx buffer descriptors. */
/* Note that using only 32 bit fields simplifies conversion to big-endian
   architectures. */
struct netdev_desc {
#if ADDRLEN == 64
	u64 next_desc;
	u64 buf_addr;
#endif
	u32 next_desc;
	u32 buf_addr;
	s32 cmd_status;
	u32 vlan_status;
};

/* Bits in network_desc.status */
enum desc_status_bits {
	DescOwn=0x80000000, DescMore=0x40000000, DescIntr=0x20000000,
	DescNoCRC=0x10000000,
	DescPktOK=0x08000000, RxTooLong=0x00400000,
};

#define PRIV_ALIGN	15	/* Required alignment mask */
struct netdev_private {
	/* Descriptor rings first for alignment. */
	struct netdev_desc rx_ring[RX_RING_SIZE];
	struct netdev_desc tx_ring[TX_RING_SIZE];
	struct net_device *next_module;		/* Link for devices of this type. */
	void *priv_addr;					/* Unaligned address for kfree */
	const char *product_name;
	/* The addresses of receive-in-place skbuffs. */
	struct sk_buff* rx_skbuff[RX_RING_SIZE];
	/* The saved address of a sent-in-place packet/buffer, for later free(). */
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	struct net_device_stats stats;
	struct timer_list timer;	/* Media monitoring timer. */
	/* Frequently used values: keep some adjacent for cache effect. */
	int msg_level;
	int chip_id, drv_flags;
	struct pci_dev *pci_dev;
	long in_interrupt;			/* Word-long for SMP locks. */
	int max_interrupt_work;
	int intr_enable;
	unsigned int restore_intr_enable:1;	/* Set if temporarily masked.  */
	unsigned int rx_q_empty:1;			/* Set out-of-skbuffs.  */

	struct netdev_desc *rx_head_desc;
	unsigned int cur_rx, dirty_rx;		/* Producer/consumer ring indices */
	unsigned int rx_buf_sz;				/* Based on MTU+slack. */
	int rx_copybreak;

	unsigned int cur_tx, dirty_tx;
	unsigned int tx_full:1;				/* The Tx queue is full. */
	/* These values keep track of the transceiver/media in use. */
	unsigned int full_duplex:1;			/* Full-duplex operation requested. */
	unsigned int duplex_lock:1;
	unsigned int medialock:1;			/* Do not sense media. */
	unsigned int default_port;			/* Last dev->if_port value. */
	/* Rx filter. */
	u32 cur_rx_mode;
	u32 rx_filter[16];
	int multicast_filter_limit;
	/* FIFO and PCI burst thresholds. */
	int tx_config, rx_config;
	/* MII transceiver section. */
	u16 advertising;					/* NWay media advertisement */
};

static int  eeprom_read(long ioaddr, int location);
static void mdio_sync(long mdio_addr);
static int  mdio_read(struct net_device *dev, int phy_id, int location);
static void mdio_write(struct net_device *dev, int phy_id, int location, int value);
static int  netdev_open(struct net_device *dev);
static void check_duplex(struct net_device *dev);
static void netdev_timer(unsigned long data);
static void tx_timeout(struct net_device *dev);
static int  rx_ring_fill(struct net_device *dev);
static void init_ring(struct net_device *dev);
static int  start_tx(struct sk_buff *skb, struct net_device *dev);
static void intr_handler(int irq, void *dev_instance, struct pt_regs *regs);
static void netdev_error(struct net_device *dev, int intr_status);
static int  netdev_rx(struct net_device *dev);
static void netdev_error(struct net_device *dev, int intr_status);
static void set_rx_mode(struct net_device *dev);
static struct net_device_stats *get_stats(struct net_device *dev);
static int mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int  netdev_close(struct net_device *dev);



/* A list of our installed devices, for removing the driver module. */
static struct net_device *root_net_dev = NULL;

#ifndef MODULE
int ns820_probe(struct net_device *dev)
{
	if (pci_drv_register(&ns820_drv_id, dev) < 0)
		return -ENODEV;
	printk(KERN_INFO "%s" KERN_INFO "%s", version1, version2);
	return 0;
}
#endif

static void *ns820_probe1(struct pci_dev *pdev, void *init_dev,
						  long ioaddr, int irq, int chip_idx, int card_idx)
{
	struct net_device *dev;
	struct netdev_private *np;
	void *priv_mem;
	int i, option = card_idx < MAX_UNITS ? options[card_idx] : 0;

	dev = init_etherdev(init_dev, 0);
	if (!dev)
		return NULL;

	/* Perhaps NETIF_MSG_PROBE */
	printk(KERN_INFO "%s: %s at 0x%lx, ",
		   dev->name, pci_id_tbl[chip_idx].name, ioaddr);

	for (i = 0; i < 3; i++)
		((u16 *)dev->dev_addr)[i] = le16_to_cpu(eeprom_read(ioaddr, 12 - i));
	for (i = 0; i < 5; i++)
		printk("%2.2x:", dev->dev_addr[i]);
	printk("%2.2x, IRQ %d.\n", dev->dev_addr[i], irq);

	/* Reset the chip to erase previous misconfiguration. */
	writel(ChipReset, ioaddr + ChipCmd);
	/* Power up Xcvr. */
	writel(~CfgXcrOff & readl(ioaddr + ChipConfig), ioaddr + ChipConfig);

	/* Make certain elements e.g. descriptor lists are aligned. */
	priv_mem = kmalloc(sizeof(*np) + PRIV_ALIGN, GFP_KERNEL);
	/* Check for the very unlikely case of no memory. */
	if (priv_mem == NULL)
		return NULL;

	dev->base_addr = ioaddr;
	dev->irq = irq;

	dev->priv = np = (void *)(((long)priv_mem + PRIV_ALIGN) & ~PRIV_ALIGN);
	memset(np, 0, sizeof(*np));
	np->priv_addr = priv_mem;

	np->next_module = root_net_dev;
	root_net_dev = dev;

	np->pci_dev = pdev;
	np->chip_id = chip_idx;
	np->drv_flags = pci_id_tbl[chip_idx].drv_flags;
	np->msg_level = (1 << debug) - 1;
	np->rx_copybreak = rx_copybreak;
	np->max_interrupt_work = max_interrupt_work;
	np->multicast_filter_limit = multicast_filter_limit;

	if (dev->mem_start)
		option = dev->mem_start;

	/* The lower four bits are the media type. */
	if (option > 0) {
		if (option & 0x220)
			np->full_duplex = 1;
		np->default_port = option & 0x33ff;
		if (np->default_port & 0x330)
			np->medialock = 1;
	}
	if (card_idx < MAX_UNITS  &&  full_duplex[card_idx] > 0)
		np->full_duplex = 1;

	if (np->full_duplex) {
		if (np->msg_level & NETIF_MSG_PROBE)
			printk(KERN_INFO "%s: Set to forced full duplex, autonegotiation"
				   " disabled.\n", dev->name);
		np->duplex_lock = 1;
	}

	/* The chip-specific entries in the device structure. */
	dev->open = &netdev_open;
	dev->hard_start_xmit = &start_tx;
	dev->stop = &netdev_close;
	dev->get_stats = &get_stats;
	dev->set_multicast_list = &set_rx_mode;
	dev->do_ioctl = &mii_ioctl;

	/* Allow forcing the media type. */
	if (option > 0) {
		if (option & 0x220)
			np->full_duplex = 1;
		np->default_port = option & 0x3ff;
		if (np->default_port & 0x330) {
			np->medialock = 1;
			if (np->msg_level & NETIF_MSG_PROBE)
				printk(KERN_INFO "  Forcing %dMbs %s-duplex operation.\n",
					   (option & 0x300 ? 100 : 10),
					   (np->full_duplex ? "full" : "half"));
			mdio_write(dev, 1, 0,
					   ((option & 0x300) ? 0x2000 : 0) | 	/* 100mbps? */
					   (np->full_duplex ? 0x0100 : 0)); /* Full duplex? */
		}
	}

	return dev;
}


/* Read the EEPROM and MII Management Data I/O (MDIO) interfaces.
   The EEPROM code is for the common 93c06/46 EEPROMs with 6 bit addresses.
   Update to the code in other drivers for 8/10 bit addresses.
*/

/* Delay between EEPROM clock transitions.
   This "delay" forces out buffered PCI writes, which is sufficient to meet
   the timing requirements of most EEPROMs.
*/
#define eeprom_delay(ee_addr)	readl(ee_addr)

enum EEPROM_Ctrl_Bits {
	EE_ShiftClk=0x04, EE_DataIn=0x01, EE_ChipSelect=0x08, EE_DataOut=0x02,
};
#define EE_Write0 (EE_ChipSelect)
#define EE_Write1 (EE_ChipSelect | EE_DataIn)

/* The EEPROM commands include the 01 preamble. */
enum EEPROM_Cmds {
	EE_WriteCmd=5, EE_ReadCmd=6, EE_EraseCmd=7,
};

static int eeprom_read(long addr, int location)
{
	long eeprom_addr = addr + EECtrl;
	int read_cmd = (EE_ReadCmd << 6) | location;
	int retval = 0;
	int i;

	writel(EE_Write0, eeprom_addr);

	/* Shift the read command bits out. */
	for (i = 10; i >= 0; i--) {
		int dataval = (read_cmd & (1 << i)) ? EE_Write1 : EE_Write0;
		writel(dataval, eeprom_addr);
		eeprom_delay(eeprom_addr);
		writel(dataval | EE_ShiftClk, eeprom_addr);
		eeprom_delay(eeprom_addr);
	}
	writel(EE_ChipSelect, eeprom_addr);
	eeprom_delay(eeprom_addr);

	for (i = 15; i >= 0; i--) {
		writel(EE_ChipSelect | EE_ShiftClk, eeprom_addr);
		eeprom_delay(eeprom_addr);
		retval |= (readl(eeprom_addr) & EE_DataOut) ? 1 << i : 0;
		writel(EE_ChipSelect, eeprom_addr);
		eeprom_delay(eeprom_addr);
	}

	/* Terminate the EEPROM access. */
	writel(EE_Write0, eeprom_addr);
	writel(0, eeprom_addr);
	return retval;
}

/*  MII transceiver control section.
	Read and write MII registers using software-generated serial MDIO
	protocol.  See the MII specifications or DP83840A data sheet for details.

	The maximum data clock rate is 2.5 Mhz.  To meet minimum timing we
	must flush writes to the PCI bus with a PCI read. */
#define mdio_delay(mdio_addr) readl(mdio_addr)

/* Set iff a MII transceiver on any interface requires mdio preamble.
   This only set with older tranceivers, so the extra
   code size of a per-interface flag is not worthwhile. */
static char mii_preamble_required = 0;

enum mii_reg_bits {
	MDIO_ShiftClk=0x0040, MDIO_Data=0x0010, MDIO_EnbOutput=0x0020,
};
#define MDIO_EnbIn  (0)
#define MDIO_WRITE0 (MDIO_EnbOutput)
#define MDIO_WRITE1 (MDIO_Data | MDIO_EnbOutput)

/* Generate the preamble required for initial synchronization and
   a few older transceivers. */
static void mdio_sync(long mdio_addr)
{
	int bits = 32;

	/* Establish sync by sending at least 32 logic ones. */
	while (--bits >= 0) {
		writel(MDIO_WRITE1, mdio_addr);
		mdio_delay(mdio_addr);
		writel(MDIO_WRITE1 | MDIO_ShiftClk, mdio_addr);
		mdio_delay(mdio_addr);
	}
}

static int mdio_read(struct net_device *dev, int phy_id, int location)
{
	long mdio_addr = dev->base_addr + EECtrl;
	int mii_cmd = (0xf6 << 10) | (phy_id << 5) | location;
	int i, retval = 0;

	if (mii_preamble_required)
		mdio_sync(mdio_addr);

	/* Shift the read command bits out. */
	for (i = 15; i >= 0; i--) {
		int dataval = (mii_cmd & (1 << i)) ? MDIO_WRITE1 : MDIO_WRITE0;

		writel(dataval, mdio_addr);
		mdio_delay(mdio_addr);
		writel(dataval | MDIO_ShiftClk, mdio_addr);
		mdio_delay(mdio_addr);
	}
	/* Read the two transition, 16 data, and wire-idle bits. */
	for (i = 19; i > 0; i--) {
		writel(MDIO_EnbIn, mdio_addr);
		mdio_delay(mdio_addr);
		retval = (retval << 1) | ((readl(mdio_addr) & MDIO_Data) ? 1 : 0);
		writel(MDIO_EnbIn | MDIO_ShiftClk, mdio_addr);
		mdio_delay(mdio_addr);
	}
	return (retval>>1) & 0xffff;
}

static void mdio_write(struct net_device *dev, int phy_id, int location, int value)
{
	long mdio_addr = dev->base_addr + EECtrl;
	int mii_cmd = (0x5002 << 16) | (phy_id << 23) | (location<<18) | value;
	int i;

	if (mii_preamble_required)
		mdio_sync(mdio_addr);

	/* Shift the command bits out. */
	for (i = 31; i >= 0; i--) {
		int dataval = (mii_cmd & (1 << i)) ? MDIO_WRITE1 : MDIO_WRITE0;

		writel(dataval, mdio_addr);
		mdio_delay(mdio_addr);
		writel(dataval | MDIO_ShiftClk, mdio_addr);
		mdio_delay(mdio_addr);
	}
	/* Clear out extra bits. */
	for (i = 2; i > 0; i--) {
		writel(MDIO_EnbIn, mdio_addr);
		mdio_delay(mdio_addr);
		writel(MDIO_EnbIn | MDIO_ShiftClk, mdio_addr);
		mdio_delay(mdio_addr);
	}
	return;
}

static int netdev_open(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int i;
	u32 intr_status = readl(ioaddr + IntrStatus);

	/* We have not yet encountered a case where we need to reset the chip. */

	MOD_INC_USE_COUNT;

	if (request_irq(dev->irq, &intr_handler, SA_SHIRQ, dev->name, dev)) {
		MOD_DEC_USE_COUNT;
		return -EAGAIN;
	}

	/* Power up Xcvr. */
	writel((~CfgXcrOff & readl(ioaddr + ChipConfig)) | 0x00400000,
		   ioaddr + ChipConfig);
	if (np->msg_level & NETIF_MSG_IFUP)
		printk(KERN_DEBUG "%s: netdev_open() irq %d intr_status %8.8x.\n",
			   dev->name, dev->irq, intr_status);

	init_ring(dev);

#if defined(ADDR_64BITS) && defined(__alpha__)
	writel(virt_to_bus(np->rx_ring) >> 32, ioaddr + RxRingPtrHi);
	writel(virt_to_bus(np->tx_ring) >> 32, ioaddr + TxRingPtrHi);
#else
	writel(0, ioaddr + RxRingPtrHi);
	writel(0, ioaddr + TxRingPtrHi);
#endif
	writel(virt_to_bus(np->rx_ring), ioaddr + RxRingPtr);
	writel(virt_to_bus(np->tx_ring), ioaddr + TxRingPtr);

	for (i = 0; i < 6; i += 2) {
		writel(i, ioaddr + RxFilterAddr);
		writel(dev->dev_addr[i] + (dev->dev_addr[i+1] << 8),
			   ioaddr + RxFilterData);
	}

	/* Initialize other registers. */
	/* Configure the PCI bus bursts and FIFO thresholds. */
	/* Configure for standard, in-spec Ethernet. */

	if (np->full_duplex  ||
		((readl(ioaddr + ChipConfig) & CfgFDX) == 0) ^
		((np->drv_flags & FDXActiveLow) != 0)) {
		np->tx_config = 0xD0801002;
		np->rx_config = 0x10000020;
	} else {
		np->tx_config = 0x10801002;
		np->rx_config = 0x0020;
	}
	if (dev->mtu > 1500)
		np->rx_config |= 0x08000000;
	writel(np->tx_config, ioaddr + TxConfig);
	writel(np->rx_config, ioaddr + RxConfig);
	if (np->msg_level & NETIF_MSG_IFUP)
		printk(KERN_DEBUG "%s: Setting TxConfig to %8.8x.\n",
			   dev->name, (int)readl(ioaddr + TxConfig));

	if (dev->if_port == 0)
		dev->if_port = np->default_port;

	np->in_interrupt = 0;

	check_duplex(dev);
	set_rx_mode(dev);
	netif_start_tx_queue(dev);

	/* Enable interrupts by setting the interrupt mask. */
	np->intr_enable = IntrNormalSummary | IntrAbnormalSummary | 0x1f;
	writel(np->intr_enable, ioaddr + IntrMask);
	writel(1, ioaddr + IntrEnable);

	writel(RxOn | TxOn, ioaddr + ChipCmd);
	writel(4, ioaddr + StatsCtrl);					/* Clear Stats */

	if (np->msg_level & NETIF_MSG_IFUP)
		printk(KERN_DEBUG "%s: Done netdev_open(), status: %x.\n",
			   dev->name, (int)readl(ioaddr + ChipCmd));

	/* Set the timer to check for link beat. */
	init_timer(&np->timer);
	np->timer.expires = jiffies + 3*HZ;
	np->timer.data = (unsigned long)dev;
	np->timer.function = &netdev_timer;				/* timer handler */
	add_timer(&np->timer);

	return 0;
}

static void check_duplex(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int duplex;

	if (np->duplex_lock)
		return;
	duplex = readl(ioaddr + ChipConfig) & CfgFDX ? 1 : 0;
	if (np->full_duplex != duplex) {
		np->full_duplex = duplex;
		if (np->msg_level & NETIF_MSG_LINK)
			printk(KERN_INFO "%s: Setting %s-duplex based on negotiated link"
				   " capability.\n", dev->name,
				   duplex ? "full" : "half");
		if (duplex) {
			np->rx_config |= 0x10000000;
			np->tx_config |= 0xC0000000;
		} else {
			np->rx_config &= ~0x10000000;
			np->tx_config &= ~0xC0000000;
		}
		writel(np->tx_config, ioaddr + TxConfig);
		writel(np->rx_config, ioaddr + RxConfig);
		if (np->msg_level & NETIF_MSG_LINK)
			printk(KERN_DEBUG "%s: Setting TxConfig to %8.8x (%8.8x).\n",
				   dev->name, np->tx_config, (int)readl(ioaddr + TxConfig));
	}
}

static void netdev_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int next_tick = 10*HZ;

	if (np->msg_level & NETIF_MSG_TIMER)
		printk(KERN_DEBUG "%s: Driver monitor timer tick, status %8.8x.\n",
			   dev->name, (int)readl(ioaddr + ChipConfig));
	if (np->rx_q_empty) {
		/* Trigger an interrupt to refill. */
		writel(SoftIntr, ioaddr + ChipCmd);
	}
	if (netif_queue_paused(dev)  &&
		np->cur_tx - np->dirty_tx > 1  &&
		(jiffies - dev->trans_start) > TX_TIMEOUT) {
		tx_timeout(dev);
	}
	check_duplex(dev);
	np->timer.expires = jiffies + next_tick;
	add_timer(&np->timer);
}

static void tx_timeout(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;

	printk(KERN_WARNING "%s: Transmit timed out, status %8.8x,"
		   " resetting...\n", dev->name, (int)readl(ioaddr + TxRingPtr));

	if (np->msg_level & NETIF_MSG_TX_ERR) {
		int i;
		printk(KERN_DEBUG "  Rx ring %p: ", np->rx_ring);
		for (i = 0; i < RX_RING_SIZE; i++)
			printk(" %8.8x", (unsigned int)np->rx_ring[i].cmd_status);
		printk("\n"KERN_DEBUG"  Tx ring %p: ", np->tx_ring);
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(" %4.4x", np->tx_ring[i].cmd_status);
		printk("\n");
	}

	/* Perhaps we should reinitialize the hardware here. */
	dev->if_port = 0;
	/* Stop and restart the chip's Tx processes . */

	/* Trigger an immediate transmit demand. */

	dev->trans_start = jiffies;
	np->stats.tx_errors++;
	return;
}

/* Refill the Rx ring buffers, returning non-zero if not full. */
static int rx_ring_fill(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	unsigned int entry;

	for (; np->cur_rx - np->dirty_rx > 0; np->dirty_rx++) {
		entry = np->dirty_rx % RX_RING_SIZE;
		if (np->rx_skbuff[entry] == NULL) {
			struct sk_buff *skb = dev_alloc_skb(np->rx_buf_sz);
			np->rx_skbuff[entry] = skb;
			if (skb == NULL)
				return 1;				/* Better luck next time. */
			skb->dev = dev;			/* Mark as being used by this device. */
			np->rx_ring[entry].buf_addr = virt_to_bus(skb->tail);
		}
		np->rx_ring[entry].cmd_status = cpu_to_le32(DescIntr | np->rx_buf_sz);
	}
	return 0;
}

/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void init_ring(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int i;

	np->tx_full = 0;
	np->cur_rx = np->cur_tx = 0;
	np->dirty_rx = np->dirty_tx = 0;

	/* MAX(PKT_BUF_SZ, dev->mtu + 8); */
	/* I know you _want_ to change this without understanding it.  Don't. */
	np->rx_buf_sz = (dev->mtu <= 1532 ? PKT_BUF_SZ : dev->mtu + 8);
	np->rx_head_desc = &np->rx_ring[0];

	/* Initialize all Rx descriptors. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].next_desc = virt_to_bus(&np->rx_ring[i+1]);
		np->rx_ring[i].cmd_status = cpu_to_le32(DescOwn);
		np->rx_skbuff[i] = 0;
	}
	/* Mark the last entry as wrapping the ring. */
	np->rx_ring[i-1].next_desc = virt_to_bus(&np->rx_ring[0]);

	for (i = 0; i < TX_RING_SIZE; i++) {
		np->tx_skbuff[i] = 0;
		np->tx_ring[i].next_desc = virt_to_bus(&np->tx_ring[i+1]);
		np->tx_ring[i].cmd_status = 0;
	}
	np->tx_ring[i-1].next_desc = virt_to_bus(&np->tx_ring[0]);

	/* Fill in the Rx buffers.
	   Allocation failure just leaves a "negative" np->dirty_rx. */
	np->dirty_rx = (unsigned int)(0 - RX_RING_SIZE);
	rx_ring_fill(dev);

	return;
}

static int start_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	unsigned int entry;

	/* Block a timer-based transmit from overlapping.  This happens when
	   packets are presumed lost, and we use this check the Tx status. */
	if (netif_pause_tx_queue(dev) != 0) {
		/* This watchdog code is redundant with the media monitor timer. */
		if (jiffies - dev->trans_start > TX_TIMEOUT)
			tx_timeout(dev);
		return 1;
	}

	/* Note: Ordering is important here, set the field with the
	   "ownership" bit last, and only then increment cur_tx.
	   No spinlock is needed for either Tx or Rx.
	*/

	/* Calculate the next Tx descriptor entry. */
	entry = np->cur_tx % TX_RING_SIZE;

	np->tx_skbuff[entry] = skb;

	np->tx_ring[entry].buf_addr = virt_to_bus(skb->data);
	np->tx_ring[entry].cmd_status = cpu_to_le32(DescOwn|DescIntr | skb->len);
	np->cur_tx++;

	/* StrongARM: Explicitly cache flush np->tx_ring and skb->data,skb->len. */

	if (np->cur_tx - np->dirty_tx >= TX_QUEUE_LEN - 1) {
		np->tx_full = 1;
		/* Check for a just-cleared queue. */
		if (np->cur_tx - (volatile unsigned int)np->dirty_tx
			< TX_QUEUE_LEN - 4) {
			np->tx_full = 0;
			netif_unpause_tx_queue(dev);
		} else
			netif_stop_tx_queue(dev);
	} else
		netif_unpause_tx_queue(dev);		/* Typical path */
	/* Wake the potentially-idle transmit channel. */
	writel(TxOn, dev->base_addr + ChipCmd);

	dev->trans_start = jiffies;

	if (np->msg_level & NETIF_MSG_TX_QUEUED) {
		printk(KERN_DEBUG "%s: Transmit frame #%d queued in slot %d.\n",
			   dev->name, np->cur_tx, entry);
	}
	return 0;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void intr_handler(int irq, void *dev_instance, struct pt_regs *rgs)
{
	struct net_device *dev = (struct net_device *)dev_instance;
	struct netdev_private *np;
	long ioaddr;
	int boguscnt;

#ifndef final_version			/* Can never occur. */
	if (dev == NULL) {
		printk (KERN_ERR "Netdev interrupt handler(): IRQ %d for unknown "
				"device.\n", irq);
		return;
	}
#endif

	ioaddr = dev->base_addr;
	np = (struct netdev_private *)dev->priv;
	boguscnt = np->max_interrupt_work;

#if defined(__i386__)  &&  LINUX_VERSION_CODE < 0x020300
	/* A lock to prevent simultaneous entry bug on Intel SMP machines. */
	if (test_and_set_bit(0, (void*)&dev->interrupt)) {
		printk(KERN_ERR"%s: SMP simultaneous entry of an interrupt handler.\n",
			   dev->name);
		dev->interrupt = 0;	/* Avoid halting machine. */
		return;
	}
#endif

	do {
		u32 intr_status = readl(ioaddr + IntrStatus);

		if (np->msg_level & NETIF_MSG_INTR)
			printk(KERN_DEBUG "%s: Interrupt, status %8.8x.\n",
				   dev->name, intr_status);

		if (intr_status == 0 || intr_status == 0xffffffff)
			break;

		/* Acknowledge all of the current interrupt sources ASAP.
		   Nominally the read above accomplishes this, but... */
		writel(intr_status & 0x001ffff, ioaddr + IntrStatus);

		if (intr_status & (IntrRxDone | IntrRxIntr)) {
			netdev_rx(dev);
			np->rx_q_empty = rx_ring_fill(dev);
		}

		if (intr_status & (IntrRxIdle | IntrDrv)) {
			unsigned int old_dirty_rx = np->dirty_rx;
			if (rx_ring_fill(dev) == 0)
				np->rx_q_empty = 0;
			/* Restart Rx engine iff we did add a buffer. */
			if (np->dirty_rx != old_dirty_rx)
				writel(RxOn, dev->base_addr + ChipCmd);
		}

		for (; np->cur_tx - np->dirty_tx > 0; np->dirty_tx++) {
			int entry = np->dirty_tx % TX_RING_SIZE;
			if (np->msg_level & NETIF_MSG_INTR)
				printk(KERN_DEBUG "%s: Tx entry %d @%p status %8.8x.\n",
					   dev->name, entry, &np->tx_ring[entry],
					   np->tx_ring[entry].cmd_status);
			if (np->tx_ring[entry].cmd_status & cpu_to_le32(DescOwn))
				break;
			if (np->tx_ring[entry].cmd_status & cpu_to_le32(0x08000000)) {
				if (np->msg_level & NETIF_MSG_TX_DONE)
					printk(KERN_DEBUG "%s: Transmit done, Tx status %8.8x.\n",
						   dev->name, np->tx_ring[entry].cmd_status);
				np->stats.tx_packets++;
#if LINUX_VERSION_CODE > 0x20127
				np->stats.tx_bytes += np->tx_skbuff[entry]->len;
#endif
			} else {			/* Various Tx errors */
				int tx_status = le32_to_cpu(np->tx_ring[entry].cmd_status);
				if (tx_status & 0x04010000) np->stats.tx_aborted_errors++;
				if (tx_status & 0x02000000) np->stats.tx_fifo_errors++;
				if (tx_status & 0x01000000) np->stats.tx_carrier_errors++;
				if (tx_status & 0x00200000) np->stats.tx_window_errors++;
				if (np->msg_level & NETIF_MSG_TX_ERR)
					printk(KERN_DEBUG "%s: Transmit error, Tx status %8.8x.\n",
						   dev->name, tx_status);
				np->stats.tx_errors++;
			}
			/* Free the original skb. */
			dev_free_skb_irq(np->tx_skbuff[entry]);
			np->tx_skbuff[entry] = 0;
		}
		/* Note the 4 slot hysteresis to mark the queue non-full. */
		if (np->tx_full
			&& np->cur_tx - np->dirty_tx < TX_QUEUE_LEN - 4) {
			/* The ring is no longer full, allow new TX entries. */
			np->tx_full = 0;
			netif_resume_tx_queue(dev);
		}

		/* Abnormal error summary/uncommon events handlers. */
		if (intr_status & IntrAbnormalSummary)
			netdev_error(dev, intr_status);

		if (--boguscnt < 0) {
			printk(KERN_WARNING "%s: Too much work at interrupt, "
				   "status=0x%4.4x.\n",
				   dev->name, intr_status);
			np->restore_intr_enable = 1;
			break;
		}
	} while (1);

	if (np->msg_level & NETIF_MSG_INTR)
		printk(KERN_DEBUG "%s: exiting interrupt, status=%#4.4x.\n",
			   dev->name, (int)readl(ioaddr + IntrStatus));

#if defined(__i386__)  &&  LINUX_VERSION_CODE < 0x020300
	clear_bit(0, (void*)&dev->interrupt);
#endif
	return;
}

/* This routine is logically part of the interrupt handler, but separated
   for clarity and better register allocation. */
static int netdev_rx(struct net_device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int entry = np->cur_rx % RX_RING_SIZE;
	int boguscnt = np->dirty_rx + RX_RING_SIZE - np->cur_rx;
	s32 desc_status = le32_to_cpu(np->rx_head_desc->cmd_status);

	/* If the driver owns the next entry it's a new packet. Send it up. */
	while (desc_status < 0) {        /* e.g. & DescOwn */
		if (np->msg_level & NETIF_MSG_RX_STATUS)
			printk(KERN_DEBUG "  In netdev_rx() entry %d status was %8.8x.\n",
				   entry, desc_status);
		if (--boguscnt < 0)
			break;
		if ((desc_status & (DescMore|DescPktOK|RxTooLong)) != DescPktOK) {
			if (desc_status & DescMore) {
				printk(KERN_WARNING "%s: Oversized(?) Ethernet frame spanned "
					   "multiple buffers, entry %#x status %x.\n",
					   dev->name, np->cur_rx, desc_status);
				np->stats.rx_length_errors++;
			} else {
				/* There was a error. */
				if (np->msg_level & NETIF_MSG_RX_ERR)
					printk(KERN_DEBUG "  netdev_rx() Rx error was %8.8x.\n",
						   desc_status);
				np->stats.rx_errors++;
				if (desc_status & 0x06000000) np->stats.rx_over_errors++;
				if (desc_status & 0x00600000) np->stats.rx_length_errors++;
				if (desc_status & 0x00140000) np->stats.rx_frame_errors++;
				if (desc_status & 0x00080000) np->stats.rx_crc_errors++;
			}
		} else {
			struct sk_buff *skb;
			int pkt_len = (desc_status & 0x0fff) - 4;	/* Omit CRC size. */
			/* Check if the packet is long enough to accept without copying
			   to a minimally-sized skbuff. */
			if (pkt_len < np->rx_copybreak
				&& (skb = dev_alloc_skb(pkt_len + 2)) != NULL) {
				skb->dev = dev;
				skb_reserve(skb, 2);	/* 16 byte align the IP header */
#if HAS_IP_COPYSUM
				eth_copy_and_sum(skb, np->rx_skbuff[entry]->tail, pkt_len, 0);
				skb_put(skb, pkt_len);
#else
				memcpy(skb_put(skb, pkt_len), np->rx_skbuff[entry]->tail,
					   pkt_len);
#endif
			} else {
				skb_put(skb = np->rx_skbuff[entry], pkt_len);
				np->rx_skbuff[entry] = NULL;
			}
#ifndef final_version				/* Remove after testing. */
			/* You will want this info for the initial debug. */
			if (np->msg_level & NETIF_MSG_PKTDATA)
				printk(KERN_DEBUG "  Rx data %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:"
					   "%2.2x %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x %2.2x%2.2x "
					   "%d.%d.%d.%d.\n",
					   skb->data[0], skb->data[1], skb->data[2], skb->data[3],
					   skb->data[4], skb->data[5], skb->data[6], skb->data[7],
					   skb->data[8], skb->data[9], skb->data[10],
					   skb->data[11], skb->data[12], skb->data[13],
					   skb->data[14], skb->data[15], skb->data[16],
					   skb->data[17]);
#endif
			skb->protocol = eth_type_trans(skb, dev);
			/* W/ hardware checksum: skb->ip_summed = CHECKSUM_UNNECESSARY; */
			netif_rx(skb);
			dev->last_rx = jiffies;
			np->stats.rx_packets++;
#if LINUX_VERSION_CODE > 0x20127
			np->stats.rx_bytes += pkt_len;
#endif
		}
		entry = (++np->cur_rx) % RX_RING_SIZE;
		np->rx_head_desc = &np->rx_ring[entry];
		desc_status = le32_to_cpu(np->rx_head_desc->cmd_status);
	}

	/* Refill is now done in the main interrupt loop. */
	return 0;
}

static void netdev_error(struct net_device *dev, int intr_status)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;

	if (intr_status & LinkChange) {
		int chip_config = readl(ioaddr + ChipConfig);
		if (np->msg_level & NETIF_MSG_LINK)
			printk(KERN_NOTICE "%s: Link changed: Autonegotiation advertising"
				   " %4.4x  partner %4.4x.\n", dev->name,
				   (int)readl(ioaddr + 0x90), (int)readl(ioaddr + 0x94));
		if (chip_config & CfgLinkGood)
			netif_link_up(dev);
		else
			netif_link_down(dev);
		check_duplex(dev);
	}
	if (intr_status & StatsMax) {
		get_stats(dev);
	}
	if (intr_status & IntrTxUnderrun) {
		/* Increase the Tx threshold, 32 byte units. */
		if ((np->tx_config & 0x3f) < 62)
			np->tx_config += 2;			/* +64 bytes */
		writel(np->tx_config, ioaddr + TxConfig);
	}
	if (intr_status & WOLPkt) {
		int wol_status = readl(ioaddr + WOLCmd);
		printk(KERN_NOTICE "%s: Link wake-up event %8.8x",
			   dev->name, wol_status);
	}
	if (intr_status & (RxStatusOverrun | IntrRxOverrun)) {
		if (np->msg_level & NETIF_MSG_DRV)
			printk(KERN_ERR "%s: Rx overflow! ns820 %8.8x.\n",
				   dev->name, intr_status);
		np->stats.rx_fifo_errors++;
	}
	if (intr_status & ~(LinkChange|StatsMax|RxResetDone|TxResetDone|
						RxStatusOverrun|0xA7ff)) {
		if (np->msg_level & NETIF_MSG_DRV)
			printk(KERN_ERR "%s: Something Wicked happened! ns820 %8.8x.\n",
				   dev->name, intr_status);
	}
	/* Hmmmmm, it's not clear how to recover from PCI faults. */
	if (intr_status & IntrPCIErr) {
		np->stats.tx_fifo_errors++;
		np->stats.rx_fifo_errors++;
	}
}

static struct net_device_stats *get_stats(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int crc_errs = readl(ioaddr + RxCRCErrs);

	if (crc_errs != 0xffffffff) {
		/* We need not lock this segment of code for SMP.
		   There is no atomic-add vulnerability for most CPUs,
		   and statistics are non-critical. */
		/* The chip only need report frame silently dropped. */
		np->stats.rx_crc_errors	+= crc_errs;
		np->stats.rx_missed_errors += readl(ioaddr + RxMissed);
	}

	return &np->stats;
}

/* The little-endian AUTODIN II ethernet CRC calculations.
   A big-endian version is also available.
   This is slow but compact code.  Do not use this routine for bulk data,
   use a table-based routine instead.
   This is common code and should be moved to net/core/crc.c.
   Chips may use the upper or lower CRC bits, and may reverse and/or invert
   them.  Select the endian-ness that results in minimal calculations.
*/
static unsigned const ethernet_polynomial_le = 0xedb88320U;
static inline unsigned ether_crc_le(int length, unsigned char *data)
{
	unsigned int crc = 0xffffffff;	/* Initial value. */
	while(--length >= 0) {
		unsigned char current_octet = *data++;
		int bit;
		for (bit = 8; --bit >= 0; current_octet >>= 1) {
			if ((crc ^ current_octet) & 1) {
				crc >>= 1;
				crc ^= ethernet_polynomial_le;
			} else
				crc >>= 1;
		}
	}
	return crc;
}

static void set_rx_mode(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	u8 mc_filter[64];			/* Multicast hash filter */
	u32 rx_mode;

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		/* Unconditionally log net taps. */
		printk(KERN_NOTICE "%s: Promiscuous mode enabled.\n", dev->name);
		rx_mode = AcceptBroadcast | AcceptAllMulticast | AcceptAllPhys
			| AcceptMyPhys;
	} else if ((dev->mc_count > np->multicast_filter_limit)
			   ||  (dev->flags & IFF_ALLMULTI)) {
		rx_mode = AcceptBroadcast | AcceptAllMulticast | AcceptMyPhys;
	} else {
		struct dev_mc_list *mclist;
		int i;
		memset(mc_filter, 0, sizeof(mc_filter));
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
			 i++, mclist = mclist->next) {
			set_bit(ether_crc_le(ETH_ALEN, mclist->dmi_addr) & 0x7ff,
					mc_filter);
		}
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
		for (i = 0; i < 64; i += 2) {
			writel(rx_mode + 0x200 + i, ioaddr + RxFilterAddr);
			writel((mc_filter[i+1]<<8) + mc_filter[i], ioaddr + RxFilterData);
		}
	}
	writel(rx_mode, ioaddr + RxFilterAddr);
	np->cur_rx_mode = rx_mode;
}

static int mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	u16 *data = (u16 *)&rq->ifr_data;
	u32 *data32 = (void *)&rq->ifr_data;

	switch(cmd) {
	case 0x8947: case 0x89F0:
		/* SIOCGMIIPHY: Get the address of the PHY in use. */
		data[0] = 1;
		/* Fall Through */
	case 0x8948: case 0x89F1:
		/* SIOCGMIIREG: Read the specified MII register. */
		data[3] = mdio_read(dev, data[0] & 0x1f, data[1] & 0x1f);
		return 0;
	case 0x8949: case 0x89F2:
		/* SIOCSMIIREG: Write the specified MII register */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (data[0] == 1) {
			u16 miireg = data[1] & 0x1f;
			u16 value = data[2];
			switch (miireg) {
			case 0:
				/* Check for autonegotiation on or reset. */
				np->duplex_lock = (value & 0x9000) ? 0 : 1;
				if (np->duplex_lock)
					np->full_duplex = (value & 0x0100) ? 1 : 0;
				break;
			case 4: np->advertising = value; break;
			}
		}
		mdio_write(dev, data[0] & 0x1f, data[1] & 0x1f, data[2]);
		return 0;
	case SIOCGPARAMS:
		data32[0] = np->msg_level;
		data32[1] = np->multicast_filter_limit;
		data32[2] = np->max_interrupt_work;
		data32[3] = np->rx_copybreak;
		return 0;
	case SIOCSPARAMS:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		np->msg_level = data32[0];
		np->multicast_filter_limit = data32[1];
		np->max_interrupt_work = data32[2];
		np->rx_copybreak = data32[3];
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int netdev_close(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int i;

	netif_stop_tx_queue(dev);

	if (np->msg_level & NETIF_MSG_IFDOWN) {
		printk(KERN_DEBUG "%s: Shutting down ethercard, status was %4.4x "
			   "Int %2.2x.\n",
			   dev->name, (int)readl(ioaddr + ChipCmd),
			   (int)readl(ioaddr + IntrStatus));
		printk(KERN_DEBUG "%s: Queue pointers were Tx %d / %d,  Rx %d / %d.\n",
			   dev->name, np->cur_tx, np->dirty_tx, np->cur_rx, np->dirty_rx);
	}

	/* We don't want the timer to re-start anything. */
	del_timer(&np->timer);

	/* Disable interrupts using the mask. */
	writel(0, ioaddr + IntrMask);
	writel(0, ioaddr + IntrEnable);
	writel(2, ioaddr + StatsCtrl);					/* Freeze Stats */

	/* Stop the chip's Tx and Rx processes. */
	writel(RxOff | TxOff, ioaddr + ChipCmd);

	get_stats(dev);

#ifdef __i386__
	if (np->msg_level & NETIF_MSG_IFDOWN) {
		printk("\n"KERN_DEBUG"  Tx ring at %8.8x:\n",
			   (int)virt_to_bus(np->tx_ring));
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(" #%d desc. %8.8x %8.8x.\n",
				   i, np->tx_ring[i].cmd_status, (u32)np->tx_ring[i].buf_addr);
		printk("\n"KERN_DEBUG "  Rx ring %8.8x:\n",
			   (int)virt_to_bus(np->rx_ring));
		for (i = 0; i < RX_RING_SIZE; i++) {
			printk(KERN_DEBUG " #%d desc. %8.8x %8.8x\n",
				   i, np->rx_ring[i].cmd_status, (u32)np->rx_ring[i].buf_addr);
		}
	}
#endif /* __i386__ debugging only */

	free_irq(dev->irq, dev);

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].cmd_status = 0;
		np->rx_ring[i].buf_addr = 0xBADF00D0; /* An invalid address. */
		if (np->rx_skbuff[i]) {
#if LINUX_VERSION_CODE < 0x20100
			np->rx_skbuff[i]->free = 1;
#endif
			dev_free_skb(np->rx_skbuff[i]);
		}
		np->rx_skbuff[i] = 0;
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		if (np->tx_skbuff[i])
			dev_free_skb(np->tx_skbuff[i]);
		np->tx_skbuff[i] = 0;
	}

	/* Power down Xcvr. */
	writel(CfgXcrOff | readl(ioaddr + ChipConfig), ioaddr + ChipConfig);

	MOD_DEC_USE_COUNT;

	return 0;
}

static int power_event(void *dev_instance, int event)
{
	struct net_device *dev = dev_instance;
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;

	if (np->msg_level & NETIF_MSG_LINK)
		printk(KERN_DEBUG "%s: Handling power event %d.\n", dev->name, event);
	switch(event) {
	case DRV_ATTACH:
		MOD_INC_USE_COUNT;
		break;
	case DRV_SUSPEND:
		/* Disable interrupts, freeze stats, stop Tx and Rx. */
		writel(0, ioaddr + IntrEnable);
		writel(2, ioaddr + StatsCtrl);
		writel(RxOff | TxOff, ioaddr + ChipCmd);
		writel(CfgXcrOff | readl(ioaddr + ChipConfig), ioaddr + ChipConfig);
		break;
	case DRV_RESUME:
		/* This is incomplete: the open() actions should be repeated. */
		writel(~CfgXcrOff & readl(ioaddr + ChipConfig), ioaddr + ChipConfig);
		set_rx_mode(dev);
		writel(np->intr_enable, ioaddr + IntrEnable);
		writel(1, ioaddr + IntrEnable);
		writel(RxOn | TxOn, ioaddr + ChipCmd);
		break;
	case DRV_DETACH: {
		struct net_device **devp, **next;
		if (dev->flags & IFF_UP) {
			/* Some, but not all, kernel versions close automatically. */
			dev_close(dev);
			dev->flags &= ~(IFF_UP|IFF_RUNNING);
		}
		unregister_netdev(dev);
		release_region(dev->base_addr, pci_id_tbl[np->chip_id].io_size);
		for (devp = &root_net_dev; *devp; devp = next) {
			next = &((struct netdev_private *)(*devp)->priv)->next_module;
			if (*devp == dev) {
				*devp = *next;
				break;
			}
		}
		if (np->priv_addr)
			kfree(np->priv_addr);
		kfree(dev);
		MOD_DEC_USE_COUNT;
		break;
	}
	}

	return 0;
}


#ifdef MODULE
int init_module(void)
{
	/* Emit version even if no cards detected. */
	printk(KERN_INFO "%s" KERN_INFO "%s", version1, version2);
#ifdef CARDBUS
	register_driver(&etherdev_ops);
	return 0;
#else
	return pci_drv_register(&ns820_drv_id, NULL);
#endif
}

void cleanup_module(void)
{
	struct net_device *next_dev;

#ifdef CARDBUS
	unregister_driver(&etherdev_ops);
#else
	pci_drv_unregister(&ns820_drv_id);
#endif

	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	while (root_net_dev) {
		struct netdev_private *np = (void *)(root_net_dev->priv);
		unregister_netdev(root_net_dev);
		iounmap((char *)root_net_dev->base_addr);
		next_dev = np->next_module;
		if (np->priv_addr)
			kfree(np->priv_addr);
		kfree(root_net_dev);
		root_net_dev = next_dev;
	}
}

#endif  /* MODULE */

/*
 * Local variables:
 *  compile-command: "make KERNVER=`uname -r` ns820.o"
 *  compile-cmd: "gcc -DMODULE -Wall -Wstrict-prototypes -O6 -c ns820.c"
 *  simple-compile-command: "gcc -DMODULE -O6 -c ns820.c"
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
