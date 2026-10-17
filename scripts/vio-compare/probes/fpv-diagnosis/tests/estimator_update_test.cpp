// SPDX-License-Identifier: GPL-3.0-or-later
#include <svo/stable_update.h>
#include <svo/common/pixel_residual.h>
#include <svo/common/frame.h>
#include <vikit/cameras/ncamera.h>
#include <Eigen/QR>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
Eigen::MatrixXd gaussian(int rows, int cols) {
    static std::mt19937 generator(42);
    std::normal_distribution<double> distribution;
    Eigen::MatrixXd result(rows, cols);
    for (int i = 0; i < result.size(); ++i) result.data()[i] = distribution(generator);
    return result;
}
void testSchur() {
    const Eigen::MatrixXd Q = gaussian(24, 24).householderQr().householderQ();
    const Eigen::MatrixXd A = gaussian(60, 18) * Q.leftCols(18).transpose();
    const Eigen::VectorXd error = gaussian(60, 1);
    const Eigen::MatrixXd H = A.transpose() * A;
    const Eigen::VectorXd g = A.transpose() * error;
    Eigen::MatrixXd j;
    Eigen::VectorXd r;
    require(schur_vins::whitenSchurMeasurement(H, g, j, r), "Schur whitening failed");
    require(j.rows() == 18, "Schur gauge rank changed");
    require((j.transpose()*j-H).norm() < 1e-9*H.norm(), "Schur information changed");
    require((j.transpose()*r-g).norm() < 1e-9*g.norm(), "Schur gradient changed");
    require((j*Q.rightCols(6)).norm() < 1e-9, "gauge gained spurious information");

    // A singular prior models exact pose cloning; only innovation must be SPD.
    const Eigen::MatrixXd L = gaussian(39, 25) * 0.01;
    const Eigen::MatrixXd prior = L * L.transpose();
    Eigen::MatrixXd J = Eigen::MatrixXd::Zero(18,39), raw = Eigen::MatrixXd::Zero(60,39);
    J.rightCols(24) = j;
    raw.rightCols(24) = A;
    auto update = [&](const Eigen::MatrixXd& measurement, const Eigen::VectorXd& residual) {
        const Eigen::MatrixXd ph = prior*measurement.transpose();
        const Eigen::MatrixXd s = measurement*ph + Eigen::MatrixXd::Identity(measurement.rows(),measurement.rows());
        const Eigen::MatrixXd k = s.llt().solve(ph.transpose()).transpose();
        const Eigen::MatrixXd ikj = Eigen::MatrixXd::Identity(39,39)-k*measurement;
        const Eigen::MatrixXd post = ikj*prior*ikj.transpose()+k*k.transpose();
        Eigen::MatrixXd result(39,40);
        result.leftCols(39) = post;
        result.rightCols(1) = k*residual;
        return result;
    };
    const Eigen::MatrixXd compressed = update(J,r), direct = update(raw,error);
    require((compressed-direct).norm() < 1e-9, "whitened Kalman update differs from raw measurements");
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eigen(compressed.leftCols(39));
    require(eigen.eigenvalues().minCoeff() > -1e-12, "posterior covariance is not PSD");
    require(!schur_vins::whitenSchurMeasurement(Eigen::MatrixXd::Zero(24,24),g,j,r), "zero information accepted");
    Eigen::MatrixXd invalid = H; invalid(0,0) = std::numeric_limits<double>::quiet_NaN();
    require(!schur_vins::whitenSchurMeasurement(invalid,g,j,r), "nonfinite information accepted");
}
void testCamera(const svo::Camera& camera) {
    for (double fraction : {0.2, 0.5, 0.8}) {
        const Eigen::Vector2d pixel(camera.imageWidth()*fraction, camera.imageHeight()*fraction);
        Eigen::Vector3d point, observed;
        require(camera.backProject3(pixel,&point), "back projection failed");
        require(camera.backProject3(pixel+Eigen::Vector2d(0.7,-0.5),&observed), "observation back projection failed");
        point *= 3.0;
        Eigen::Vector2d residual;
        Eigen::Matrix<double,2,3> jacobian;
        require(svo::pixelResidual(camera,observed,point,residual,&jacobian), "pixel residual failed");
        require((residual-Eigen::Vector2d(0.7,-0.5)).norm() < 0.01, "pixel noise depends on image radius");
        for (int axis=0;axis<3;++axis) {
            Eigen::Vector3d plus=point, minus=point;
            plus[axis]+=1e-6;minus[axis]-=1e-6;
            Eigen::Vector2d rp,rm;
            require(svo::pixelResidual(camera,observed,plus,rp) && svo::pixelResidual(camera,observed,minus,rm), "finite difference projection failed");
            require(((rp-rm)/(2e-6)+jacobian.col(axis)).norm()<1e-4, "pixel Jacobian finite difference mismatch");
        }
        require(!svo::pixelResidual(camera,observed,Eigen::Vector3d(0,0,-1),residual), "behind-camera point accepted");
    }
}
}
int main(int argc,char** argv) {
    try {
        testSchur();
        for (int i=1;i<argc;++i) {
            auto rig=svo::CameraBundle::loadFromYaml(argv[i]);
            require(bool(rig),"camera calibration load failed");
            for (size_t c=0;c<rig->numCameras();++c) testCamera(rig->getCamera(c));
        }
        std::cout << "estimator update tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';return 1;
    }
}
