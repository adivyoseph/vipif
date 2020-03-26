#include <linux/netdevice.h>
#include <linux/printk.h>
#include <linux/smp.h>
#include <linux/etherdevice.h>
#include <linux/inetdevice.h>
#include <linux/netlink.h>
#include <net/sock.h>
#include <linux/rtnetlink.h>
#include <net/netlink.h>

#include "vipif.h"
#include "igb.h"

/*
virtual IP interfcae (netdev) upper 
Each instance is bound to an actual (physical) netdev device which has vipif_lower hooked into it. 
On downlink, namespace transmit, the instance may be called on any Core, per Core synchronization 
is required in the vipif_lower as the master netdev transmit may already be invoked by another 
vipif_upper instance. 
vipif_upper uplink is called directly from vipif_lower, on the same Core, so no  synchronization 
is required. 
 
per Core stats are maintained in each  vipif_upper context structure to simplify tracing and access 
 
*/


int vipif_open(struct net_device *);
int vipif_close(struct net_device *);
int vipif_send_packet(struct sk_buff *skb, struct net_device *);
int vipif_set_mac(struct net_device *, void *);
int vipif_change_mtu(struct net_device *netdev, int new_mtu);
void vipif_get_stats64(struct net_device *dev,
			    struct rtnl_link_stats64 *stats);


static const struct net_device_ops vipif_netdev_ops = {
	.ndo_open		= vipif_open,                           //really if up/enable
	.ndo_stop		= vipif_close,                          //really if down/disable
	.ndo_start_xmit		= vipif_send_packet,                //tx entry point from namespace stack
    .ndo_features_check = NULL,                             //todo netdev_features_t features override
    .ndo_select_queue   = NULL,                             //todo may be useful for core afinity to mq master netdev
    .ndo_change_rx_flags = NULL,
    .ndo_set_rx_mode    = NULL,                             //
	.ndo_set_mac_address	= vipif_set_mac,                //allow nns to change MAC address
	.ndo_validate_addr	= eth_validate_addr,                //just use kernel service
	.ndo_do_ioctl		= NULL,                             //depricated service
	.ndo_change_mtu		= vipif_change_mtu,                 //todo disable ??
	.ndo_tx_timeout		= NULL,                             //tx queue timeout handler
	.ndo_get_stats64	= vipif_get_stats64,                  //async stats read
    .ndo_get_iflink     = NULL,                             //todo
    .ndo_change_proto_down = NULL,                          //todo
    .ndo_fill_metadata_dst = NULL,
    .ndo_set_rx_headroom = NULL,
	.ndo_setup_tc		= NULL,
};


struct my_nlattr {
	__u16           nla_len;
	__u16           nla_type;
    __u32           ipv4addr;
};

typedef struct msg_nlh {
    struct nlmsghdr nlh;
    struct ifaddrmsg ifm;       //ifa_index used to finf netdev
    struct my_nlattr pnlattr;
} t_msg_nlh;

//no device context just correct ifa_index

int vipif_rtm_newaddr(struct sk_buff *skb, struct nlmsghdr *nlh,
			      struct netlink_ext_ack *ack){
    struct net_device *this_netdev; 
    t_vipif_ctx *this_context;
    struct my_nlattr *pnlattr;
    t_msg_nlh  *pmsg_nlh = (t_msg_nlh *)nlh;
    int rem;
    int cpu;
    int i;
    unsigned char *cpwork;
	struct net *net;

    if(skb->sk){

        net = sock_net(skb->sk);
    }

    this_netdev  = __dev_get_by_index(net, pmsg_nlh->ifm.ifa_index);
    this_context = netdev_priv(this_netdev);
    if (this_context->cookie != 0xc00c1e) {  //only vipid netdev type
        //return 0;
    }
    cpu = get_cpu();

    rem = nlh->nlmsg_len - sizeof(struct nlmsghdr) - sizeof(struct ifaddrmsg);

    if (nlh->nlmsg_type == RTM_NEWADDR) {  
        printk("KIWI %d:vipif_rtm_newaddr %s RTM_NEWADDR len %d\n", cpu, this_netdev->name, rem);
        pnlattr = &pmsg_nlh->pnlattr;
        cpwork = (unsigned char *)&pmsg_nlh->ifm;
        for (i = 0; i < sizeof(struct ifaddrmsg); i++, cpwork++) {
            printk("0x%02x, ", *cpwork) ;
        }
        printk("==");
        for (i = 0; i < (rem - sizeof(struct ifaddrmsg)); i++, cpwork++) {
            printk("0x%02x, ", *cpwork) ;
        }
        cpwork = (unsigned char *)pnlattr;
        while (rem > 0) {
            printk("nlattr type %x len %d\n", pnlattr->nla_type, pnlattr->nla_len);
            if (pnlattr->nla_type == 0x02) {
                this_context->ip4_addr = pnlattr->ipv4addr;
                printk("  new ip address 0x%x", htonl(this_context->ip4_addr));
            }
            rem = rem - pnlattr->nla_len;
            printk("  pnlattr %p rem %d\n", pnlattr, rem);
            cpwork = &cpwork[pnlattr->nla_len];
            pnlattr = (struct my_nlattr *) cpwork;
            printk("  pnlattr %p\n", pnlattr);
           rem = 0;
        }

    }
    else {
        printk("KIWI :vipif_rtm_newaddr event %d\n", nlh->nlmsg_type);
    }
    return 0;
}

struct net_device * vipif_create(struct net_device *master_netdev, int instance){
    struct net_device *this_netdev;     // this netdev pointer
    struct igb_adapter *adapter = netdev_priv(master_netdev);
    t_vipif_ctx *this_context;
    char cname[32];
    int err;
    int cpu = get_cpu();

    printk("KIWI %d:vipif_create instance %d\n", cpu, instance);

    //allocate a netdev structure with a vipif_ctx appended
    this_netdev = alloc_etherdev(sizeof(t_vipif_ctx));
	if (!this_netdev){
        printk("KIWI %d:vipif_create(%d) alloc_etherdev failed\n", cpu, instance);
        return NULL;
    }

    //setup netdev instance
    this_netdev->netdev_ops = &vipif_netdev_ops;
	this_context = netdev_priv(this_netdev);        //get appened context
    this_context->cookie = 0xc00c1e;
    this_context->instance = instance;
    this_context->per_core_stats[cpu].create++;
    //link back to master netdev
    this_context->master_netdev = master_netdev;
    //set device name
    sprintf(cname, "%s_%d", master_netdev->name, instance );
    this_netdev->addr_len = ETH_ALEN;

	this_netdev->min_mtu = ETH_MIN_MTU;
	this_netdev->max_mtu = MAX_STD_JUMBO_FRAME_SIZE;
    strcpy(this_netdev->name, cname);
    //set MAC address
    this_context->mac_addr[0] = ((int)this_netdev) & 0x00ff;
    this_context->mac_addr[1] = (((int)this_netdev) >> 8) & 0x00ff;
    this_context->mac_addr[2] = 0x01;
    this_context->mac_addr[3] = 0x11;
    this_context->mac_addr[4] = 0x01;
    this_context->mac_addr[5] = instance;
    //update master inbound mapping table
    //vipif_uppersend_map_entry(master_netdev, this_netdev->perm_addr, instance, cpu);
    memcpy(adapter->vipif_map[instance].mac_addr, this_context->mac_addr, 6);
    memcpy(this_netdev->dev_addr, this_context->mac_addr, 6);
    printk("KIWI %d:map_entry instance %d %02x:%02x:%02x:%02x:%02x:%02x\n",
           cpu,
           instance,
           this_netdev->dev_addr[0], 
           this_netdev->dev_addr[1], 
           this_netdev->dev_addr[2],
           this_netdev->dev_addr[3],
           this_netdev->dev_addr[4],
           this_netdev->dev_addr[5]);

/**/
	err = register_netdev(this_netdev);
	if (err){
		free_netdev(this_netdev);
        return NULL;
    }
    netif_carrier_off(this_netdev);
    /**/

    rtnl_register_module(THIS_MODULE, PF_INET, RTM_NEWADDR, vipif_rtm_newaddr, NULL, 0);

    return this_netdev;
}
 
int vipif_delete(struct net_device *this_netdev){
    //	unregister_netdev(netdev);
    //	free_netdev(netdev);

    return 0;
}







int vipif_open(struct net_device *this_netdev){
    t_vipif_ctx *this_context;
    struct in_device *ip_ptr; 
  //  struct in_ifaddr *ifa_list;

    this_context = netdev_priv(this_netdev); 
    printk("KIWI %d:vipif_open[%d] %s \n", get_cpu(), this_context->instance,  this_netdev->name);
    ip_ptr = this_netdev->ip_ptr;
    if (ip_ptr) {
        if (ip_ptr->ifa_list) {
           // ifa_list = ip_ptr->ifa_list;
           printk("  ifa_local     0x%x\n",ip_ptr->ifa_list->ifa_local);
           printk("  ifa_address   0x%x\n",ip_ptr->ifa_list->ifa_address);
           printk("  ifa_mask      0x%x\n",ip_ptr->ifa_list->ifa_mask);
           printk("  ifa_broadcast 0x%x\n",ip_ptr->ifa_list->ifa_broadcast);
        }
    }
    return 0;
}

int vipif_close(struct net_device *this_netdev){
    t_vipif_ctx *this_context;

    this_context = netdev_priv(this_netdev); 
    printk("KIWI %d:vipif_close[%d] %s \n", get_cpu(), this_context->instance,  this_netdev->name);

    return 0;
}

int vipif_send_packet(struct sk_buff *skb, struct net_device *this_netdev){
    return 0;
}

int vipif_set_mac(struct net_device *this_netdev, void *vp){
    return 0;
}

int vipif_change_mtu(struct net_device *this_netdev, int new_mtu){
    return 0;
}

void vipif_get_stats64(struct net_device *this_netdev,
			    struct rtnl_link_stats64 *stats){
 
}
