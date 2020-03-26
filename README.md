# Martin's virtual IP interface hack up
modifiied `/driver/net/ethernet/intel/igb` device

tested using 4x GE NIC card

for each physical port (Master netdev) create 8 slave netdev interfaces.

## vipif_lower.c
monitor NETLINK RTM_NEWADDR to get IP address changes for each vipif netdev. Cannot just look at netdev.ndo_open
as in_link may not be created at that time and IP address can be set at any time independantly.
todo track MAC address changes and push down to ingress mapper

## vipif_upper.c
Impliments an ARP handle for master_netdev, responds for each slave/child vipif IP address if set.
impliments ingress mapper, maps MAC address to vipif and forwards skb to it for processing; vipif_recv_packet.

# build
to make the kernel

`$ make -j $(nproc)`

then make the modules

`$ sudo make modules_install`

then install the new images

`$ sudo make install`

you can the reboot to test

`$ sudo reboot now`

if image crashes, 
1. power of the PC/server 
2. remove IGB NIC
3. power of the PC/server (IGB NIC will not be probed so kernel should boot)
4. fix your code
5. power off `sudo shutdown now`
6. re-install IGB NIC
7. power on	(hopefully you fixed the bug)


git push -u origin master



Linux kernel
============

There are several guides for kernel developers and users. These guides can
be rendered in a number of formats, like HTML and PDF. Please read
Documentation/admin-guide/README.rst first.

In order to build the documentation, use ``make htmldocs`` or
``make pdfdocs``.  The formatted documentation can also be read online at:

    https://www.kernel.org/doc/html/latest/

There are various text files in the Documentation/ subdirectory,
several of them using the Restructured Text markup notation.

Please read the Documentation/process/changes.rst file, as it contains the
requirements for building and running the kernel, and information about
the problems which may result by upgrading your kernel.
