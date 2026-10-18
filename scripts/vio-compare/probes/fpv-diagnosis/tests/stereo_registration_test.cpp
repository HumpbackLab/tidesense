#include <svo/schur_vins.h>
#include <svo/common/frame.h>
#include <svo/common/point.h>
#include <svo/common/feature_wrapper.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
svo::FrameBundle::Ptr makeFrame(int index) {
    using namespace svo;
    const Transformation identity(Eigen::Quaterniond::Identity(), Position::Zero());
    std::vector<FramePtr> frames;
    for (int camera = 0; camera < 2; ++camera) {
        FramePtr frame(new Frame(index * 2 + camera, 10000000000LL + index * 50000000LL, nullptr, identity));
        frame->setNFrameIndex(camera);
        frame->T_body_cam_ = identity;
        frame->T_cam_body_ = identity;
        frame->resizeFeatureStorage(16);
        frames.push_back(frame);
    }
    FrameBundle::Ptr bundle(new FrameBundle(frames));
    for (int sample = index ? 1 : 10; sample <= 10; ++sample)
        bundle->imu_datas_.emplace_back((10000000000LL + index * 50000000LL - (10 - sample) * 5000000LL) * 1e-9,
                                       Eigen::Vector3d::Zero(), Eigen::Vector3d(0, 0, 9.81007));
    return bundle;
}
void addStereoPoint(const svo::FrameBundle::Ptr& bundle, int index) {
    using namespace svo;
    auto point = std::make_shared<Point>(Position(index % 3, index / 3, 3));
    for (const auto& frame : bundle->frames_) {
        const size_t slot = frame->numFeatures();
        auto feature = frame->getEmptyFeatureWrapper();
        feature.type = FeatureType::kCorner;
        feature.f = point->pos().normalized();
        feature.px = Keypoint(20 + index, 30 + index);
        feature.level = 0;
        feature.landmark = point;
        feature.track_id = point->id();
        ++frame->num_features_;
        point->addObservation(frame, slot);
    }
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}
int main() {
    setenv("SCHUR_IMU_MIDPOINT", "1", 1);
    schur_vins::SchurVINS estimator;
    estimator.InitCov();
    estimator.InitMaxState(15);
    estimator.InitImuModel(.01, .001, .001, .0001);
    auto first = makeFrame(0);
    estimator.Forward(first);
    estimator.Backward(first);
    schur_vins::SchurVINS::ImuSnapshot before, after;
    require(estimator.getImuSnapshot(before), "missing inertial state");
    for (int i = 0; i < 8; ++i) addStereoPoint(first, i);
    require(estimator.RegisterCurrentFramePoints(first) == 16, "late stereo points not registered");
    addStereoPoint(first, 8);
    require(estimator.RegisterCurrentFramePoints(first) == 2, "incremental registration failed");
    for (int i = 0; i < 50; ++i)
        require(estimator.RegisterCurrentFramePoints(first) == 0, "duplicate observations registered");
    estimator.getImuSnapshot(after);
    require((before.position - after.position).norm() == 0 && (before.velocity - after.velocity).norm() == 0 &&
            before.orientation.angularDistance(after.orientation) < 1e-15, "registration changed inertial state");
    auto next = makeFrame(1);
    estimator.Forward(next);
    for (int i = 0; i < 8; ++i) addStereoPoint(next, i);
    require(estimator.RegisterCurrentFramePoints(next) == 0, "unregistered frame must not create a clone");
    require(estimator.RegisterCurrentFramePoints(first) == 0, "old frame registration was not idempotent");
    std::cout << "PASS: incremental stereo registration, duplicate prevention, state preservation and no implicit clone\n";
}
