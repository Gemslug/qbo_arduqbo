
#include <controllers/base_controller.h>
#include <cmath>
#include "ros/ros.h"
#include <geometry_msgs/Twist.h>
#include <tf/transform_broadcaster.h>
#include <nav_msgs/Odometry.h>
#include <ros/console.h>

CBaseController::CBaseController(std::string name, CQboduinoDriver *device_p, ros::NodeHandle& nh) : CController(name,device_p,nh)
{
    v_linear_ = 0.0;             // current setpoint velocity
    v_angular_ = 0.0;
    v_dirty_=true;
    x_ = 0.0;                  // position in xy plane
    y_ = 0.0;
    th_ = 0.0;
    //vel_sub_ = nh_.subscribe<turtlesim::Velocity>("command_velocity", 1, &CBaseController::commandCallback, this);
    std::string topic, odom_topic;
    //double rate;
    //std::string temp="controllers/"+name+"/topic";
    nh.param("controllers/"+name+"/topic", topic, std::string("cmd_vel"));
    //temp="controllers/"+name+"/odom_topic";
    nh.param("controllers/"+name+"/odom_topic", odom_topic, std::string("odom"));
    nh.param("controllers/"+name+"/rate", rate_, 15.0);
    nh.param("controllers/"+name+"/tf_odom_broadcast", is_odom_broadcast_enabled_, true);
    twist_sub_ = nh.subscribe<geometry_msgs::Twist>(topic, 1, &CBaseController::twistCallback, this);
    odom_pub_ = nh.advertise<nav_msgs::Odometry>(odom_topic, 1);
    stall_unlock_service_ = nh.advertiseService("unlock_motors_stall", &CBaseController::unlockStall,this);
    then_=ros::Time::now();
    //std::cout << rate_ << std::endl;
    timer_=nh.createTimer(ros::Duration(1/rate_),&CBaseController::timerCallback,this);
    odom_.header.stamp = then_;
    odom_.header.frame_id = "odom";
   // odom_.child_frame_id = "base_link"; CHANGED
    odom_.child_frame_id = "base_footprint";

    //set the position
    odom_.pose.pose.position.x = 0;
    odom_.pose.pose.position.y = 0;
    odom_.pose.pose.position.z = 0;
    odom_.pose.pose.orientation = tf::createQuaternionMsgFromYaw(0);
    boost::array<double, 36> pose_covariance = {{0.0015, 0, 0, 0, 0, 0,
                                                 0, 0.0015, 0, 0, 0, 0,
                                                 0, 0, 1e-6 , 0, 0, 0,
                                                 0, 0, 0, 1e-6 , 0, 0,
                                                 0, 0, 0, 0, 1e-6 , 0,
                                                 0, 0, 0, 0, 0, 0.05}};
    odom_.pose.covariance=pose_covariance;

    odom_.twist.twist.linear.x = 0;
    odom_.twist.twist.linear.y = 0;
    odom_.twist.twist.angular.z = 0;
    boost::array<double, 36> twist_covariance = {{0.0015, 0, 0, 0, 0, 0,
                                                 0, 1e-6 , 0, 0, 0, 0,
                                                 0, 0, 1e-6 , 0, 0, 0,
                                                 0, 0, 0, 1e-6 , 0, 0,
                                                 0, 0, 0, 0, 1e-6 , 0,
                                                 0, 0, 0, 0, 0, 0.05}};
    odom_.twist.covariance=twist_covariance;

    //TODO: Fix covariance values
}

void CBaseController::twistCallback(const geometry_msgs::Twist::ConstPtr& msg)
{
    v_linear_=(float)msg->linear.x;
    v_angular_=(float)msg->angular.z;
    v_dirty_=true;
    // clean up and log values                  
    //rospy.logdebug("Twist move: "+str(self.v_linear)+","+str(self.v_angular))
    ROS_DEBUG_STREAM("Twist move: linear=" << v_linear_ << " angular=" << v_angular_);
}

void CBaseController::timerCallback(const ros::TimerEvent& e)
{
    //Calculo de tiempos
    ros::Time now=ros::Time::now();
    double elapsed=(now - then_).toSec();
    then_=now;
    //Obtengo posicion
    float x,y,th;
    int code=device_p_->getOdometry(x,y,th);
    if(code<0)
    {
	ROS_ERROR_STREAM("Unable to get odometry from the base controler board " << code);
        return;
    }
    else
	ROS_DEBUG_STREAM("Odometry messege from base controler board: " << x << "," << y << "," << th);
    float d=sqrt(pow((x-x_),2) + pow((y-y_),2));
    float dx = d / elapsed;
    float angDiff=(th_-th)/elapsed;
    float dth = (angDiff >= 0) ? angDiff : -angDiff;
    
    geometry_msgs::Quaternion quaternion = tf::createQuaternionMsgFromYaw(th);
    
    x_=x;
    y_=y;
    th_=th;
    
    nav_msgs::Odometry odom;
    odom_.header.stamp = then_;
  
    //set the position
    odom_.pose.pose.position.x = x_;
    odom_.pose.pose.position.y = y_;
    odom_.pose.pose.orientation = quaternion;
    
    odom_.twist.twist.linear.x = dx;
    odom_.twist.twist.angular.z = dth;
  
    //publish the message
    odom_pub_.publish(odom_);
    
    if(is_odom_broadcast_enabled_)
    {
        geometry_msgs::TransformStamped odom_trans;
        //odom_trans.header.frame_id = "odom";
        odom_trans.header.frame_id = odom_.header.frame_id;
        //odom_trans.child_frame_id = "base_footprint";
        odom_trans.child_frame_id = odom_.child_frame_id;
        odom_trans.header.stamp = then_;
        odom_trans.transform.translation.x = x_;
        odom_trans.transform.translation.y = y_;
        odom_trans.transform.translation.z = 0.0;
        odom_trans.transform.rotation = quaternion;

        //send the transform
        odom_broadcaster_.sendTransform(odom_trans);
    }
    
    // update motors
    if (v_dirty_)
    {
        int code=device_p_->setSpeed(v_linear_, v_angular_);
	if (code<0)
	    ROS_ERROR("Unable to send speed command to the base controler board");
	else
	    ROS_DEBUG_STREAM("speed command sent to the base board: linear=" << v_linear_ << " angular=" << v_angular_);
        v_dirty_=false;
    }
    
}

bool CBaseController::unlockStall(std_srvs::Empty::Request &req,
                                  std_srvs::Empty::Response &res )
{
  int code=device_p_->resetStall();
  if (code<0)
  {
      ROS_ERROR("Unable to send stall reset command to the base controler board");
      return false;
  }
  else
  {
      ROS_DEBUG_STREAM("stall reset command sent to the base board");
      return true;
  }
}
