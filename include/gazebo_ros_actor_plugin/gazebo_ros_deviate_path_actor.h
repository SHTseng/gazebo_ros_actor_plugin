#ifndef GAZEBO_ROS_DEVIATE_PATH_ACTOR_H
#define GAZEBO_ROS_DEVIATE_PATH_ACTOR_H

#include <string>
#include <queue>

#include <ros/ros.h>
#include <ros/callback_queue.h>
#include <geometry_msgs/Twist.h>

#include "gazebo/common/Plugin.hh"
#include "gazebo/physics/physics.hh"
#include "gazebo/util/system.hh"

namespace gazebo
{

class GazeboRosDeviatePathActor : public ModelPlugin
{
  /// \brief Constructor
  public: GazeboRosDeviatePathActor();

  public: ~GazeboRosDeviatePathActor();

  /// \brief Load the actor plugin.
  /// \param[in] _model Pointer to the parent model.
  /// \param[in] _sdf Pointer to the plugin's SDF elements.
  public: virtual void Load(physics::ModelPtr _model, sdf::ElementPtr _sdf);

    // Documentation Inherited.
  public: virtual void Reset();

  private: void VelCallback(const geometry_msgs::Twist::ConstPtr &msg);

  /// \brief Function that is called every update cycle.
  /// \param[in] _info Timing information
  private: void OnUpdate(const common::UpdateInfo &_info);

  private: void VelQueueThread();

  private: ignition::math::Pose3d frameRotation(const ignition::math::Pose3d pose, const double angle);

  private: ros::NodeHandle *ros_node_;

  private: ros::Subscriber vel_sub_;

  /// \brief Custom Callback Queue for guide vel
  private: ros::CallbackQueue vel_queue_;

  /// \brief Custom Callback Queue thread

  private: boost::thread velCallbackQueueThread_;

  private: std::string vel_topic_;

  /// \brief Pointer to the parent actor.
  private: physics::ActorPtr actor;

  /// \brief Pointer to the world, for convenience.
  private: physics::WorldPtr world;

  /// \brief Pointer to the sdf element.
  private: sdf::ElementPtr sdf;

  private: double animation_factor_;

  /// \brief List of connections
  private: std::vector<event::ConnectionPtr> connections;

  /// \brief Time of the last update.
  private: common::Time last_update;

  /// \brief Custom trajectory info.
  private: physics::TrajectoryInfoPtr trajectoryInfo;

  /// \brief Variable for control the non-constant velocity.
  private: bool oscillation_enable_;

  /// \brief Variable for control the non-constant velocity.
  private: double oscillation_factor_;

  /// \brief Velocity of the robot guide
  private: ignition::math::Pose3d guide_vel_;

  /// \brief Data structure for saving robot command.
  private: std::queue<ignition::math::Vector3d> cmd_queue_;

  private: bool first_run_;

  /// \brief Time of the actor start to deviate from orignal path
  private: double time_of_deviation_;

  /// \brief deviation duration
  private: double deviation_duration_;

  private: double deviate_angle_;

  private: bool start_deviate_;

};
}

#endif // GAZEBO_ROS_DEVIATE_PATH_ACTOR_H
