#include <gazebo_ros_actor_plugin/gazebo_ros_deviate_path_actor.h>

#include <functional>

#include <ignition/math.hh>
#include "gazebo/physics/physics.hh"

using namespace gazebo;
GZ_REGISTER_MODEL_PLUGIN(GazeboRosDeviatePathActor)

#define WALKING_ANIMATION "walking"

/////////////////////////////////////////////////
GazeboRosDeviatePathActor::GazeboRosDeviatePathActor()
{
}

GazeboRosDeviatePathActor::~GazeboRosDeviatePathActor()
{
  this->vel_queue_.clear();
  this->vel_queue_.disable();
  this->velCallbackQueueThread_.join();
  this->ros_node_->shutdown();
  delete this->ros_node_;
}

/////////////////////////////////////////////////
void GazeboRosDeviatePathActor::Load(physics::ModelPtr _model, sdf::ElementPtr _sdf)
{
  this->vel_topic_ = "/cmd_vel_mux/input/navi";
  if (_sdf->HasElement("vel_topic"))
  {
    this->vel_topic_ = _sdf->Get<std::string>("vel_topic");
  }

  this->animation_factor_ = 4.0;
  if (_sdf->HasElement("animation_factor"))
  {
    this->animation_factor_ = _sdf->Get<double>("animation_factor");
  }

  this->oscillation_enable_ = false;
  if (_sdf->HasElement("oscillation_enable"))
  {
    this->oscillation_enable_ = _sdf->Get<bool>("oscillation_enable");
  }

  this->time_of_deviation_ = 5.0;
  if (_sdf->HasElement("time_of_deviation"))
  {
    this->time_of_deviation_ = _sdf->Get<double>("time_of_deviation");
  }

  this->deviation_duration_ = 5.0;
  if (_sdf->HasElement("deviation_duration"))
  {
    this->deviation_duration_ = _sdf->Get<double>("deviation_duration");
  }

  this->deviate_angle_ = 1.57;
  if (_sdf->HasElement("deviate_angle"))
  {
    this->deviate_angle_ = _sdf->Get<double>("deviate_angle");
  }

  // Make sure the ROS node for Gazebo has already been initialized
  if (!ros::isInitialized())
  {
    ROS_FATAL_STREAM_NAMED("actor", "A ROS node for Gazebo has not been initialized,"
                                        << "unable to load plugin. Load the Gazebo system plugin "
                                        << "'libgazebo_ros_api_plugin.so' in the gazebo_ros package)");
    return;
  }

  this->sdf = _sdf;
  this->actor = boost::dynamic_pointer_cast<physics::Actor>(_model);
  this->world = this->actor->GetWorld();

  this->Reset();

  this->ros_node_ = new ros::NodeHandle();

  // subscribe to the speed of the guide
  ros::SubscribeOptions vel_so = ros::SubscribeOptions::create<geometry_msgs::Twist>(vel_topic_, 1000,
                                                                                     boost::bind(&GazeboRosDeviatePathActor::VelCallback, this, _1),
                                                                                     ros::VoidPtr(), &vel_queue_);
  this->vel_sub_ = ros_node_->subscribe(vel_so);

  this->velCallbackQueueThread_ =
      boost::thread(boost::bind(&GazeboRosDeviatePathActor::VelQueueThread, this));

  this->connections.push_back(event::Events::ConnectWorldUpdateBegin(
      std::bind(&GazeboRosDeviatePathActor::OnUpdate, this, std::placeholders::_1)));
}

/////////////////////////////////////////////////
void GazeboRosDeviatePathActor::Reset()
{
  this->last_update = 0;
  this->first_run_ = true;
  this->start_deviate_ = false;

  auto skelAnims = this->actor->SkeletonAnimations();
  if (skelAnims.find(WALKING_ANIMATION) == skelAnims.end())
  {
    gzerr << "Skeleton animation " << WALKING_ANIMATION << " not found.\n";
  }
  else
  {
    // Create custom trajectory
    this->trajectoryInfo.reset(new physics::TrajectoryInfo());
    this->trajectoryInfo->type = WALKING_ANIMATION;
    this->trajectoryInfo->duration = 1.0;

    this->actor->SetCustomTrajectory(this->trajectoryInfo);
  }
}

void GazeboRosDeviatePathActor::VelCallback(const geometry_msgs::Twist::ConstPtr &msg)
{
  if (this->first_run_)
  {
    // Insert initial commands, assume the robot is in front of the model 2m and 0.4m/s velocity
    for (int i = 0; i < 1000*2/0.4; i++)
    {
      ignition::math::Vector3d cmd;
      cmd.X() = 0.4;
      cmd.Z() = 0.0;
      this->cmd_queue_.push(cmd);
    }
    this->first_run_ = false;
  }

  // To make the frequency of command match update event
  for (int i = 0; i < 50; i++)
  {
    ignition::math::Vector3d vel_cmd;
    vel_cmd.X() = msg->linear.x;
    vel_cmd.Z() = msg->angular.z;
    this->cmd_queue_.push(vel_cmd);
  }
}

/////////////////////////////////////////////////
void GazeboRosDeviatePathActor::OnUpdate(const common::UpdateInfo &_info)
{
  ignition::math::Pose3d pose = this->actor->WorldPose();
  double dt = (_info.simTime - this->last_update).Double();
  ignition::math::Vector3d rpy = pose.Rot().Euler();

  if (!this->cmd_queue_.empty())
  {
    this->guide_vel_.Pos().X() = this->cmd_queue_.front().X();
    this->guide_vel_.Rot() = ignition::math::Quaterniond(0, 0, this->cmd_queue_.front().Z());
    this->cmd_queue_.pop();
  }

  if ((_info.simTime).Double() > this->time_of_deviation_ && (_info.simTime).Double() < (this->time_of_deviation_+this->deviation_duration_))
  {
    // change the direction of velocity in a duration
    this->guide_vel_ = frameRotation(this->guide_vel_, this->deviate_angle_);
    if (!this->start_deviate_)
    {
      rpy.Z() += this->deviate_angle_;
      this->start_deviate_ = true;
    }
    pose.Pos().Y() += this->guide_vel_.Pos().X()*cos(pose.Rot().Euler().Z())*dt;
  }
  else
  {
    pose.Pos().Y() -= this->guide_vel_.Pos().X()*cos(pose.Rot().Euler().Z())*dt;
  }

  pose.Pos().X() += this->guide_vel_.Pos().X()*sin(pose.Rot().Euler().Z())*dt;
//  pose.Pos().Y() -= this->guide_vel_.Pos().X()*cos(pose.Rot().Euler().Z())*dt;
  pose.Rot() = ignition::math::Quaterniond(1.5707, 0, rpy.Z()+this->guide_vel_.Rot().Euler().Z()*dt);

  double distanceTraveled = (pose.Pos() - this->actor->WorldPose().Pos()).Length();

  this->actor->SetWorldPose(pose, false, false);
  this->actor->SetScriptTime(this->actor->ScriptTime() + (distanceTraveled * this->animation_factor_));
  this->last_update = _info.simTime;
}

void GazeboRosDeviatePathActor::VelQueueThread()
{
  static const double timeout = 0.01;

  while (this->ros_node_->ok())
    this->vel_queue_.callAvailable(ros::WallDuration(timeout));
}

ignition::math::Pose3d GazeboRosDeviatePathActor::frameRotation(const ignition::math::Pose3d pose, const double angle)
{
  ignition::math::Pose3d transformed_p;
  transformed_p.Pos().X() = std::cos(angle)*pose.Pos().X()-std::sin(angle)*pose.Pos().Y();
  transformed_p.Pos().Y() = std::sin(angle)*pose.Pos().X()+std::cos(angle)*pose.Pos().Y();
//  transformed_p.Rot() = ignition::math::Quaterniond(1.5707, 0, pose.Rot().Euler().Z()+angle);
  return transformed_p;
}
