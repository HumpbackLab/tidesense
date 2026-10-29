#include "qr_fundamental.hpp"
#include <opencv2/calib3d.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
static cv::Mat normalize(const cv::Mat& f){cv::Mat out=f/cv::norm(f);int at=0;for(int i=1;i<9;++i)if(std::abs(out.at<double>(i))>std::abs(out.at<double>(at)))at=i;if(out.at<double>(at)<0)out=-out;return out;}
static double model_error(const cv::Mat& a,const cv::Mat& b){
    if(a.rows!=b.rows)return 1e9;double result=0;
    for(int i=0;i<a.rows;i+=3){double best=1e9;auto x=normalize(a.rowRange(i,i+3));
        for(int j=0;j<b.rows;j+=3)best=std::min(best,cv::norm(x-normalize(b.rowRange(j,j+3))));result=std::max(result,best);}
    return result;
}
int main(int argc,char **argv){
    bool force=argc>1 && std::string(argv[1])=="force";
    bool disabled=argc>1 && std::string(argv[1])=="off";
    if(force)setenv("OPENVINS_QR7POINT_FORCE_SVD","1",1);
    if(disabled)setenv("OPENVINS_QR7POINT","0",1);
    cv::setNumThreads(1);std::mt19937 rng(919);std::uniform_real_distribution<float> random(-1,1);
    unsigned kernels=0,ransac=0,degenerate=0;double worst=0,ordered_worst=0;
    for(unsigned sample=0;sample<1000;++sample){
        std::vector<cv::Point2f> a,b;
        for(int i=0;i<7;++i){a.emplace_back(random(rng),random(rng));b.emplace_back(random(rng),random(rng));}
        cv::Mat x=cv::findFundamentalMat(a,b,cv::FM_7POINT),y=ov_core::qr7::seven_point(cv::Mat(a),cv::Mat(b));
        if(x.empty()!=y.empty())throw std::runtime_error("kernel emptiness differs");
        if(!x.empty()){
            double error=model_error(x,y);worst=std::max(worst,error);
            if(x.rows==y.rows)for(int i=0;i<x.rows;i+=3)ordered_worst=std::max(ordered_worst,cv::norm(normalize(x.rowRange(i,i+3))-normalize(y.rowRange(i,i+3))));
            if(force || disabled){if(cv::norm(x-y,cv::NORM_INF)!=0)throw std::runtime_error("SVD control differs");}
            else if(error>1e-5){std::cerr<<"sample="<<sample<<" model error="<<error<<'\n';return 1;}
        }
        ++kernels;
    }
    for(bool planar:{false,true})for(int count:{10,14,15,75,150})for(int outliers:{0,10,30,50}){
        std::vector<cv::Point2f> a,b;unsigned expected=0;
        for(int i=0;i<count;++i){double x=random(rng),y=random(rng),z=planar?4:4+random(rng)*2;
            a.emplace_back(float(x/z),float(y/z));
            double px=.9887710779360422*x+.14943813247359922*z+.12,py=y+.02,pz=-.14943813247359922*x+.9887710779360422*z+.01;
            if(i*100/count<outliers)b.emplace_back(random(rng)*.5f,random(rng)*.5f);
            else {b.emplace_back(float(px/pz),float(py/pz));++expected;}
        }
        cv::Mat m,n;auto x=cv::findFundamentalMat(a,b,cv::FM_RANSAC,.003,.999,1000,m);auto y=ov_core::qr7::find(a,b,.003,.999,1000,n);
        if(force || disabled){
            if(x.empty()!=y.empty() || m.size()!=n.size() || cv::norm(m,n,cv::NORM_INF)!=0 || (!x.empty() && cv::norm(x-y,cv::NORM_INF)!=0))throw std::runtime_error("RANSAC SVD control differs");
        }else if(!planar && outliers<=30 && count>=75 && (y.empty() || cv::countNonZero(n)<int(expected)-2))throw std::runtime_error("lost clear inliers");
        ++ransac;
    }
    for(int type=0;type<3;++type){
        std::vector<cv::Point2f> a,b;for(int i=0;i<7;++i){float x=random(rng),y=random(rng);a.emplace_back(x,y);b.emplace_back(type==0?x*1.1f+.1f:x,type==0?y*.9f+.01f:y);}
        auto y=ov_core::qr7::seven_point(cv::Mat(a),cv::Mat(b));if(!y.empty() && !cv::checkRange(y))throw std::runtime_error("nonfinite degenerate result");++degenerate;
    }
    std::cout<<"PASS kernels="<<kernels<<" ransac_cases="<<ransac<<" degenerate_cases="<<degenerate<<" worst_model_set_error="<<worst<<" worst_ordered_model_error="<<ordered_worst<<'\n';
}
