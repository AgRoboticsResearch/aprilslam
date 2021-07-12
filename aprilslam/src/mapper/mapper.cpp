#include "aprilslam/mapper.h"
#include "aprilslam/utils.h"
#include <fstream>
#include <ros/ros.h>

namespace aprilslam
{

    using namespace gtsam;

    int Mapper::pose_cnt = 0;

    Mapper::Mapper(double relinearize_thresh, int relinearize_skip)
        : init_(false),
          init_opt_(false),
          min_pose_count_(15),
          inc_pose_count_(0),
          inc_pose_thr_(3),
          obsv_thr_(4),
          params_(ISAM2GaussNewtonParams(), relinearize_thresh, relinearize_skip),
          isam2_(params_),
          tag_noise_(noiseModel::Diagonal::Sigmas((Vector(6) << 0.10, 0.10, 0.10, 0.10, 0.10, 0.10).finished())),
          small_noise_(noiseModel::Diagonal::Sigmas((Vector(6) << 0.05, 0.03, 0.01, 0.05, 0.05, 0.05).finished())),
          tag_size_noise_(noiseModel::Isotropic::Sigma(1, 0.01)),
          motion_model_noise_(noiseModel::Diagonal::Sigmas((Vector(6) << 0.15, 0.01, 0.15, 0.01, 0.05, 0.01).finished()))
    {
        measurement_noise_ = noiseModel::Isotropic::Sigma(2, 1.0);

        tag_noise_huber_ = noiseModel::Robust::Create(noiseModel::mEstimator::Huber::Create(1.0), tag_noise_);
        small_noise_huber_ = noiseModel::Robust::Create(noiseModel::mEstimator::Huber::Create(1.0), small_noise_);
        tag_size_noise_huber_ = noiseModel::Robust::Create(noiseModel::mEstimator::Huber::Create(0.1), small_noise_);
        measurement_noise_huber_ = noiseModel::Robust::Create(noiseModel::mEstimator::Huber::Create(0.5), measurement_noise_);

        // lm_params_.setMaxIterations(10);
        // lm_params_.setVerbosity("ERROR");
    }

    void Mapper::InitCameraParams(const cv::Matx33d &intrinsic, const cv::Mat &distCoeff)
    {
        Eigen::Matrix3d intrinsic_eigen;
        cv::cv2eigen(intrinsic, intrinsic_eigen);
        double fx = intrinsic_eigen(0, 0);
        double fy = intrinsic_eigen(1, 1);
        double cx = intrinsic_eigen(0, 2);
        double cy = intrinsic_eigen(1, 2);
        K_ = Cal3_S2::shared_ptr(new Cal3_S2(fx, fy, 0, cx, cy));
    }

    void Mapper::UpdateTagsPriorInfo(const std::map<size_t, geometry_msgs::Pose> tag_prior_poses)
    {
        tag_prior_poses_ = tag_prior_poses;
    }

    void Mapper::AddPose(const geometry_msgs::Pose &pose)
    {
        pose_cnt++;

        // Transform geometey_msgs::pose to gtsam::Pose3
        pose_ = FromGeometryPose(pose);
        initial_estimates_.insert(Symbol('x', pose_cnt), pose_);
        //  ROS_INFO("Initial estimate: x%d", pose_cnt);
        // std::cout << "Initial estimate: x" << pose_cnt << std::endl;
    }

    void Mapper::AddPose(const geometry_msgs::Pose &pose, const geometry_msgs::Pose &vel)
    {
        pose_cnt++;

        // Transform geometey_msgs::pose to gtsam::Pose3
        pose_ = FromGeometryPose(pose);
        initial_estimates_.insert(Symbol('x', pose_cnt), pose_);

        if(pose_cnt > min_pose_count_)
        {
            gtsam::Pose3 velocity = FromGeometryPose(vel);
            graph_.push_back(BetweenFactor<Pose3>(Symbol('x', pose_cnt - 1), Symbol('x', pose_cnt), velocity, motion_model_noise_));
        }
        //  ROS_INFO("Initial estimate: x%d", pose_cnt);
        // std::cout << "Initial estimate: x" << pose_cnt << std::endl;
    }

    void Mapper::Initialize(const Apriltag &tag_w)
    {
        ROS_ASSERT_MSG(pose_cnt == 1, "Incorrect initial pose");

        if (tag_prior_poses_.empty() || tag_prior_poses_.find(tag_w.id) == tag_prior_poses_.end())
        {
            if (all_ids_.find(tag_w.id) == all_ids_.end())
            {
                AddLandmark(tag_w, Pose3());
                ROS_INFO("INIT: Add l%x to initial estimate", tag_w.id);
            }
        }
        else
        {
            geometry_msgs::Pose tag_prior_pose = tag_prior_poses_.find(tag_w.id)->second;
            if (all_ids_.find(tag_w.id) == all_ids_.end())
            {
                AddLandmark(tag_w, FromGeometryPose(tag_prior_pose));
                ROS_INFO("INIT: Add l%x to initial estimate", tag_w.id);
            } // ! Check ok. corners of first tag is correct
        }

        // A very strong prior on first pose
        ROS_INFO("Add pose prior on: x%d", pose_cnt);
        graph_.push_back(PriorFactor<Pose3>(Symbol('x', pose_cnt), pose_, small_noise_));

        init_ = true;
    }

    void Mapper::AddLandmark(const Apriltag &tag_c, const Pose3 &pose)
    {
        initial_estimates_.insert(Symbol('l', tag_c.id), pose);
        //  ROS_INFO("Initial estimate: l%d", tag_c.id);
        // std::cout << "Initial estimate: l" << tag_c.id << std::endl;
        all_ids_.insert(tag_c.id);
        all_tags_c_[tag_c.id] = tag_c;
        AddLandmarkPrior(tag_c.id);
    }

    void Mapper::AddLandmarks(const std::vector<Apriltag> &tags_c)
    {
        for (const Apriltag &tag_c : tags_c)
        {
            // Only add landmark if it's not already added
            if (all_ids_.find(tag_c.id) == all_ids_.end())
            {
                // wTt = wTc * cTt
                const Pose3 &w_T_c = pose_;
                const Pose3 c_T_t = FromGeometryPose(tag_c.pose);
                const Pose3 w_T_t = w_T_c.compose(c_T_t);
                AddLandmark(tag_c, w_T_t);
            }
        }
    }

    void Mapper::AddLandmarkPrior(const size_t tag_id)
    {
        if (tag_prior_poses_.find(tag_id) == tag_prior_poses_.end())
        {
            ROS_WARN("There is no prior information about tag %d", tag_id);
            return;
        }
        else
        {
            ROS_INFO("Add landmark prior on: %d", tag_id);
            gtsam::Pose3 tag_prior_pose = FromGeometryPose(tag_prior_poses_.find(tag_id)->second);
            graph_.push_back(PriorFactor<Pose3>(Symbol('l', tag_id), tag_prior_pose, small_noise_));
            return;
        }
    }

    void Mapper::AddFactors(const std::vector<Apriltag> &tags_c)
    {

        for (const Apriltag &tag_c : tags_c)
        {
            // Newly Observed Apriltags
            if (tags_obsv_.find(tag_c.id) == tags_obsv_.end() && tags_in_isam_.find(tag_c.id) == tags_in_isam_.end())
            {
                std::vector<int> obsv_pose_id;
                obsv_pose_id.push_back(pose_cnt);
                tags_obsv_.insert(std::pair<int, std::vector<int>>(tag_c.id, obsv_pose_id));
                tags_in_isam_.insert(std::pair<int, bool>(tag_c.id, false));
                continue;
            }
            else if (tags_in_isam_.find(tag_c.id)->second == false)
            {
                auto iter_obsv = tags_obsv_.find(tag_c.id);
                iter_obsv->second.push_back(pose_cnt);

                if (iter_obsv->second.size() > obsv_thr_)
                {
                    // Trverse all observations
                    for (int i_obsv = 0; i_obsv < iter_obsv->second.size(); i_obsv++)
                    {
                        // Add between factors
                        Symbol x_i('x', iter_obsv->second[i_obsv]);
                        graph_.push_back(BetweenFactor<Pose3>(x_i, Symbol('l', tag_c.id), FromGeometryPose(tag_c.pose), tag_noise_huber_));

                        // Add visual projection factors and range factors
                        if (all_tags_w_.find(tag_c.id) != all_tags_w_.end())
                        {
                            Apriltag tag_w = all_tags_w_.find(tag_c.id)->second;
                            for (size_t ip = 0; ip < tag_c.corners.size(); ip++)
                            {
                                Point2 corner_measurment(tag_c.corners[ip].x, tag_c.corners[ip].y);
                                int corner_id = ip + tag_c.id * 4;
                                Symbol p_i('p', corner_id);
                                graph_.push_back(GenericProjectionFactor<Pose3, Point3, Cal3_S2>(corner_measurment, measurement_noise_, x_i, p_i, K_));
                                // std::cout << "Add factor between x" << iter_obsv->second[i_obsv] << " and p" << corner_id << std::endl;

                                if (!initial_estimates_.exists(p_i))
                                {
                                    double a = tag_w.size / 2;
                                    const std::vector<Point3> corners_on_board = {{-a, -a, 0}, {a, -a, 0}, {a, a, 0}, {-a, a, 0}};
                                    Symbol l_i('l', tag_c.id);

                                    Point3 corner_initial_estimate(tag_w.corners[ip].x, tag_w.corners[ip].y, tag_w.corners[ip].z);
                                    initial_estimates_.insert(p_i, corner_initial_estimate);
                                    //  ROS_INFO("Initial estimate: p%d", p_i);
                                    std::cout << "Initial estimate: p" << corner_id << std::endl;

                                    Unit3 bearing_ip_li(corners_on_board[ip]);
                                    double range_ip_li = std::sqrt(2) * a;
                                    // graph_.push_back(BearingRange3D(l_i, p_i, bearing_ip_li, range_ip_li, tag_size_noise_));
                                    graph_.push_back(RangeFactor<Pose3, Point3>(l_i, p_i, range_ip_li, tag_size_noise_));
                                }
                            }
                        }
                    }
                    tags_in_isam_.find(tag_c.id)->second = true;
                    continue;
                }
            }
            else if (tags_in_isam_.find(tag_c.id)->second == true)
            {
                // Add between factors
                Symbol x_i('x', pose_cnt);
                graph_.push_back(BetweenFactor<Pose3>(x_i, Symbol('l', tag_c.id), FromGeometryPose(tag_c.pose), tag_noise_huber_));

                // Add visual projection factors and range factors
                if (all_tags_w_.find(tag_c.id) != all_tags_w_.end())
                {
                    Apriltag tag_w = all_tags_w_.find(tag_c.id)->second;
                    for (size_t ip = 0; ip < tag_c.corners.size(); ip++)
                    {
                        Point2 corner_measurment(tag_c.corners[ip].x, tag_c.corners[ip].y);
                        int corner_id = ip + tag_c.id * 4;
                        Symbol p_i('p', corner_id);
                        graph_.push_back(GenericProjectionFactor<Pose3, Point3, Cal3_S2>(corner_measurment, measurement_noise_, x_i, p_i, K_));
                        // std::cout << "Add factor between x" << pose_cnt << " and p" << corner_id << std::endl;
                    }
                }
            }
        }
    }

    // // Add bearingRange factors here
    // Symbol l_i('l', tag_w.id);

    // for (size_t ip = 0; ip < tag_w.corners.size(); ip++)
    // {
    //     Point3 corner_initial_estimate(tag_w.corners[ip].x, tag_w.corners[ip].y, tag_w.corners[ip].z);
    //     // std::cout << corner_initial_estimate << std::endl;
    //     int corner_id = ip + tag_w.id * 4;
    //     Symbol p_i('p', corner_id);
    //     initial_estimates_.insert(p_i, corner_initial_estimate);
    //     // ROS_INFO("Initial estimate: p%d", p_i);
    //     std::cout << "Initial estimate: " << p_i << std::endl;
    //     // Unit3 bearing_ip_li(corners_on_board[ip]);
    //     // double range_ip_li = std::sqrt(2) * a;
    //     //graph_.push_back(BearingRange3D(l_i, p_i, bearing_ip_li, range_ip_li, tag_size_noise_));
    //     //graph_.push_back(RangeFactor<Pose3, Point3>(l_i, p_i, range_ip_li, tag_size_noise_));
    // }

    void Mapper::Optimize(int num_iterations)
    {
        std::ofstream graph_ofs("/home/wushaoteng/project/electroMechanical/catkin_ws/data/aprilslam.dot");
        graph_.saveGraph(graph_ofs, batch_results_);

        inc_pose_count_++;
        // ROS_INFO("Begin Optimize!");
        if ((pose_cnt > min_pose_count_) )
        {
            // ROS_INFO("Optimize!");
            if (!init_opt_)
            { // Do a full optimize for first minK ranges
                init_opt_ = true;
                LevenbergMarquardtOptimizer batchOptimizer(graph_, initial_estimates_);
                initial_estimates_ = batchOptimizer.optimize();
                ROS_INFO("Batch Optimize for first %d frames", min_pose_count_);
                
            }
            
            isam2_.update(graph_, initial_estimates_);
            if (num_iterations > 1)
            {
                for (int i = 1; i < num_iterations; ++i)
                {
                    isam2_.update();
                }
                inc_pose_count_ = 0;
            }
            results_ = isam2_.calculateEstimate();
            this->Clear();
        }
        else
        {
            results_ = initial_estimates_;
        }
    }

    void Mapper::BatchOptimize()
    {
        gtsam::LevenbergMarquardtOptimizer lm_optimizer(graph_, initial_estimates_, lm_params_);
        batch_results_ = lm_optimizer.optimize();
        std::ofstream graph_ofs("/home/wushaoteng/project/electroMechanical/catkin_ws/data/aprilslam.dot");
        graph_.saveGraph(graph_ofs, batch_results_);
    }

    void Mapper::BatchUpdate(TagMap *map, geometry_msgs::Pose *pose)
    {
        ROS_ASSERT_MSG(all_ids_.size() == all_tags_c_.size(), "id and size mismatch");
        results_ = batch_results_;
        // Update the current pose
        const Pose3 &cam_pose = results_.at<Pose3>(Symbol('x', pose_cnt));
        SetPosition(&pose->position, cam_pose.x(), cam_pose.y(), cam_pose.z());
        SetOrientation(&pose->orientation, cam_pose.rotation().toQuaternion());
        // Update the current map
        for (const int tag_id : all_ids_)
        {
            const Pose3 &tag_pose3 = results_.at<Pose3>(Symbol('l', tag_id));
            geometry_msgs::Pose tag_pose;
            SetPosition(&tag_pose.position, tag_pose3.x(), tag_pose3.y(), tag_pose3.z());
            SetOrientation(&tag_pose.orientation, tag_pose3.rotation().toQuaternion());
            // This should not change the size of all_sizes_ because all_sizes_ and
            // all_ids_ should have the same size
            auto it = all_tags_c_.find(tag_id);
            map->AddOrUpdate(it->second, tag_pose);
            //应该在这里同步mapper中的tagw
            //上面的addorupdate操作不会改变迭代器中second的值

            if (all_tags_w_.find(it->second.id) == all_tags_w_.end())
            {
                Apriltag tag_w = it->second;
                tag_w.pose = tag_pose;
                tag_w.center = tag_w.pose.position;

                //! Check OK
                SetCorners(&tag_w.corners, tag_w.pose, tag_w.size);
                all_tags_w_.insert(std::make_pair(tag_w.id, tag_w));
                // ROS_INFO("tag %d added to mapper", tag_w.id);

                // Add bearingRange factors here
                Symbol l_i('l', tag_w.id);
                double a = tag_w.size / 2;
                const std::vector<Point3> corners_on_board = {{-a, -a, 0}, {a, -a, 0}, {a, a, 0}, {-a, a, 0}};

                for (size_t ip = 0; ip < tag_w.corners.size(); ip++)
                {
                    Point3 corner_initial_estimate(tag_w.corners[ip].x, tag_w.corners[ip].y, tag_w.corners[ip].z);
                    // std::cout << corner_initial_estimate << std::endl;
                    int corner_id = ip + tag_w.id * 4;
                    Symbol p_i('p', corner_id);
                    initial_estimates_.insert(p_i, corner_initial_estimate);
                    // ROS_INFO("Initial estimate: p%d", p_i);
                    std::cout << "Initial estimate: " << p_i << std::endl;
                    // Unit3 bearing_ip_li(corners_on_board[ip]);
                    // double range_ip_li = std::sqrt(2) * a;
                    //graph_.push_back(BearingRange3D(l_i, p_i, bearing_ip_li, range_ip_li, tag_size_noise_));
                    //graph_.push_back(RangeFactor<Pose3, Point3>(l_i, p_i, range_ip_li, tag_size_noise_));
                }
            }
        }
    }

    void Mapper::Update(TagMap *map, geometry_msgs::Pose *pose)
    {
        // if(!init_opt_)
        // {
        //     // pose = geometry_msgs::Pose();
        //     return;
        // }
        ROS_ASSERT_MSG(all_ids_.size() == all_tags_c_.size(), "id and size mismatch");
        // results_ = isam2_.calculateEstimate();
        // Update the current pose
        const Pose3 &cam_pose = results_.at<Pose3>(Symbol('x', pose_cnt));
        SetPosition(&pose->position, cam_pose.x(), cam_pose.y(), cam_pose.z());
        SetOrientation(&pose->orientation, cam_pose.rotation().toQuaternion());
        // Update the current map
        for (const int tag_id : all_ids_)
        {
            const Pose3 &tag_pose3 = results_.at<Pose3>(Symbol('l', tag_id));
            geometry_msgs::Pose tag_pose;
            SetPosition(&tag_pose.position, tag_pose3.x(), tag_pose3.y(), tag_pose3.z());
            SetOrientation(&tag_pose.orientation, tag_pose3.rotation().toQuaternion());
            // This should not change the size of all_sizes_ because all_sizes_ and
            // all_ids_ should have the same size
            auto it = all_tags_c_.find(tag_id);
            map->AddOrUpdate(it->second, tag_pose);
            //应该在这里同步mapper中的tagw
            //上面的addorupdate操作不会改变迭代器中second的值

            if (all_tags_w_.find(it->second.id) == all_tags_w_.end())
            {
                Apriltag tag_w = it->second;
                tag_w.pose = tag_pose;
                tag_w.center = tag_w.pose.position;

                //! Check OK
                SetCorners(&tag_w.corners, tag_w.pose, tag_w.size);
                all_tags_w_.insert(std::make_pair(tag_w.id, tag_w));
                ROS_INFO("tag %d added to mapper", tag_w.id);

                // for (size_t ip = 0; ip < tag_w.corners.size(); ip++)
                // {
                //     Point3 corner_initial_estimate(tag_w.corners[ip].x, tag_w.corners[ip].y, tag_w.corners[ip].z);
                //     // std::cout << corner_initial_estimate << std::endl;
                //     int corner_id = ip + tag_w.id * 4;
                //     Symbol p_i('p', corner_id);
                //     initial_estimates_.insert(p_i, corner_initial_estimate);
                //     //  ROS_INFO("Initial estimate: p%d", p_i);
                //     std::cout << "Initial estimate: " << p_i << std::endl;
                // }
            }
        }
        // ROS_INFO("Update finished");
    }

    void Mapper::Clear()
    {
        // ROS_WARN("Optimizer Clear");
        graph_.resize(0);
        initial_estimates_.clear();
    }

    Pose3 FromGeometryPose(const geometry_msgs::Pose &pose)
    {
        Point3 t(pose.position.x, pose.position.y, pose.position.z);
        Rot3 R(Eigen::Quaterniond(pose.orientation.w, pose.orientation.x,
                                  pose.orientation.y, pose.orientation.z));
        return Pose3(R, t);
    }

} // namespace aprilslam

// ROS_INFO("tag %d added to mapper", tag_w.id);
