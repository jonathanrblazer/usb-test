#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <iomanip>
#include <vector>
#include <dirent.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

static const int BAUDRATE = B115200;
std::atomic<bool> running(true);

// -----------------------------------------------------
// Scan /dev for all ttyACM* devices
// -----------------------------------------------------
std::vector<std::string> findACMports() {
    std::vector<std::string> ports;
    DIR* dir = opendir("/dev");
    if (!dir) return ports;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.rfind("ttyACM", 0) == 0) {  // starts with "ttyACM"
            ports.emplace_back("/dev/" + name);
        }
    }
    closedir(dir);
    return ports;
}

// -----------------------------------------------------
// Configure the serial port
// -----------------------------------------------------
int configureSerial(int fd) {
    struct termios tty;

    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        return -1;
    }

    cfmakeraw(&tty);
    cfsetspeed(&tty, BAUDRATE);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;  // No hardware flow control for Teensy USB

    tty.c_cc[VMIN]  = 1;  
    tty.c_cc[VTIME] = 0;

    tcflush(fd, TCIFLUSH);

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        return -1;
    }
    return 0;
}

// -----------------------------------------------------
// Thread: read incoming bytes from Teensy
// -----------------------------------------------------
void readThread(int fd) {
    char buf[512];
    while (running.load()) {
        int n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            // Trea incoming data as TEXT
            std::cout.write(buf, n);
            std::cout.flush();
        }
    }
}

// -----------------------------------------------------
// MAIN PROGRAM
// -----------------------------------------------------
int main() {
    // Step 1: find available ACM ports
    auto ports = findACMports();

    if (ports.empty()) {
        std::cerr << "ERROR: No /dev/ttyACM* ports found.\n";
        return 1;
    }

    std::string selectedPort;

    if (ports.size() == 1) {
        selectedPort = ports[0];
        std::cout << "Found 1 ACM device: " << selectedPort << "\n";
    } else {
        std::cout << "Multiple ACM devices detected:\n";
        for (size_t i = 0; i < ports.size(); i++) {
            std::cout << "  [" << i << "] " << ports[i] << "\n";
        }
        std::cout << "Enter the number of the device to use: ";
        size_t choice;
        std::cin >> choice;

        // if cin was used, flush newline before getline loop begins later
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        if (choice >= ports.size()) {
            std::cerr << "Invalid choice.\n";
            return 1;
        }
        selectedPort = ports[choice];
    }

    std::cout << "Opening " << selectedPort << "...\n";

    int fd = open(selectedPort.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (configureSerial(fd) < 0) {
        return 1;
    }

    std::cout << "Serial port opened.\n";
    std::cout << "Type commands (e.g., debugmode(111); ) and press Enter.\n";
    std::cout << "------------------------------------------------------\n";

    // Launch receiver thread
    std::thread reader(readThread, fd);

    // Main loop for sending user commands
    std::string line;
    while (running.load()) {
        if (!std::getline(std::cin, line)) break;
        line += "\n";  // Teensy expects newline terminated commands
        ssize_t written = write(fd, line.c_str(), line.size());
        if (written < 0) perror("write");
    }

    running.store(false);
    reader.join();
    close(fd);
    return 0;
}
