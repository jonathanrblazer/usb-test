void displayImage(const int16_t* img,
                  int rows,
                  int cols,
                  const std::string& winName)
{
    // Wrap raw buffer (NO copy)
    cv::Mat img16(rows, cols, CV_16SC1, (void*)img);

    // Convert to 8-bit for display
    cv::Mat img8;
    cv::normalize(img16, img8, 0, 255, cv::NORM_MINMAX);
    img8.convertTo(img8, CV_8UC1);

    // Show image
    cv::imshow(winName, img8);
    cv::waitKey(1);  // non-blocking
}

cv::Mat makeStereoDisplay(const std::vector<int16_t>& stereo)
{
    // Wrap raw buffers (no copy)
    cv::Mat left16(ROWS, COLS, CV_16SC1,
                   (void*)(stereo.data()));
    cv::Mat right16(ROWS, COLS, CV_16SC1,
                    (void*)(stereo.data() + ROWS * COLS));

    // Convert both to 8-bit
    cv::Mat left8, right8;
    cv::normalize(left16, left8, 0, 255, cv::NORM_MINMAX);
    cv::normalize(right16, right8, 0, 255, cv::NORM_MINMAX);
    left8.convertTo(left8, CV_8UC1);
    right8.convertTo(right8, CV_8UC1);

    // Concatenate side-by-side
    cv::Mat stereo8;
    cv::hconcat(left8, right8, stereo8);

    return stereo8;
}
