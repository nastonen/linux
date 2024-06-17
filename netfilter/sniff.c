#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <net/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/string.h>

static char *search_str = "example";
module_param(search_str, charp, 0);
MODULE_PARM_DESC(search_str, "String to search for in packet payload");

static struct nf_hook_ops nfho;

static unsigned int hook_func(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
	struct iphdr *ip_header;
	struct tcphdr *tcp_header;
	struct udphdr *udp_header;
	char *data;
	unsigned int data_len;

	// check if the packet is IP
	ip_header = ip_hdr(skb);
	if (!ip_header)
		return NF_ACCEPT;

	// if not TCP, 
	if (ip_header->protocol == IPPROTO_TCP) {
		tcp_header = tcp_hdr(skb);
		data = (char *)((unsigned char *)tcp_header + (tcp_header->doff * 4));
		data_len = ntohs(ip_header->tot_len) - ip_hdrlen(skb) - (tcp_header->doff * 4);

		// search for the string
		if (data_len > 0 && strstr(data, search_str))
			pr_info("Found %s in packet from %pI4\n", search_str, &ip_header->saddr);

	} else if (ip_header->protocol == IPPROTO_UDP) {
		udp_header = udp_hdr(skb);
	}

	return NF_ACCEPT;
}

static int __init sniff_init(void)
{
	nfho.hook = hook_func;			// the hook function
	nfho.hooknum = NF_INET_PRE_ROUTING;	// hook at the pre-routing stage
	nfho.pf = PF_INET;			// IPv4 packets
	nfho.priority = NF_IP_PRI_FIRST;	// highest priority

	// register the hook
	nf_register_net_hook(&init_net, &nfho);
	pr_info("Netfilter module loaded\n");

	return 0;
}

static void __exit sniff_exit(void)
{
	nf_unregister_net_hook(&init_net, &nfho);
	pr_info("Netfilter module unloaded\n");
}

module_init(sniff_init);
module_exit(sniff_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Me");

