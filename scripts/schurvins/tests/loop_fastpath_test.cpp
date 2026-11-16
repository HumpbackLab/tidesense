// SPDX-License-Identifier: GPL-3.0-or-later
#include <svo/online_loopclosing/loop_closing.h>
#include <svo/online_loopclosing/map_alignment.h>
#include <svo/common/camera.h>
#include <vikit/cameras/ncamera.h>
#include <opencv2/imgcodecs.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
svo::LoopClosureOptions options(const char* vocabulary,bool optimized) {
  svo::LoopClosureOptions o{};
  o.runlc=true;o.voc_name=vocabulary;o.alpha=o.beta=.8;o.ignored_past_frames=1;
  o.scale_ret_app="None";o.global_map_type="None";o.bowthresh=.65;
  o.gv_3d_inlier_thresh=.4;o.min_num_3d=10;o.orb_dist_thresh=48;o.gv_2d_match_thresh=.1;
  o.proximity_offset=1;o.proximity_dist_ratio=.01;
  o.reuse_orb=o.precheck_3d=optimized;
  return o;
}
svo::KeyFramePtr frame(int id,int points,const cv::Mat& image,const svo::CameraPtr& camera) {
  auto k=std::make_shared<svo::KeyFrame>(id);
  k->frame_id_=id;k->timestamp_sec_abs_=id*.1;k->keyframe_image_=image.clone();
  k->T_w_c_=svo::Transformation();
  for(int i=0;i<points;++i) {
    const cv::Point2f px(140+(i%5)*55,100+(i/5)*55);
    Eigen::Vector3d bearing;
    require(camera->backProject3(Eigen::Vector2d(px.x,px.y),&bearing),"back projection");
    bearing.normalize();const Eigen::Vector3d p=bearing*(3.0/bearing.z());
    k->svo_keypointsvector_.push_back(px);k->svo_bearingvectors_.push_back(bearing);
    k->svo_landmarksvector_cam_.emplace_back(p.x(),p.y(),p.z());
    k->svo_landmark_ids_.push_back(i);k->svo_depthsvector_.push_back(3);
    k->svo_trackIDsvector_.push_back(id*100+i);
    k->svo_featuretypevector_.push_back(svo::FeatureType::kCorner);
    k->svo_original_indexvec_.push_back(i);
  }
  return k;
}
void descriptors(const std::vector<cv::Mat>& a,const std::vector<cv::Mat>& b) {
  require(a.size()==b.size(),"descriptor count mismatch");
  for(size_t i=0;i<a.size();++i)require(a[i].size()==b[i].size() && a[i].type()==b[i].type() &&
      (a[i].empty() || cv::norm(a[i],b[i],cv::NORM_INF)==0),"descriptor bytes mismatch");
}
}
int main(int argc,char** argv) {
 try {
  if(argc!=4)throw std::runtime_error("usage: loop_fastpath_test CALIBRATION VOCABULARY IMAGE");
  auto rig=svo::CameraBundle::loadFromYaml(argv[1]);require(bool(rig),"camera rig");
  auto image=cv::imread(argv[3],cv::IMREAD_GRAYSCALE);require(!image.empty(),"image");
  // Exercise impossible, exact-boundary and recovery thresholds through the real pipeline.
  for(const auto& test:std::vector<std::pair<int,bool>>{{0,false},{9,false},{10,false},{6,true},{7,true}}) {
    const int points=test.first;const bool recovery=test.second;
    svo::LoopClosing reference(options(argv[2],false),rig),optimized(options(argv[2],true),rig);
    svo::MapAlignmentOptions alignment;alignment.ransac3d_min_pts=8;alignment.ransac3d_inlier_percent=40;
    reference.map_alignment_se3_=std::make_shared<svo::MapAlignmentSE3>(alignment);
    optimized.map_alignment_se3_=std::make_shared<svo::MapAlignmentSE3>(alignment);
    // Upstream starts historical queries at index 10; the 12th frame triggers a candidate.
    for(int i=0;i<12;++i) {
      reference.recovery_after_loss_=optimized.recovery_after_loss_=recovery && i==11;
      reference.addKeyframeToPR(frame(i,points,image,rig->getCameraShared(0)),true);
      optimized.addKeyframeToPR(frame(i,points,image,rig->getCameraShared(0)),true);
    }
    require(reference.kf_list_.size()==12 && optimized.kf_list_.size()==12,"admission mismatch");
    require(reference.num_queries_==optimized.num_queries_ &&
            reference.num_bow_candidates_==optimized.num_bow_candidates_,"retrieval candidate mismatch");
    for(size_t i=0;i<12;++i) {
      const auto& a=*reference.kf_list_[i];const auto& b=*optimized.kf_list_[i];
      require(a.bow_keypoints_==b.bow_keypoints_ && a.mixed_keypoints_==b.mixed_keypoints_,"pixel mismatch");
      require(a.vec_bow_==b.vec_bow_ && a.bow_node_ids_==b.bow_node_ids_ &&
              a.svo_node_ids_==b.svo_node_ids_,"BoW/node mismatch");
      descriptors(a.bow_features_,b.bow_features_);descriptors(a.svo_features_,b.svo_features_);
      descriptors(a.mixed_features_,b.mixed_features_);
    }
    require(reference.num_geometry_checks_>0,"test did not exercise geometry");
    require(optimized.reused_orb_==12,"ORB reuse not exercised");
    const bool impossible=points<(recovery?7:10);
    if(impossible)require(optimized.num_geometry_checks_==0 && optimized.num_3d_precheck_rejects_>0,
                         "impossible 3D candidate not rejected before RANSAC");
    else require(optimized.num_geometry_checks_==reference.num_geometry_checks_ &&
                 optimized.num_3d_precheck_rejects_==0,"viable boundary candidate skipped");
    require(reference.lc_closed_loops_.size()==optimized.lc_closed_loops_.size() &&
            reference.loop_detect_viz_info_.size()==optimized.loop_detect_viz_info_.size(),"loop result mismatch");
    std::cout<<"FASTPATH_CASE points="<<points<<" recovery="<<recovery
             <<" reference_geometry="<<reference.num_geometry_checks_
             <<" optimized_geometry="<<optimized.num_geometry_checks_
             <<" reference_orb_ms="<<reference.orb_ms_<<" optimized_orb_ms="<<optimized.orb_ms_
             <<" reference_geometry_ms="<<reference.geometry_ms_<<" optimized_geometry_ms="<<optimized.geometry_ms_
             <<" PASS\n";
  }
  std::cout<<"LOOP FASTPATH PASS cases=5\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
