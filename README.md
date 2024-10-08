# AprilSLAM - Mapping and localization from AprilTags

## 1. Introduction

Some basic code in this repository is forked from https://github.com/ProjectArtemis/aprilslam . Thanks [*@mhkabir*](https://github.com/mhkabir) for opening the awesome code.

**Your can open this [doxygen documentation](./docs/index.html#http://) created by [*@ShaotengWu*](https://github.com/ShaotengWu) in your browser for detailed documentation.**

AprilSLAM is a package designed for fast camera pose estimation from a single or multiple AprilTags in an unstructured environment. AprilSLAM needs prior information of Apriltags for better localization performance. The system can map multiple tags in the camera's view as long as there is atleast another tag in view to estimate relative tag pose the first time. The system has been run with a forward looking ZED2 stereo camera on an AGV with a X86-based computing solutions for precise estimation of the vehicle pose. The localization FPS is nearly 30Hz. The system is implemented under ROS (Robot Operating System) for ease of integration, but should be easy to run without it as well.


![aprilslam](./aprilslam/pics/aprilslam.jpg)
We use the awesome [apriltag_ros repository](https://github.com/AprilRobotics/apriltag_ros)[1-3] to extract Apriltags and modify some interfaces. The mapping system is implemented based on GTSAM [4].

The default AprilTag family used is 36h11 with a black border of 1. A PDF of the tag family is available here : http://www.dotproduct3d.com/assets/pdf/apriltags.pdf

The package is originally developed by Chao Qu and Gareth Cross from Kumar Robotics (www.kumarrobotics.org) and M.H.Kabirm. The repository is forked from the original Apriltag SLAM and is developed and maintained by Shaoteng Wu from SJTU. (Contact me: wushaoteng@sjtu.edu.cn)

![ex1](./aprilslam/pics/aprilslam.gif)

## 2. Dependencies

The code is implemented majorly based on [ROS melodic](https://www.ros.org/), [GTSAM 4.0.2](https://github.com/borglab/gtsam) and [OpenCV 4.4.0](https://opencv.org/opencv-4-0/).

### 2.1 ROS Melodic
Install ROS Melodic according to [ROS official instructions](http://wiki.ros.org/melodic/Installation/Ubuntu). Additionally, install `apriltag-ros` dependency with following step.
```bash
#!bash
$ sudo apt-get install ros-melodic-apriltag
```


### 2.2 GTSAM 4.0.2

GTSAM [installation](https://github.com/borglab/gtsam)
```bash
#!bash
$ git clone https://github.com/borglab/gtsam.git
$ cd gtsam
$ mkdir build
$ cd build
$ cmake ..
$ make check (optional, runs unit tests)
$ sudo make install
```

### 2.3 OpenCV 4.4.0

You can install OpenCV 4.4.0 referring to this [link](https://gist.github.com/raulqf/f42c718a658cddc16f9df07ecc627be7). CUDA is not necessarily needed and you can customize your own compilation settings. Aprilslam only need some basic data structures and algorithms.



## 3. Get Started

### 3.1 Build

```bash
#!bash
$ cd YOUR_WORK_SPACE/src
$ git clone https://github.com/ShaotengWu/aprilslam.git 
$ cd ..
$ catkin build
```

### 3.2 Get test data

You can get example data on Baidu Netdisk with following link and password.

Link:     https://pan.baidu.com/s/1kbAQ4fmSu9N7nlXAXFHemw
Password: sbs4

### 3.3 Modify Parameters

In `aprilslam/aprilslam/launch/slam.launch`, set proper value of following parameters according to your settings.
**Attention:** The prior information yaml should be consistent with bag data for better localization performance.
```xml
<arg name="camera" default="/zed2_left" />
<arg name="use_tag_prior_info" default="true" />
<arg name="tag_prior_info_path" default="$(find aprilslam)/config/YOUR_CONFIG.yaml" />
...
<node pkg="rosbag" type="play" name="bag_data" args="--clock  PATH_TO_YOUR_BAG_DATA.bag" />
```

### 3.4 Launch
```bash
#!bash
$ catkin build
$ source devel/setup.bash
# if you use zsh:
# $ source devel/setup.zsh
$ roslaunch aprilslam slam.launch
```
## 4.Acknowledgement

This repository is developed on a SJTU Master course under instructions of A/Prof. Liang Gong. Besides, Thanks Yingxin Wu for help on experiments.  



## 5.References

Please cite the appropriate papers when using this package or parts of it in an academic publication.

1. D. Malyuta, C. Brommer, D. Hentzen, T. Stastny, R. Siegwart, and R. Brockers, “Long-duration fully autonomous operation of rotorcraft unmanned aerial systems for remote-sensing data acquisition,” Journal of Field Robotics, p. arXiv:1908.06381, Aug. 2019.
2. C. Brommer, D. Malyuta, D. Hentzen, and R. Brockers, “Long-duration autonomy for small rotorcraft UAS including recharging,” in IEEE/RSJ International Conference on Intelligent Robots and Systems, IEEE, p. arXiv:1810.05683, oct 2018.
3. J. Wang and E. Olson, "AprilTag 2: Efficient and robust fiducial detection," in ''Proceedings of the IEEE/RSJ International Conference on Intelligent Robots and Systems (IROS)'', October 2016.
4. GTSAM. https://collab.cc.gatech.edu/borg/gtsam/
