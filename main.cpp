#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <sstream>
#include <iomanip>
#include <dirent.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

// ---------------- CONFIG ----------------
static const int ROWS = 72;
static const int COLS = 72;
static const int BAUDRATE = B115200;
static const char *SPI_DEV = "/dev/spidev0.0";
static const uint32_t SPI_SPEED = 1000000;
static const uint8_t SPI_MODE = 3;
static const uint8_t SPI_BITS = 8;

// ----------------------------------------
std::atomic<bool> running(true);

// ---------------- SERIAL HELPERS ----------------
std::vector<std::string> findACMports() {
    std::vector<std::string> ports;
    DIR* dir = opendir("/dev");
    if (!dir) return ports;
    dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (std::string(entry->d_name).rfind("ttyACM", 0) == 0)
            ports.emplace_back("/dev/" + std::string(entry->d_name));
    }
    closedir(dir);
    return ports;
}

int configureSerial(int fd) {
    termios tty{};
    if (tcgetattr(fd, &tty) != 0) return -1;
    cfmakeraw(&tty);
    cfsetspeed(&tty, BAUDRATE);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    return tcsetattr(fd, TCSANOW, &tty);
}

// ---------------- SPI HELPERS ----------------
int openSPI() {
    int fd = open(SPI_DEV, O_WRONLY);
    if (fd < 0) return -1;
    ioctl(fd, SPI_IOC_WR_MODE, &SPI_MODE);
    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &SPI_BITS);
    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &SPI_SPEED);
    return fd;
}

void sendImageSPI(int fd, const std::vector<int16_t>& img) {
    spi_ioc_transfer tr{};
    tr.tx_buf = (unsigned long)img.data();
    tr.len = img.size() * sizeof(int16_t);
    tr.speed_hz = SPI_SPEED;
    tr.bits_per_word = SPI_BITS;
    ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
}

// ---------------- IMAGE PRINT ----------------
void printImage(const std::vector<int16_t>& img) {
    for (int r = 0; r < ROWS; ++r) {
        std::cout << "R" << std::setw(2) << std::setfill('0') << r << ": ";
        for (int c = 0; c < COLS; ++c)
            std::cout << std::setw(6) << img[r*COLS + c] << " ";
        std::cout << "\n";
    }
}

// ---------------- SERIAL READ THREAD ----------------
void readThread(int serial_fd, int spi_fd) {
    std::string buffer;
    char tmp[512];

    bool inImage = false;
    int row = 0;
    std::vector<int16_t> image;
    image.reserve(ROWS * COLS);

    while (running.load()) {
        int n = read(serial_fd, tmp, sizeof(tmp));
        if (n <= 0) continue;

        buffer.append(tmp, n);

        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);

            if (line.find("DEBUGMODE 106") != std::string::npos) {
                image.clear();
                row = 0;
                inImage = true;
                continue;
            }

            if (line.find("END_DEBUGMODE_106") != std::string::npos) {
                inImage = false;
                if ((int)image.size() == ROWS * COLS) {
                    printImage(image);
                    sendImageSPI(spi_fd, image);
                    std::cout << ">>> Image forwarded to SPI (" 
                              << image.size() << " pixels)\n";
                } else {
                    std::cerr << "Incomplete image: got "
                              << image.size() << " values\n";
                }
                continue;
            }

            if (!inImage) continue;

            std::stringstream ss(line);
            int val;
            while (ss >> val && image.size() < ROWS * COLS)
                image.push_back((int16_t)val);
        }
    }
}

// ---------------- MAIN ----------------
int main() {
    auto ports = findACMports();
    if (ports.empty()) {
        std::cerr << "No ttyACM devices found.\n";
        return 1;
    }

    std::string port = ports[0];
    int serial_fd = open(port.c_str(), O_RDWR | O_NOCTTY);
    if (serial_fd < 0 || configureSerial(serial_fd) < 0) {
        std::cerr << "Failed to open serial.\n";
        return 1;
    }

    int spi_fd = openSPI();
    if (spi_fd < 0) {
        std::cerr << "Failed to open SPI.\n";
        return 1;
    }

    std::thread reader(readThread, serial_fd, spi_fd);

    std::string cmd;
    while (std::getline(std::cin, cmd)) {
        cmd += "\n";
        write(serial_fd, cmd.c_str(), cmd.size());
    }

    running.store(false);
    reader.join();
    close(serial_fd);
    close(spi_fd);
    return 0;
}
