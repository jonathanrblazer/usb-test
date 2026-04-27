# Stereo Disparity Demo (CM5 + OpenCV)

This project demonstrates real-time stereo disparity visualization running on the CM5.

---

## ⚙️ Current Configuration

- **SPI chunk limit:** `4096 bytes`
- Target device: CM5
- Display: OpenCV window (via VNC or local execution)

---

## 🚀 How to Run

1. **Connect via VNC**
   - Open VNC viewer
   - Connect to:
     ```
     192.168.8.159
     ```
2. Build with
    - make clean
    - make
    - executable is: StereoOpenCV2

3. **Launch the Application**
   - Run from within VNC **or** directly on the CM5

4. **Enable Debug Mode**
    - ONE-SHOT: debugmode 106;
    - STREAMING: debugmode 107;


5. **Expected Result**
- An OpenCV window appears
- Displays image with **3×11 disparity overlay**

---

## 🧠 What You Should See

- Live image feed
- Overlayed disparity measurements (grid: 3 rows × 11 columns)
- Useful for quick validation of stereo pipeline

---

## 📅 TODO (as of 12/28/2025)

- [ ] Perform proper camera calibration
- [ ] Transmit disparity data over SPI
- [ ] Add profiling & timing analysis  
   - Compare CM5 vs Teensy performance
- [ ] Switch image transport to binary format  
   (`raw int16_t buffer`)

---

## 💡 Notes

- Debug mode `106` is required to enable disparity visualization
- Current implementation prioritizes visibility/debugging over performance
- SPI integration and optimization are still in progress

---

## 📌 Future Direction

Focus areas moving forward:
- Accuracy (calibration)
- Performance (profiling + optimization)
- Data pipeline (binary transport + SPI integration)

---
