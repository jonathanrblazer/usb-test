// ============================================================
// CM5 Stereo Bring-Up: SINGLE FILE main.cpp
// ============================================================

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <cmath>
#include <chrono>
#include <dirent.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#include <opencv2/opencv.hpp>

// ============================================================
// CONFIG
// ============================================================

static const int ROWS = 72;
static const int COLS = 72;

static const int BAUDRATE = B115200;
static const char *SPI_DEV = "/dev/spidev0.0";
static const uint32_t SPI_SPEED = 8000000;
static const uint8_t SPI_MODE = 3;
static const uint8_t SPI_BITS = 8;

// ============================================================
// GLOBAL STATE
// ============================================================

std::mutex frameMutex;
std::vector<int16_t> stereoFrame;
std::atomic<bool> newFrame(false);
std::atomic<bool> running(true);

// ============================================================
// SERIAL HELPERS
// ============================================================

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

// ============================================================
// SPI HELPERS
// ============================================================

int openSPI() {
    int fd = open(SPI_DEV, O_WRONLY);
    if (fd < 0) return -1;
    ioctl(fd, SPI_IOC_WR_MODE, &SPI_MODE);
    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &SPI_BITS);
    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &SPI_SPEED);
    return fd;
}

void sendImageSPI(int fd, const std::vector<int16_t>& img) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(img.data());
    size_t total = img.size() * sizeof(int16_t);
    const size_t CHUNK = 2592;

    size_t offset = 0;
    while (offset < total) {
        size_t n = std::min(CHUNK, total - offset);
        spi_ioc_transfer tr{};
        tr.tx_buf = (unsigned long)(p + offset);
        tr.len = n;
        tr.speed_hz = SPI_SPEED;
        tr.bits_per_word = SPI_BITS;
        if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
            perror("SPI send");
            return;
        }
        offset += n;
    }
}

// ============================================================
// TIME HELPER (Arduino micros() replacement)
// ============================================================

static inline long micros_now() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()
    ).count();
}

// ============================================================
// ================== STEREO CORE ==============================
// ============================================================

#define STEREO_MAXBLOCKRAD 8
#define STEREO_MAXBLOCKPIX ((STEREO_MAXBLOCKRAD*2+1)*(STEREO_MAXBLOCKRAD*2+1))
#define STEREO_SEARCHRAD_Y 2
#define STEREO_SEARCHRAD_X1 4
#define STEREO_SEARCHRAD_X2 8
#define STEREO_MAXSEARCHRAD 8
#define STEREO_MAXSEARCHPIX ((STEREO_MAXSEARCHRAD*2+1)*(STEREO_MAXSEARCHRAD*2+1))
#define STEREO_MAXBLOCK 50

struct Stereo {
    // --- control ---
    short m_Qblocknum = 0;
    short m_Qiscal    = 0;

    // --- confidence thresholds (reasonable defaults) ---
    short m_BVTstd    = 15;
    short m_BVTvar    = 10;
    float m_BVTc0     = 0.02f;
    float m_BVTcr     = 3.0f;
    float m_BVTmd     = 0.5f;
    float m_BVTmatch  = 0.8f;

    // --- confidence outputs ---
    short m_BVstd[STEREO_MAXBLOCK]{};
    short m_BVvar[STEREO_MAXBLOCK]{};
    float m_BVc0[STEREO_MAXBLOCK]{};
    float m_BVcr[STEREO_MAXBLOCK]{};
    float m_BVmd[STEREO_MAXBLOCK]{};
    float m_BVmatch[STEREO_MAXBLOCK]{};

    // --- timing ---
    float m_microsPerBlock = 0.0f;
    float m_microsPerPix   = 0.0f;

    // images
    short *m_pI1 = nullptr;
    short *m_pI2 = nullptr;
    short m_numrows = 0;
    short m_numcols = 0;

    // blocks
    short m_numblocks = 0;
    short m_blockrad = 5;
    short m_bm1, m_bn1, m_bm2, m_bn2;
    short m_BM1[STEREO_MAXBLOCK]{};
    short m_BN1[STEREO_MAXBLOCK]{};
    short m_BM2[STEREO_MAXBLOCK]{};
    short m_BN2[STEREO_MAXBLOCK]{};
    float m_BM2frac[STEREO_MAXBLOCK]{};
    float m_BN2frac[STEREO_MAXBLOCK]{};

    // outputs
    float m_BDM[STEREO_MAXBLOCK]{};
    float m_BDN[STEREO_MAXBLOCK]{};
    char  m_Bvalid[STEREO_MAXBLOCK]{};

    // scratch
    float m_RMatch[STEREO_MAXSEARCHPIX]{};
    short m_RDM, m_RDN;
    float m_RDMfrac, m_RDNfrac;

    // timing
    long m_micros = 0;

    void Initialize()
    {
        m_bm1 = m_bn1 = 36;
        m_bm2 = m_bn2 = 32;

        m_Qiscal = 0;
        m_Qblocknum = 0;

        for (short b = 0; b < STEREO_MAXBLOCK; ++b) {
            m_BM2frac[b] = 0.0f;
            m_BN2frac[b] = 0.0f;
            m_Bvalid[b]  = 0x00;
        }
    }


    void SetImageSize(short r, short c) {
        m_numrows = r;
        m_numcols = c;
    }

    void LinkImageArrays(short* i1, short* i2) {
        m_pI1 = i1;
        m_pI2 = i2;
    }

    void InitDummyCalibration() {
        for (short b = 0; b < m_numblocks; ++b) {
            m_BM2[b] = m_BM1[b];
            m_BN2[b] = m_BN1[b];
            m_BM2frac[b] = 0.0f;
            m_BN2frac[b] = 0.0f;
            m_Bvalid[b] = 0x80;
        }
    }

    float Hyper(float a, float b, float c, short method)
    {
        if (method == 2) {
            float denom = b - 0.5f * (a + c);
            if (std::fabs(denom) < 1e-6f)
                return 0.0f;

            float x = 0.25f * (c - a) / denom;

            // clamp to sane subpixel range
            if (x >  0.5f) x =  0.5f;
            if (x < -0.5f) x = -0.5f;
            return x;
        }
        else if (method == 1) {
            if (c > a) {
                float d = (a - b);
                return (std::fabs(d) < 1e-6f) ? 0.0f : 0.5f * (a - c) / d;
            } else {
                float d = (c - b);
                return (std::fabs(d) < 1e-6f) ? 0.0f : 0.5f * (a - c) / d;
            }
        }
        else {
            if (a > c) return -0.25f;
            if (a < c) return  0.25f;
            return 0.0f;
        }
    }


    // ---- stereo math (trimmed to essentials) ----
    bool StereoBlockMatchOne()
    {
        // image pointers
        short* pimg1 = m_pI1;
        short* pimg2 = m_pI2;

        // search parameters
        short searchwidth = m_Qiscal
            ? (STEREO_MAXSEARCHRAD * 2 + 1)
            : (STEREO_SEARCHRAD_X1 + STEREO_SEARCHRAD_X2 + 1);

        short searchy  = m_Qiscal ? STEREO_MAXSEARCHRAD : STEREO_SEARCHRAD_Y;
        short searchx1 = m_Qiscal ? STEREO_MAXSEARCHRAD : STEREO_SEARCHRAD_X1;
        short searchx2 = m_Qiscal ? STEREO_MAXSEARCHRAD : STEREO_SEARCHRAD_X2;

        short blockdim = m_blockrad * 2 + 1;
        short blockpix = blockdim * blockdim;

        // block centers
        short mc1 = m_bm1 + m_BM1[m_Qblocknum];
        short nc1 = m_bn1 + m_BN1[m_Qblocknum];
        short mc2 = m_bm2 + m_BM2[m_Qblocknum];
        short nc2 = m_bn2 + m_BN2[m_Qblocknum];

        // clear match surface
        for (short i = 0; i < STEREO_MAXSEARCHPIX; ++i)
            m_RMatch[i] = 0.0f;

        // out-of-bounds check (runtime mode only)
        if (!m_Qiscal) {
            short ms1 = mc2 - m_blockrad - searchy;
            short ms2 = mc2 + m_blockrad + searchy;
            short ns1 = nc2 - m_blockrad - searchx1;
            short ns2 = nc2 + m_blockrad + searchx2;

            if (ms1 < 0 || ns1 < 0 || ms2 >= m_numrows || ns2 >= m_numcols) {
                m_RDM = m_RDN = 0;
                m_RDMfrac = m_RDNfrac = 0.0f;
                m_Bvalid[m_Qblocknum] &= 0x80;
                return false;
            }
        }

        // ----- build zero-mean block -----
        short zmblock[STEREO_MAXBLOCKPIX];
        long sumi = 0;

        short ma1 = mc1 - m_blockrad;
        short na1 = nc1 - m_blockrad;
        short bbase = ma1 * m_numcols + na1;

        for (short mb = 0; mb < blockdim; ++mb)
            for (short nb = 0; nb < blockdim; ++nb) {
                short indx = bbase + mb * m_numcols + nb;
                short bndx = mb * blockdim + nb;
                short v = pimg1[indx];
                zmblock[bndx] = v;
                sumi += v;
            }

        sumi /= blockpix;

        float varvalx = 0.0f, varvaly = 0.0f;
        long sumvar = 0;

        for (short mb = 0; mb < blockdim; ++mb)
            for (short nb = 0; nb < blockdim; ++nb) {
                short bndx = mb * blockdim + nb;
                zmblock[bndx] -= sumi;
                sumvar += zmblock[bndx] * zmblock[bndx];

                if (nb) varvalx += std::abs(zmblock[bndx] - zmblock[bndx - 1]);
                if (mb) varvaly += std::abs(zmblock[bndx] - zmblock[bndx - blockdim]);
            }

        float stdval = std::sqrt((float)sumvar / blockpix);
        short bpmbd = blockdim * (blockdim - 1);
        varvalx /= bpmbd;
        varvaly /= bpmbd;
        float varval = m_Qiscal ? std::max(varvalx, varvaly) : varvalx;

        // ----- matching -----
        float bestmatch = -1e9f;
        short bestm = 0, bestn = 0;

        short zmprospect[STEREO_MAXBLOCKPIX];

        for (short ms = -searchy; ms <= searchy; ++ms) {
            short ms1 = mc2 - m_blockrad + ms;
            for (short ns = -searchx1; ns <= searchx2; ++ns) {
                short matchndx =
                    (ms + searchy) * searchwidth + (ns + searchx1);

                short ns1 = nc2 - m_blockrad + ns;
                short pbase = ms1 * m_numcols + ns1;

                sumi = 0;
                for (short mb = 0; mb < blockdim; ++mb)
                    for (short nb = 0; nb < blockdim; ++nb) {
                        short indx = pbase + mb * m_numcols + nb;
                        short bndx = mb * blockdim + nb;
                        short v = pimg2[indx];
                        zmprospect[bndx] = v;
                        sumi += v;
                    }

                sumi /= blockpix;

                float sbb = 0, spp = 0, sbp = 0;
                for (short i = 0; i < blockpix; ++i) {
                    float pb = zmblock[i];
                    float pp = zmprospect[i] - sumi;
                    sbb += pb * pb;
                    spp += pp * pp;
                    sbp += pb * pp;
                }

                float dircos = sbp / std::sqrt(sbb * spp);
                m_RMatch[matchndx] = dircos;

                if (dircos > bestmatch) {
                    bestmatch = dircos;
                    bestm = ms;
                    bestn = ns;
                }
            }
        }

        m_RDM = bestm;
        m_RDN = bestn;

        m_RDMfrac = m_RDNfrac = 0.0f;
        short matchndx =
            (bestm + searchy) * searchwidth + (bestn + searchx1);

        if (bestm > -searchy && bestm < searchy)
            m_RDMfrac = Hyper(
                m_RMatch[matchndx - searchwidth],
                m_RMatch[matchndx],
                m_RMatch[matchndx + searchwidth], 2);

        if (bestn > -searchx1 && bestn < searchx2)
            m_RDNfrac = Hyper(
                m_RMatch[matchndx - 1],
                m_RMatch[matchndx],
                m_RMatch[matchndx + 1], 2);

        // confidence tests (unchanged logic)
        float conv0, conv1, conv2, conv3;
        if (bestn > -searchx1 && bestn < searchx2) {
            conv0 = m_RMatch[matchndx] -
                    (m_RMatch[matchndx - 1] + m_RMatch[matchndx + 1]) / 2;
            conv1 = m_RMatch[matchndx] -
                    (m_RMatch[matchndx - searchwidth] + m_RMatch[matchndx + searchwidth]) / 2;
            conv2 = m_RMatch[matchndx] -
                    (m_RMatch[matchndx - searchwidth + 1] + m_RMatch[matchndx + searchwidth - 1]) / 2;
            conv3 = m_RMatch[matchndx] -
                    (m_RMatch[matchndx - searchwidth - 1] + m_RMatch[matchndx + searchwidth + 1]) / 2;
        } else {
            conv0 = conv1 = conv2 = conv3 = 0.0f;
        }

        float convratio = std::max({conv1, conv2, conv3}) / conv0;

        m_Bvalid[m_Qblocknum] &= 0x80;
        m_BVmatch[m_Qblocknum] = bestmatch;
        if (bestmatch >= m_BVTmatch) m_Bvalid[m_Qblocknum] |= 0x20;

        m_BVmd[m_Qblocknum] =
            std::abs(m_RDM + m_RDMfrac - m_BM2frac[m_Qblocknum]);
        if (m_BVmd[m_Qblocknum] <= m_BVTmd) m_Bvalid[m_Qblocknum] |= 0x10;

        m_BVcr[m_Qblocknum] = convratio;
        if (convratio <= m_BVTcr) m_Bvalid[m_Qblocknum] |= 0x08;

        m_BVc0[m_Qblocknum] = conv0;
        if (conv0 >= m_BVTc0) m_Bvalid[m_Qblocknum] |= 0x04;

        m_BVvar[m_Qblocknum] = (short)(varval + 1);
        if (varval >= m_BVTvar) m_Bvalid[m_Qblocknum] |= 0x02;

        m_BVstd[m_Qblocknum] = stdval;
        if (stdval >= m_BVTstd) m_Bvalid[m_Qblocknum] |= 0x01;

        return true;
    }

    short StereoBlockMatchBank(short /*bank*/)
    {
        short numskipped = 0;

        auto t0 = std::chrono::steady_clock::now();

        for (short b = 0; b < m_numblocks; ++b) {
            m_Qiscal = 0;
            m_Qblocknum = b;

            m_BDM[b] = m_BDN[b] = 9.999f;

            if (!StereoBlockMatchOne()) {
                numskipped++;
            } else {
                m_BDM[b] = m_RDM + m_RDMfrac - m_BM2frac[b];
                m_BDN[b] = m_RDN + m_RDNfrac - m_BN2frac[b];
            }
        }

        auto t1 = std::chrono::steady_clock::now();
        m_micros =
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        m_microsPerBlock = (float)m_micros / m_numblocks;

        short pixperblock =
            (2 * STEREO_SEARCHRAD_Y + 1) *
            (STEREO_SEARCHRAD_X1 + STEREO_SEARCHRAD_X2 + 1);

        m_microsPerPix = m_microsPerBlock / pixperblock;

        return numskipped;
    }
};


// ============================================================
// STEREO SETUP (RC50)
// ============================================================

void StereoSetup_RC50(Stereo& st) {
    constexpr short BLOCKSPERROW = 11;
    constexpr short BLOCKSPERCOL = 3;
    constexpr float SCALEROWS = 2.1f;
    constexpr float SCALECOLS = 0.38f;

    st.m_numblocks = BLOCKSPERROW * BLOCKSPERCOL;
    st.SetImageSize(ROWS, COLS);

    short b = 0;
    for (short br = 0; br < BLOCKSPERCOL; ++br) {
        for (short bc = 0; bc < BLOCKSPERROW; ++bc) {
            st.m_BM1[b] = static_cast<short>(
                (br - BLOCKSPERCOL / 2) * BLOCKSPERCOL * SCALEROWS
            );
            st.m_BN1[b] = static_cast<short>(
                (bc - BLOCKSPERROW / 2) * BLOCKSPERROW * SCALECOLS
            );
            ++b;
        }
    }
}

// ============================================================
// OpenCV DISPLAY
// ============================================================

cv::Mat makeStereoDisplay(const std::vector<int16_t>& stereo) {
    cv::Mat left16(ROWS, COLS, CV_16SC1, (void*)stereo.data());
    cv::Mat right16(ROWS, COLS, CV_16SC1,
                    (void*)(stereo.data() + ROWS * COLS));

    cv::Mat left8, right8;
    cv::normalize(left16, left8, 0, 255, cv::NORM_MINMAX);
    cv::normalize(right16, right8, 0, 255, cv::NORM_MINMAX);
    left8.convertTo(left8, CV_8UC1);
    right8.convertTo(right8, CV_8UC1);

    cv::Mat stereo8;
    cv::hconcat(left8, right8, stereo8);
    return stereo8;
}

// ============================================================
// SERIAL READ THREAD
// ============================================================

void readThread(int serial_fd, int spi_fd) {
    std::string buffer;
    char tmp[512];
    bool inImage = false;
    std::vector<int16_t> images;
    images.reserve(2 * ROWS * COLS);

    while (running.load()) {
        int n = read(serial_fd, tmp, sizeof(tmp));
        if (n <= 0) continue;
        buffer.append(tmp, n);

        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);

            if (line.find("DEBUGMODE 106") != std::string::npos) {
                images.clear();
                inImage = true;
                continue;
            }

            if (line.find("END_DEBUGMODE_106") != std::string::npos) {
                inImage = false;
                if ((int)images.size() == 2 * ROWS * COLS) {
                    {
                        std::lock_guard<std::mutex> lock(frameMutex);
                        stereoFrame = images;
                        newFrame.store(true);
                    }
                    for (int imgIdx = 0; imgIdx < 2; ++imgIdx) {
                        std::vector<int16_t> img(
                            images.begin() + imgIdx * ROWS * COLS,
                            images.begin() + (imgIdx + 1) * ROWS * COLS
                        );
                        sendImageSPI(spi_fd, img);
                    }
                }
                continue;
            }

            if (!inImage) continue;
            std::stringstream ss(line);
            int val;
            while (ss >> val && images.size() < 2 * ROWS * COLS)
                images.push_back((int16_t)val);
        }
    }
}

// --------- PRINT STEREO DISPARITY MEASUREMENTS -----------------
void printStereoDisparityGrid(const Stereo& ST)
{
    constexpr int BLOCKSPERCOL = 3;
    constexpr int BLOCKSPERROW = 11;

    std::cout << "\nStereo Disparity (DN), blocks "
              << BLOCKSPERCOL << " x " << BLOCKSPERROW << "\n";

    for (int br = 0; br < BLOCKSPERCOL; ++br) {
        std::cout << "Row " << br << ": ";
        for (int bc = 0; bc < BLOCKSPERROW; ++bc) {
            int b = br * BLOCKSPERROW + bc;

            // Valid if calibration bit + most confidence bits passed
            bool valid = (ST.m_Bvalid[b] & 0x80);

            if (valid) {
                std::cout << std::setw(7)
                          << std::fixed << std::setprecision(2)
                          << ST.m_BDN[b] << " ";
            } else {
                std::cout << "   ---- ";
            }
        }
        std::cout << "\n";
    }

    std::cout << std::flush;
}

void stdinThread(int serial_fd){
    std::string cmd;
    while (std::getline(std::cin, cmd)) {
        cmd += "\n";
        write(serial_fd, cmd.c_str(), cmd.size());
    }
}

// ============================================================
// MAIN
// ============================================================

int main() {
    auto ports = findACMports();
    if (ports.empty()) {
        std::cerr << "No ttyACM devices found\n";
        return 1;
    }

    int serial_fd = open(ports[0].c_str(), O_RDWR | O_NOCTTY);
    if (serial_fd < 0 || configureSerial(serial_fd) < 0)
        return 1;

    int spi_fd = openSPI();
    if (spi_fd < 0)
        return 1;

    std::thread reader(readThread, serial_fd, spi_fd);
    std::thread stdinReader(stdinThread, serial_fd);

    cv::namedWindow("Stereo", cv::WINDOW_NORMAL);
    cv::resizeWindow("Stereo", 1200, 600);

    Stereo ST;
    ST.Initialize();
    StereoSetup_RC50(ST);
    ST.InitDummyCalibration();

    while (running.load()) {
        if (newFrame.load()) {
            std::vector<int16_t> localCopy;
            {
                std::lock_guard<std::mutex> lock(frameMutex);
                localCopy = stereoFrame;
                newFrame.store(false);
            }

            short* left  = localCopy.data();
            short* right = localCopy.data() + ROWS * COLS;
            ST.LinkImageArrays(right, left);

            // ---- stereo runs here ----
            ST.StereoBlockMatchBank(-1);        // TODO: argument not used
            
            printStereoDisparityGrid(ST);

            cv::Mat disp = makeStereoDisplay(localCopy);
            cv::bitwise_not(disp, disp);
            cv::resize(disp, disp, {}, 6.0, 6.0, cv::INTER_NEAREST);
            cv::imshow("Stereo", disp);
        }

        if (cv::waitKey(1) == 'q')
            break;
    }

    running.store(false);
    reader.join();
    close(serial_fd);
    close(spi_fd);
    return 0;
}
