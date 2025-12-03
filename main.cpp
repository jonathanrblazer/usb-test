#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

static const char* SERIAL_PORT = "/dev/ttyACM0";
static const int   BAUDRATE    = B115200;

std::atomic<bool> running(true);

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
    tty.c_cflag &= ~CRTSCTS;  // Teensy USB uses no HW flow control

    tty.c_cc[VMIN]  = 1;  // read returns after at least 1 byte
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
    unsigned char buf[512];

    while (running.load()) {
        int n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            std::cout.write((char*)buf, n);
            std::cout.flush();
        }
    }
}

// -----------------------------------------------------
// MAIN PROGRAM
// -----------------------------------------------------
int main() {
    std::cout << "Opening " << SERIAL_PORT << "..." << std::endl;

    int fd = open(SERIAL_PORT, O_RDWR | O_NOCTTY | O_NONBLOCK);
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

        line += "\n";  // Teensy usually accepts newline endings

        ssize_t written = write(fd, line.c_str(), line.size());
        if (written < 0) {
            perror("write");
        }
    }

    running.store(false);
    reader.join();

    close(fd);
    return 0;
}
