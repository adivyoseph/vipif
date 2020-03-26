#ifndef _VIPIF_H_
#define _VIPIF_H_
#include <linux/skbuff.h>
#include <linux/byteorder/generic.h>


#define VIPIF_PER_MASTER_MAX_CTX    8


/// <summary>
/// virtual IP interface create
/// builds a slave netdev instance 
/// </summary>
/// <param name="netdev"> pointer to master netdev</param> 
/// <param name="instance"> per master index</param> 
/// <returns>NULL or netdev child pointer</returns> 
struct net_device * vipif_create(struct net_device *netdev, int instance);

/// <summary>
/// handle ARP requests on behalh of each vipif instance
/// </summary> 
/// <param name="master_netdev"></param> 
/// <param name="skb">arp request</param> 
/// <returns>0 not found, 1 found</returns>
int vipif_arp(struct net_device *master_netdev, struct sk_buff *skb); 


typedef struct vipif_stats {
    int create;
    int open;
    int close;
    int mapp_update;
    int recieve;
    int transmit;

} t_vipif_stats;


//vipif netdev private data
//context
typedef struct vipif_ctx {
    int cookie;                         //0xc00c1e
    struct net_device   *master_netdev;  //back pointer to master netdev
        int             instance;       //this vipif master netdev instance index
        char mac_addr[8];               //
    __u32               ip4_addr;       //

    struct sock *nl_sk ;                //netlink socket for tracking ip address cahnges
    //stats
    t_vipif_stats  per_core_stats[16];

} t_vipif_ctx;

//netdev master ingress mapper per vipif entry
typedef struct vipif_map_entry{
    char mac_addr[8];                   //MAC address for filtering
    struct net_device *vipif_netdev;    //
    spinlock_t spinlock;                //fine spinlock to protect updates

} t_vipif_map_entry;




/// <summary>
/// push new vipif mac address down to ingress mapper
/// </summary>
/// <param name="master_netdev"></param>
/// <param name="perm_addr">new mac address</param>
/// <param name="instance">per master vipif index</param> 
/// <param name="cpu"></param> 
/// <returns>void</returns>
void vipif_uppersend_map_entry(struct net_device *master_netdev, u8 *perm_addr, int instance, int cpu);

#endif
