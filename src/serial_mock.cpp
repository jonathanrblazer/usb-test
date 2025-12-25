#include "serial_mock.h"
#include <cmath>

std::vector<int16_t> generateDummyStereoFrame(int rows, int cols) {
    std::vector<int16_t> frame(2 * rows * cols);

    for (int img = 0; img < 2; ++img) {
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                int idx = img * rows * cols + r * cols + c;
                frame[idx] = static_cast<int16_t>(
                    100 * img + r + 5 * std::sin(c * 0.1)
                );
            }
        }
    }
    return frame;
}
