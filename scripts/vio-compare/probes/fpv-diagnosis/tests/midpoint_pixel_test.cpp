// SPDX-License-Identifier: GPL-3.0-or-later
#include <svo/common/pixel_residual.h>
#include <svo/common/frame.h>
#include <svo/schur_vins.h>
#include <vikit/cameras/ncamera.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

void testCamera(const svo::Camera& camera) {
    for (double fraction : {0.2, 0.5, 0.8}) {
        const Eigen::Vector2d pixel(camera.imageWidth()*fraction, camera.imageHeight()*fraction);
        Eigen::Vector3d point, observed;
        require(camera.backProject3(pixel, &point), "back projection failed");
        require(camera.backProject3(pixel+Eigen::Vector2d(0.7,-0.5), &observed), "observation back projection failed");
        point *= 3.0;
        Eigen::Vector2d residual;
        Eigen::Matrix<double,2,3> jacobian;
        require(svo::pixelResidual(camera, observed, point, residual, &jacobian), "pixel residual failed");
        require((residual-Eigen::Vector2d(0.7,-0.5)).norm()<0.01, "pixel noise depends on image radius");
        for (int axis=0; axis<3; ++axis) {
            Eigen::Vector3d plus=point, minus=point;
            plus[axis]+=1e-6;
            minus[axis]-=1e-6;
            Eigen::Vector2d rp, rm;
            require(svo::pixelResidual(camera, observed, plus, rp) &&
                    svo::pixelResidual(camera, observed, minus, rm), "finite difference projection failed");
            require(((rp-rm)/(2e-6)+jacobian.col(axis)).norm()<1e-4, "pixel Jacobian mismatch");
        }
        require(!svo::pixelResidual(camera, observed, Eigen::Vector3d(0,0,-1), residual), "behind-camera point accepted");
    }
}

void testPropagation() {
    // Compare with analytic angular/acceleration ramps, ending between samples.
    // The original normalized small-angle rotation is retained: its error must
    // converge quadratically when the sampling interval is halved.
    setenv("SCHUR_IMU_MIDPOINT", "1", 1);
    const double duration=0.9973, start=10.0, angular_accel=16.0, jerk=2.0;
    double previous_rotation_error=0;
    for (double step : {0.005, 0.0025}) {
        for (bool rotate : {true, false}) {
            schur_vins::SchurVINS estimator;
            estimator.InitCov();
            estimator.InitImuModel(0.01, 0.001, 0.001, 0.0001);
            const svo::Transformation identity(Eigen::Quaterniond::Identity(), Eigen::Vector3d::Zero());
            svo::FramePtr frame(new svo::Frame(0, int64_t((start+duration)*1e9), nullptr, identity));
            frame->T_cam_body_=identity;
            frame->T_body_cam_=identity;
            svo::FrameBundle::Ptr bundle(new svo::FrameBundle({frame}));
            for (int i=0; i<=int(std::ceil(duration/step)); ++i) {
                const double t=i*step;
                bundle->imu_datas_.emplace_back(start+t,
                    Eigen::Vector3d(0,0,rotate ? angular_accel*t : 0),
                    Eigen::Vector3d(rotate ? 0 : jerk*t,0,9.81007));
            }
            estimator.Forward(bundle);
            schur_vins::SchurVINS::ImuSnapshot state;
            require(estimator.getImuSnapshot(state), "propagation produced no state");
            require(std::abs(state.timestamp-start-duration)<1e-10, "camera boundary timestamp mismatch");
            if (rotate) {
                const Eigen::Quaterniond expected(Eigen::AngleAxisd(
                    0.5*angular_accel*duration*duration, Eigen::Vector3d::UnitZ()));
                const double error=state.orientation.angularDistance(expected);
                require(error<0.003, "angular ramp has integration time lag");
                if (previous_rotation_error>0)
                    require(error<0.27*previous_rotation_error, "rotation error does not converge quadratically");
                previous_rotation_error=error;
                require(state.velocity.norm()<1e-10 && state.position.norm()<1e-10, "yaw rotation generated translation");
            } else {
                require((state.velocity-Eigen::Vector3d(0.5*jerk*duration*duration,0,0)).norm()<1e-9,
                        "acceleration ramp velocity mismatch");
                require((state.position-Eigen::Vector3d(jerk*duration*duration*duration/6,0,0)).norm()<1e-5,
                        "acceleration ramp position mismatch");
            }
        }
    }
    unsetenv("SCHUR_IMU_MIDPOINT");
}
}

int main(int argc, char** argv) {
    try {
        testPropagation();
        for (int i=1; i<argc; ++i) {
            auto rig=svo::CameraBundle::loadFromYaml(argv[i]);
            require(bool(rig), "camera calibration load failed");
            for (size_t camera=0; camera<rig->numCameras(); ++camera) testCamera(rig->getCamera(camera));
        }
        std::cout << "midpoint and pixel residual tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
