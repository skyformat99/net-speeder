#include <pcap.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <libnet.h>

/* default snap length (maximum bytes per packet to capture) */
#define SNAP_LEN 65535

#ifdef COOKED
	#define ETHERNET_H_LEN 16
#else
	#define ETHERNET_H_LEN 14
#endif

#define SPECIAL_TTL 88

void got_packet(u_char *args, const struct pcap_pkthdr *header, const u_char *packet);
void print_usage(void);
bpf_u_int32 g_net;

/*
 * print help text
 */
void print_usage(void) {
	printf("Usage: %s [interface][\"filter rule\"]\n", "net_speeder");
	printf("\n");
	printf("Options:\n");
	printf("    interface    Listen on <interface> for packets.\n");
	printf("    filter       Rules to filter packets.\n");
	printf("\n");
}

void got_packet(u_char *args, const struct pcap_pkthdr *header, const u_char *packet) {
	static int count = 1;
	struct libnet_ipv4_hdr *ip;
    uint32_t u32 = 0;

	libnet_t *libnet_handler = (libnet_t *)args;
	count++;

	ip = (struct libnet_ipv4_hdr*)(packet + ETHERNET_H_LEN);

    u32 = (uint32_t)ip->ip_dst.s_addr;
    if (ip->ip_p == 0x11 && u32 == g_net) {
        //udp and udp send to myself, ignore it
        return;
    }

	if(ip->ip_ttl != SPECIAL_TTL) {
		ip->ip_ttl = SPECIAL_TTL;
		int len_written = libnet_adv_write_raw_ipv4(libnet_handler, (u_int8_t *)ip, ntohs(ip->ip_len));
		if(len_written < 0) {
			printf("packet len:[%d] actual write:[%d]\n", ntohs(ip->ip_len), len_written);
			printf("err msg:[%s]\n", libnet_geterror(libnet_handler));
		}
	} else {
		//The packet net_speeder sent. nothing todo
	}
	return;
}

libnet_t* start_libnet(char *dev) {
	char errbuf[LIBNET_ERRBUF_SIZE];
	libnet_t *libnet_handler = libnet_init(LIBNET_RAW4_ADV, dev, errbuf);

	if(NULL == libnet_handler) {
		printf("libnet_init: error %s\n", errbuf);
	}
	return libnet_handler;
}

#define ARGC_NUM 3
int main(int argc, char **argv) {
	char *dev = NULL;
	char errbuf[PCAP_ERRBUF_SIZE];
	pcap_t *handle;

	char *filter_rule = NULL;
	struct bpf_program fp;
	bpf_u_int32 net, mask;
    pcap_if_t *devList;
    pcap_if_t *pdev;

	if (argc == ARGC_NUM) {
		dev = argv[1];
		filter_rule = argv[2];
		printf("Device: %s\n", dev);
		printf("Filter rule: %s\n", filter_rule);
	} else {
		print_usage();
		return -1;
	}

	printf("ethernet header len:[%d](14:normal, 16:cooked)\n", ETHERNET_H_LEN);

	if (pcap_lookupnet(dev, &net, &mask, errbuf) == -1) {
		printf("Couldn't get netmask for device %s: %s\n", dev, errbuf);
		net = 0;
		mask = 0;
	}

/* get the devices list */
if (pcap_findalldevs(&devList, errbuf) == -1)
{
    fprintf(stderr, "There is a problem with pcap_findalldevs: %s\n", errbuf);
    return -1;
}

/* scan the list for a suitable device to capture from */
for (pdev = devList; pdev != NULL; pdev = pdev->next)
{

    pcap_addr_t *dev_addr; //interface address that used by pcap_findalldevs()

    /* check if the device captureble*/
    for (dev_addr = pdev->addresses; dev_addr != NULL; dev_addr = dev_addr->next) {
        if (dev_addr->addr->sa_family == AF_INET && dev_addr->addr && dev_addr->netmask && (0 == strcmp(pdev->name, dev))) {
            //printf("Found a device %s on address 0x%02x with netmask 0x%2x\n", pdev->name, ((struct sockaddr_in *)dev_addr->addr)->sin_addr.s_addr, ((struct sockaddr_in *)dev_addr->netmask)->sin_addr.s_addr);
            g_net = (uint32_t)(((struct sockaddr_in *)dev_addr->addr)->sin_addr.s_addr);
            break;
        }
    }
}

found:

	printf("init pcap net=0x%x\n", g_net);

	handle = pcap_open_live(dev, SNAP_LEN, 1, 1000, errbuf);
	if(handle == NULL) {
		printf("pcap_open_live dev:[%s] err:[%s]\n", dev, errbuf);
		printf("init pcap failed\n");
		return -1;
	}

	printf("init libnet\n");
	libnet_t *libnet_handler = start_libnet(dev);
	if(NULL == libnet_handler) {
		printf("init libnet failed\n");
		return -1;
	}

	if (pcap_compile(handle, &fp, filter_rule, 0, net) == -1) {
		printf("filter rule err:[%s][%s]\n", filter_rule, pcap_geterr(handle));
		return -1;
	}

	if (pcap_setfilter(handle, &fp) == -1) {
		printf("set filter failed:[%s][%s]\n", filter_rule, pcap_geterr(handle));
		return -1;
	}

	while(1) {
		pcap_loop(handle, 1, got_packet, (u_char *)libnet_handler);
	}

	/* cleanup */
	pcap_freecode(&fp);
	pcap_close(handle);
	libnet_destroy(libnet_handler);
	return 0;
}
