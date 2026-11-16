// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <opencv2/core.hpp>
#include <vector>
void schur_gaussian_cpu_q9(cv::Mat& parent,const std::vector<cv::Rect>& layers);
