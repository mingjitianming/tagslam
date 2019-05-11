/* -*-c++-*--------------------------------------------------------------------
 * 2019 Bernd Pfrommer bernd.pfrommer@gmail.com
 */

#include "tagslam/odometry_processor.h"
#include "tagslam/geometry.h"
#include "tagslam/graph_manager.h"
#include <geometry_msgs/Quaternion.h>
#include <geometry_msgs/Point.h>
#include <fstream>

namespace tagslam {
  using Odometry = nav_msgs::Odometry;
  using OdometryConstPtr = nav_msgs::OdometryConstPtr;

  OdometryProcessor::OdometryProcessor(ros::NodeHandle &nh,
                                       GraphManager *gm,
                                       const BodyConstPtr &body) :
    graphManager_(gm), body_(body) {
    pub_ = nh.advertise<nav_msgs::Odometry>("raw_odom/body_"+body->getName(), 5);
    acceleration_ = body->getOdomAcceleration();
    angularAcceleration_ = body->getOdomAngularAcceleration();
    T_body_odom_ = body->getTransformBodyOdom();
  }

  static Transform to_pose(const OdometryConstPtr &odom) {
    const geometry_msgs::Quaternion &q = odom->pose.pose.orientation;
    const geometry_msgs::Point      &p = odom->pose.pose.position;
    return (make_transform(Eigen::Quaterniond(q.w, q.x, q.y, q.z),
                           Point3d(p.x, p.y, p.z)));
  }

  void
  OdometryProcessor::process(const OdometryConstPtr &msg,
                             std::vector<VertexDesc> *factors) {
    auto msg2 = *msg;
    msg2.header.frame_id = "map";
    pub_.publish(msg2);
    Transform newPose = to_pose(msg);
    const ros::Time &t = msg->header.stamp;
    if (time_ == ros::Time(0)) {
      lastOmega_ = Eigen::Vector3d(0, 0, 0);
      lastVelocity_ = Eigen::Vector3d(0, 0, 0);
    } else {
      const double dt = std::max((t - time_).toSec(), 0.001);
      const double dt2 = dt * dt;
      const double dtinv = 1.0 / dt;
      const auto  &tf = T_body_odom_;
      Transform deltaPose = tf * pose_.inverse() * newPose * tf.inverse();
      const Eigen::Vector3d dx = deltaPose.translation();
      Eigen::AngleAxisd aa;
      aa.fromRotationMatrix(deltaPose.rotation());
      const Eigen::Vector3d da = aa.angle() * aa.axis();
      const Eigen::Vector3d v = dx * dtinv;
      const Eigen::Vector3d w = da * dtinv;
      const double dpos = (dx - lastVelocity_ * dt).norm();
      const double dang = (da - lastOmega_ * dt).norm();
      lastOmega_    = w;
      lastVelocity_ = v;
      // Whenever the accelerations go above the
      // typical values, the noise will increase correspondingly,
      // thereby reducing the weight of the odometry measurement.
      // This addresses situations where the odometry jumps.
      const PoseNoise2 pn =
        PoseNoise2::make(std::max(dang, angularAcceleration_ * dt2),
                         std::max(dpos, acceleration_ * dt2));
      const PoseWithNoise pwn(deltaPose, pn, true);
      auto fac = graphManager_->addBodyPoseDelta(time_, msg->header.stamp, body_, pwn);
      factors->push_back(fac);
    }
    pose_ = newPose;
    time_ = t;
  }

} // end of namespace