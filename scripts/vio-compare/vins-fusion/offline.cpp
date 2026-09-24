#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "VINS-Fusion/vins_estimator/src/estimator/estimator.h"
#include "VINS-Fusion/vins_estimator/src/utility/visualization.h"

struct Imu { double t; Eigen::Vector3d acc, gyr; };
static std::ofstream poses;
static unsigned long pose_count = 0;
static unsigned long frame_count = 0;

void pubLatestOdometry(const Eigen::Vector3d&, const Eigen::Quaterniond&, const Eigen::Vector3d&, double) {}
void pubTrackImage(const cv::Mat&, double) {}
void printStatistics(const Estimator&, double) {}
void pubKeyPoses(const Estimator&, const std_msgs::Header&) {}
void pubCameraPose(const Estimator&, const std_msgs::Header&) {}
void pubPointCloud(const Estimator&, const std_msgs::Header&) {}
void pubKeyframe(const Estimator&) {}
void pubTF(const Estimator&, const std_msgs::Header&) {}
void pubOdometry(const Estimator& e, const std_msgs::Header& h) {
    if (e.solver_flag != Estimator::NON_LINEAR) return;
    const int k = e.frame_count;
    Eigen::Quaterniond q(e.Rs[k]);
    poses << std::fixed << std::setprecision(9) << h.stamp.toSec() << ',' << frame_count << ','
          << e.Ps[k].x() << ',' << e.Ps[k].y() << ',' << e.Ps[k].z() << ','
          << q.x() << ',' << q.y() << ',' << q.z() << ',' << q.w() << '\n';
    ++pose_count;
}

static std::vector<Imu> readImu(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::vector<Imu> result;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream s(line);
        long long ns;
        double gx, gy, gz, ax, ay, az;
        if (!(s >> ns >> gx >> gy >> gz >> ax >> ay >> az)) throw std::runtime_error("bad imu.csv row");
        result.push_back({ns * 1e-9, {ax, ay, az}, {gx, gy, gz}});
    }
    return result;
}

static cv::Mat readPgm(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::string magic;
    int width, height, maxval;
    f >> magic >> width >> height >> maxval;
    if (magic != "P5" || width != 640 || height != 400 || maxval != 255)
        throw std::runtime_error("unexpected PGM: " + path);
    f.get();
    cv::Mat img(height, width, CV_8UC1);
    f.read(reinterpret_cast<char*>(img.data), img.total());
    if (!f) throw std::runtime_error("short PGM: " + path);
    return img;
}

int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) {
        std::cerr << "usage: vins_fusion_offline fusion.yaml recording_dir output_dir [rotate-right]\n";
        return 2;
    }
    try {
        const std::string recording = argv[2], output = argv[3];
        const bool rotate_right = argc == 5 && std::string(argv[4]) == "rotate-right";
        poses.open(output + "/trajectory.csv");
        poses << "timestamp_s,frame,x,y,z,qx,qy,qz,qw\n";
        readParameters(argv[1]);
        static Estimator estimator;
        estimator.setParameter();
        auto imu = readImu(recording + "/imu.csv");
        std::ifstream frames(recording + "/stereo.csv");
        if (!frames) throw std::runtime_error("cannot open stereo.csv");
        std::string line;
        size_t ii = 0;
        const auto begin = std::chrono::steady_clock::now();
        while (std::getline(frames, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto a = line.find(','), b = line.find(',', a + 1);
            if (a == std::string::npos || b == std::string::npos) throw std::runtime_error("bad stereo.csv row");
            const long long ns = std::stoll(line.substr(0, a));
            const double t = ns * 1e-9;
            while (ii < imu.size() && imu[ii].t <= t + 0.006) {
                estimator.inputIMU(imu[ii].t, imu[ii].acc, imu[ii].gyr);
                ++ii;
            }
            if (ii == imu.size() && imu.back().t < t) break;
            auto img0 = readPgm(recording + "/" + line.substr(a + 1, b - a - 1));
            auto img1 = readPgm(recording + "/" + line.substr(b + 1));
            if (rotate_right) cv::flip(img1, img1, -1);
            ++frame_count;
            estimator.inputImage(t, img0, img1);
            if (frame_count % 25 == 0) {
                const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
                std::cerr << "progress frame=" << frame_count << " imu=" << ii
                          << " poses=" << pose_count
                          << " left_features=" << estimator.featureTracker.ids.size()
                          << " stereo_features=" << estimator.featureTracker.ids_right.size()
                          << " elapsed_s=" << elapsed << '\n';
            }
        }
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        std::cerr << "done frames=" << frame_count << " imu=" << ii << " poses=" << pose_count
                  << " elapsed_s=" << elapsed << '\n';
        return frame_count == 744 ? 0 : 3;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return 1;
    }
}
