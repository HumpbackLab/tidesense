/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                          License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000-2008, Intel Corporation, all rights reserved.
// Copyright (C) 2009, Willow Garage Inc., all rights reserved.
// Copyright (C) 2013, OpenCV Foundation, all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of the copyright holders may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

// TideSense: optional double QR nullspace; OpenCV 3.4 registration remains unchanged.
#include "openvins_frontend_config.h"
#include "qr_fundamental.hpp"
#if OPENVINS_FRONT_QR7POINT
#include <opencv2/calib3d.hpp>
#include <opencv2/core/version.hpp>
#include <Eigen/QR>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
static_assert(CV_VERSION_MAJOR==3 && CV_VERSION_MINOR==4,"QR7 callback ABI is pinned to OpenCV 3.4");
// Verbatim OpenCV 3.4 private registration interface, linked to this SDK's static library.
namespace cv {
class CV_EXPORTS PointSetRegistrator : public Algorithm
{
public:
    class CV_EXPORTS Callback
    {
    public:
        virtual ~Callback() {}
        virtual int runKernel(InputArray m1, InputArray m2, OutputArray model) const = 0;
        virtual void computeError(InputArray m1, InputArray m2, InputArray model, OutputArray err) const = 0;
        virtual bool checkSubset(InputArray, InputArray, int) const { return true; }
    };

    virtual void setCallback(const Ptr<PointSetRegistrator::Callback>& cb) = 0;
    virtual bool run(InputArray m1, InputArray m2, OutputArray model, OutputArray mask) const = 0;
};

CV_EXPORTS Ptr<PointSetRegistrator> createRANSACPointSetRegistrator(const Ptr<PointSetRegistrator::Callback>& cb,
                                                                    int modelPoints, double threshold,
                                                                    double confidence=0.99, int maxIters=1000 );

CV_EXPORTS Ptr<PointSetRegistrator> createLMeDSPointSetRegistrator(const Ptr<PointSetRegistrator::Callback>& cb,
                                                                   int modelPoints, double confidence=0.99, int maxIters=1000 );

}
namespace ov_core { namespace qr7 { namespace {
using namespace cv;
struct Counters {
    unsigned long long calls=0,kernels=0,qr=0,rank=0,residual=0,polynomial=0,model=0,forced=0,small=0;
    ~Counters(){if(calls || kernels)std::fprintf(stderr,"QR7 calls=%llu kernels=%llu qr=%llu fallback_rank=%llu fallback_residual=%llu fallback_polynomial=%llu fallback_model=%llu forced_svd=%llu small_sets=%llu\n",calls,kernels,qr,rank,residual,polynomial,model,forced,small);}
};
thread_local Counters counters;
bool force_svd(){static const bool value=[](){const char *p=std::getenv("OPENVINS_QR7POINT_FORCE_SVD");return p && !std::strcmp(p,"1");}();return value;}
int fallback(const Mat& a,const Mat& b,Mat& out,unsigned long long &reason){
    ++reason;Mat model=cv::findFundamentalMat(a,b,FM_7POINT);
    if(model.empty()){out.release();return 0;}model.copyTo(out);return model.rows/3;
}
static inline bool haveCollinearPoints( const Mat& m, int count )
{
    int j, k, i = count-1;
    const Point2f* ptr = m.ptr<Point2f>();

    // check that the i-th selected point does not belong
    // to a line connecting some previously selected points
    // also checks that points are not too close to each other
    for( j = 0; j < i; j++ )
    {
        double dx1 = ptr[j].x - ptr[i].x;
        double dy1 = ptr[j].y - ptr[i].y;
        for( k = 0; k < j; k++ )
        {
            double dx2 = ptr[k].x - ptr[i].x;
            double dy2 = ptr[k].y - ptr[i].y;
            if( fabs(dx2*dy1 - dy2*dx1) <= FLT_EPSILON*(fabs(dx1) + fabs(dy1) + fabs(dx2) + fabs(dy2)))
                return true;
        }
    }
    return false;
}

static int run7PointQr( const Mat& _m1, const Mat& _m2, Mat& _fmatrix )
{
    ++counters.kernels;
    double a[7*9], v[9*9], c[4], r[3] = {0};
    double* f1, *f2;
    double t0, t1, t2;
    Mat coeffs( 1, 4, CV_64F, c );
    Mat roots( 1, 3, CV_64F, r );
    const Point2f* m1 = _m1.ptr<Point2f>();
    const Point2f* m2 = _m2.ptr<Point2f>();
    double* fmatrix = _fmatrix.ptr<double>();
    int i, k, n;

    Point2d m1c(0, 0), m2c(0, 0);
    double t, scale1 = 0, scale2 = 0;
    const int count = 7;

    // compute centers and average distances for each of the two point sets
    for( i = 0; i < count; i++ )
    {
        m1c += Point2d(m1[i]);
        m2c += Point2d(m2[i]);
    }

    // calculate the normalizing transformations for each of the point sets:
    // after the transformation each set will have the mass center at the coordinate origin
    // and the average distance from the origin will be ~sqrt(2).
    t = 1./count;
    m1c *= t;
    m2c *= t;

    for( i = 0; i < count; i++ )
    {
        scale1 += norm(Point2d(m1[i].x - m1c.x, m1[i].y - m1c.y));
        scale2 += norm(Point2d(m2[i].x - m2c.x, m2[i].y - m2c.y));
    }

    scale1 *= t;
    scale2 *= t;

    if( scale1 < FLT_EPSILON || scale2 < FLT_EPSILON )
        return 0;

    scale1 = std::sqrt(2.)/scale1;
    scale2 = std::sqrt(2.)/scale2;

    // form a linear system: i-th row of A(=a) represents
    // the equation: (m2[i], 1)'*F*(m1[i], 1) = 0
    for( i = 0; i < 7; i++ )
    {
        double x0 = (m1[i].x - m1c.x)*scale1;
        double y0 = (m1[i].y - m1c.y)*scale1;
        double x1 = (m2[i].x - m2c.x)*scale2;
        double y1 = (m2[i].y - m2c.y)*scale2;

        a[i*9+0] = x1*x0;
        a[i*9+1] = x1*y0;
        a[i*9+2] = x1;
        a[i*9+3] = y1*x0;
        a[i*9+4] = y1*y0;
        a[i*9+5] = y1;
        a[i*9+6] = x0;
        a[i*9+7] = y0;
        a[i*9+8] = 1;
    }

    // A*(f11 f12 ... f33)' = 0 is singular (7 equations for 9 variables), so
    // the solution is linear subspace of dimensionality 2.
    // => use the last two singular vectors as a basis of the space
    // (according to SVD properties)
    if(force_svd())return fallback(_m1,_m2,_fmatrix,counters.forced);
    using Design=Eigen::Matrix<double,7,9,Eigen::RowMajor>;
    Eigen::Map<const Design> design(a);
    Eigen::Matrix<double,9,7> transposed=design.transpose();
    Eigen::HouseholderQR<Eigen::Matrix<double,9,7>> qr(transposed);
    double largest=0,smallest=std::numeric_limits<double>::infinity();
    for(int d=0;d<7;++d){double value=std::abs(qr.matrixQR()(d,d));largest=std::max(largest,value);smallest=std::min(smallest,value);}
    if(!qr.matrixQR().allFinite() || !(largest>0) || smallest<=largest*1e-6)
        return fallback(_m1,_m2,_fmatrix,counters.rank);
    Eigen::Matrix<double,9,2> select=Eigen::Matrix<double,9,2>::Zero();select(7,0)=select(8,1)=1;
    Eigen::Matrix<double,9,2> basis=qr.householderQ()*select;
    double design_norm=design.norm();
    if(!basis.allFinite() || (design*basis).norm()>1e-10*design_norm ||
       (basis.transpose()*basis-Eigen::Matrix2d::Identity()).norm()>1e-10)
        return fallback(_m1,_m2,_fmatrix,counters.residual);
    // OpenCV initializes its two null vectors by projecting deterministic random
    // vectors (seed 0x12345678) onto the orthogonal complement. Match that basis
    // orientation so the cubic's model order stays close to the original SVD.
    Eigen::Matrix<double,9,2> canonical;
    cv::RNG orientation_rng(0x12345678);
    for(int column=0;column<2;++column){
        Eigen::Matrix<double,9,1> seed;
        for(int row=0;row<9;++row)seed(row)=(orientation_rng.next()&256)?1./9:-1./9;
        Eigen::Matrix<double,9,1> direction=basis*(basis.transpose()*seed);
        if(column){double dot=0;for(int row=0;row<9;++row)dot+=canonical(row,0)*direction(row);
            for(int row=0;row<9;++row)direction(row)-=dot*canonical(row,0);}
        double squared=0;for(int row=0;row<9;++row)squared+=direction(row)*direction(row);
        if(!std::isfinite(squared) || squared<1e-16)return fallback(_m1,_m2,_fmatrix,counters.residual);
        canonical.col(column)=direction/std::sqrt(squared);
    }
    for(int d=0;d<9;++d){v[7*9+d]=canonical(d,0);v[8*9+d]=canonical(d,1);}
    f1 = v + 7*9;
    f2 = v + 8*9;

    // f1, f2 is a basis => lambda*f1 + mu*f2 is an arbitrary fundamental matrix,
    // as it is determined up to a scale, normalize lambda & mu (lambda + mu = 1),
    // so f ~ lambda*f1 + (1 - lambda)*f2.
    // use the additional constraint det(f) = det(lambda*f1 + (1-lambda)*f2) to find lambda.
    // it will be a cubic equation.
    // find c - polynomial coefficients.
    for( i = 0; i < 9; i++ )
        f1[i] -= f2[i];

    t0 = f2[4]*f2[8] - f2[5]*f2[7];
    t1 = f2[3]*f2[8] - f2[5]*f2[6];
    t2 = f2[3]*f2[7] - f2[4]*f2[6];

    c[3] = f2[0]*t0 - f2[1]*t1 + f2[2]*t2;

    c[2] = f1[0]*t0 - f1[1]*t1 + f1[2]*t2 -
    f1[3]*(f2[1]*f2[8] - f2[2]*f2[7]) +
    f1[4]*(f2[0]*f2[8] - f2[2]*f2[6]) -
    f1[5]*(f2[0]*f2[7] - f2[1]*f2[6]) +
    f1[6]*(f2[1]*f2[5] - f2[2]*f2[4]) -
    f1[7]*(f2[0]*f2[5] - f2[2]*f2[3]) +
    f1[8]*(f2[0]*f2[4] - f2[1]*f2[3]);

    t0 = f1[4]*f1[8] - f1[5]*f1[7];
    t1 = f1[3]*f1[8] - f1[5]*f1[6];
    t2 = f1[3]*f1[7] - f1[4]*f1[6];

    c[1] = f2[0]*t0 - f2[1]*t1 + f2[2]*t2 -
    f2[3]*(f1[1]*f1[8] - f1[2]*f1[7]) +
    f2[4]*(f1[0]*f1[8] - f1[2]*f1[6]) -
    f2[5]*(f1[0]*f1[7] - f1[1]*f1[6]) +
    f2[6]*(f1[1]*f1[5] - f1[2]*f1[4]) -
    f2[7]*(f1[0]*f1[5] - f1[2]*f1[3]) +
    f2[8]*(f1[0]*f1[4] - f1[1]*f1[3]);

    c[0] = f1[0]*t0 - f1[1]*t1 + f1[2]*t2;

    double coeff_scale=0;
    for(double value:c){if(!std::isfinite(value))return fallback(_m1,_m2,_fmatrix,counters.polynomial);coeff_scale=std::max(coeff_scale,std::abs(value));}
    // Near an infinite projective root, use the original SVD parametrization.
    if(!(coeff_scale>0) || std::abs(c[0])<=coeff_scale*1e-10)
        return fallback(_m1,_m2,_fmatrix,counters.polynomial);
    // solve the cubic equation; there can be 1 to 3 roots ...
    n = solveCubic( coeffs, roots );

    if(n<1 || n>3)return fallback(_m1,_m2,_fmatrix,counters.polynomial);

    // transformation matrices
    Matx33d T1( scale1, 0, -scale1*m1c.x, 0, scale1, -scale1*m1c.y, 0, 0, 1 );
    Matx33d T2( scale2, 0, -scale2*m2c.x, 0, scale2, -scale2*m2c.y, 0, 0, 1 );

    for( k = 0; k < n; k++, fmatrix += 9 )
    {
        // for each root form the fundamental matrix
        if(!std::isfinite(r[k]) || std::abs(r[k])>1e8)
            return fallback(_m1,_m2,_fmatrix,counters.polynomial);
        double lambda = r[k], mu = 1.;
        double s = f1[8]*r[k] + f2[8];

        // normalize each matrix, so that F(3,3) (~fmatrix[8]) == 1
        if( fabs(s) > DBL_EPSILON )
        {
            mu = 1./s;
            lambda *= mu;
            fmatrix[8] = 1.;
        }
        else
            fmatrix[8] = 0.;

        for( i = 0; i < 8; i++ )
            fmatrix[i] = f1[i]*lambda + f2[i]*mu;

        Eigen::Map<const Eigen::Matrix<double,9,1>> fvec(fmatrix);
        double f_norm=fvec.norm();
        if(!fvec.allFinite() || !(f_norm>0) || (design*fvec).norm()>1e-8*design_norm*f_norm)
            return fallback(_m1,_m2,_fmatrix,counters.model);
        // de-normalize
        Mat F(3, 3, CV_64F, fmatrix);
        F = T2.t() * F * T1;

        // make F(3,3) = 1
        if(fabs(F.at<double>(8)) > FLT_EPSILON )
            F *= 1. / F.at<double>(8);
    }

    ++counters.qr;
    return n;
}

class Callback final:public cv::PointSetRegistrator::Callback {
public:
    bool checkSubset(InputArray a,InputArray b,int count)const override {
        return !haveCollinearPoints(a.getMat(),count) && !haveCollinearPoints(b.getMat(),count);
    }
    int runKernel(InputArray a,InputArray b,OutputArray out)const override {
        double storage[27];Mat models(9,3,CV_64F,storage);
        int n=run7PointQr(a.getMat(),b.getMat(),models);
        if(n<=0)out.release();else models.rowRange(0,n*3).copyTo(out);
        return n;
    }
    void computeError( InputArray _m1, InputArray _m2, InputArray _model, OutputArray _err ) const CV_OVERRIDE
    {
        Mat __m1 = _m1.getMat(), __m2 = _m2.getMat(), __model = _model.getMat();
        int i, count = __m1.checkVector(2);
        const Point2f* m1 = __m1.ptr<Point2f>();
        const Point2f* m2 = __m2.ptr<Point2f>();
        const double* F = __model.ptr<double>();
        _err.create(count, 1, CV_32F);
        float* err = _err.getMat().ptr<float>();

        for( i = 0; i < count; i++ )
        {
            double a, b, c, d1, d2, s1, s2;

            a = F[0]*m1[i].x + F[1]*m1[i].y + F[2];
            b = F[3]*m1[i].x + F[4]*m1[i].y + F[5];
            c = F[6]*m1[i].x + F[7]*m1[i].y + F[8];

            s2 = 1./(a*a + b*b);
            d2 = m2[i].x*a + m2[i].y*b + c;

            a = F[0]*m2[i].x + F[3]*m2[i].y + F[6];
            b = F[1]*m2[i].x + F[4]*m2[i].y + F[7];
            c = F[2]*m2[i].x + F[5]*m2[i].y + F[8];

            s1 = 1./(a*a + b*b);
            d1 = m1[i].x*a + m1[i].y*b + c;

            err[i] = (float)std::max(d1*d1*s1, d2*d2*s2);
        }
    }
};
}
bool enabled(){static const bool value=[](){const char *p=std::getenv("OPENVINS_QR7POINT");return !p || std::strcmp(p,"0");}();return value;}
cv::Mat seven_point(const cv::Mat& a,const cv::Mat& b){
    if(!enabled())return cv::findFundamentalMat(a,b,cv::FM_7POINT);
    Callback cb;cv::Mat result;cb.runKernel(a,b,result);return result;
}
cv::Mat find(const std::vector<cv::Point2f>& a,const std::vector<cv::Point2f>& b,double threshold,double confidence,int cap,cv::OutputArray mask){
    if(!enabled())return cv::findFundamentalMat(a,b,cv::FM_RANSAC,threshold,confidence,cap,mask);
    ++counters.calls;
    CV_Assert(a.size()==b.size());
    // Preserve the original LMeDS choice for fewer than 15 correspondences.
    if(a.size()<15){++counters.small;return cv::findFundamentalMat(a,b,cv::FM_RANSAC,threshold,confidence,cap,mask);}
    cv::Mat m1,m2,F;
    cv::Mat(a).reshape(2,int(a.size())).convertTo(m1,CV_32F);
    cv::Mat(b).reshape(2,int(b.size())).convertTo(m2,CV_32F);
    if(threshold<=0)threshold=3;
    if(confidence<DBL_EPSILON || confidence>1-DBL_EPSILON)confidence=.99;
    auto cb=cv::makePtr<Callback>();
    if(!cv::createRANSACPointSetRegistrator(cb,7,threshold,confidence,cap)->run(m1,m2,F,mask))return cv::Mat();
    return F;
}
}}
#endif
