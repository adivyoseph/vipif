#include <linux/netdevice.h>
#include <linux/printk.h>
#include <linux/smp.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/byteorder/generic.h>

#include "vipif.h"
#include "igb.h"

/*
virtual IP interfcae (netdev) lower 
Each instance is bound to an actual (physical) netdev device which has vipif_lower hooked into it. 
On downlink, namespace transmit, the instance may be called on any Core, per Core synchronization 
is required in the vipif_lower as the master netdev transmit may already be invoked by another 
vipif_upper instance. 
vipif_upper uplink is called directly from vipif_lower, on the same Core, so no  synchronization 
is required. 
 
per Core stats are maintained in each  vipif_upper context structure to simplify tracing and access 
 
*/


/*
ARP table is per master netdev 
just search out to each context for an ip address match 
*/


void vipif_uppersend_map_entry(struct net_device *master_netdev, u8 *perm_addr, int instance, int cpu){

    struct igb_adapter *adapter = netdev_priv(master_netdev);
    t_vipif_map_entry   *map_entry;

    if (instance >= VIPIF_PER_MASTER_MAX_CTX) {
        //todo add debug msg
        return;
    }
    map_entry = &adapter->vipif_map[instance];
    spin_lock(&map_entry->spinlock);
    memcpy(map_entry->mac_addr, perm_addr, 6);
    spin_unlock(&map_entry->spinlock);

    printk("KIWI %d:vipif_uppersend_map_entry instance %d %02x:%02x:%02x:%02x:%02x:%02x\n",
           cpu,
           instance,
           perm_addr[0], perm_addr[1], perm_addr[2],perm_addr[3],perm_addr[4],perm_addr[5]);

}

typedef struct arp_req_pkt{
    unsigned char dest_mac[6];
    unsigned char src_mac[6];
    unsigned char eth_type[2];
    struct arphdr arp_hdr;
	unsigned char		ar_sha[ETH_ALEN];	/* sender hardware address	*/
	unsigned char		ar_sip[4];		/* sender IP address		*/
	unsigned char		ar_tha[ETH_ALEN];	/* target hardware address	*/
	__u32       		ar_tip;		/* target IP address		*/
} t_arp_req_pkt;

int vipif_arp(struct net_device * master_netdev, struct sk_buff *skb){
    struct igb_adapter *adapter = netdev_priv(master_netdev);        //contains ingress mapper table
    t_vipif_map_entry  *map_entry;
    struct net_device  *netdev_ctx;
    t_vipif_ctx        *vipif_ctx;
    int i;
    unsigned char *pdata =skb->data;
    pdata = pdata -14;
    t_arp_req_pkt *arp_pkt;
    arp_pkt = (t_arp_req_pkt *)pdata;

    printk("KIWI %d:vipif_arp opcode 0x%x\n", get_cpu(), ntohs(arp_pkt->arp_hdr.ar_op));
    if (ntohs(arp_pkt->arp_hdr.ar_op) == ARPOP_REQUEST) {
        if (arp_pkt->dest_mac[0] == 0xff) {
            printk("KIWI FF looking for %0x\n",ntohl(arp_pkt->ar_tip));
        }
        else {
            printk("KIWI not FF looking for %0x\n",ntohl(arp_pkt->ar_tip));
        }

       for (i = 0; i < VIPIF_PER_MASTER_MAX_CTX; i++) {
           netdev_ctx = adapter->vipif_map[i].vipif_netdev;
           if (netdev_ctx) {
               vipif_ctx = netdev_priv(netdev_ctx);
               if (vipif_ctx->ip4_addr == ntohl(arp_pkt->ar_tip)) {
                   printk("KIWI %d:vipif_arp %d req 0x%x found\n", get_cpu(), i, ntohl(arp_pkt->ar_tip));
                   break;
               }

           }

       }

    }




    return 0;
}
