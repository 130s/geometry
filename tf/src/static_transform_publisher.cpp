/*
 * Copyright (c) 2008, Willow Garage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Willow Garage, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <cstdio>
#include <dynamic_reconfigure/server.h>
#include "tf/transform_broadcaster.h"
#include "tf/TransformSenderConfig.h"

class TransformSender
{
public:
  ros::NodeHandle node_;
  //constructor
  TransformSender(double x, double y, double z, double yaw, double pitch, double roll, ros::Time time, const std::string& frame_id, const std::string& child_frame_id) :
    angle_units_(0)
  {
    tf::Quaternion q;
    q.setRPY(roll, pitch,yaw);
    transform_ = tf::StampedTransform(tf::Transform(q, tf::Vector3(x,y,z)), time, frame_id, child_frame_id );
    reconfInit();
  };
  TransformSender(double x, double y, double z, double qx, double qy, double qz, double qw, ros::Time time, const std::string& frame_id, const std::string& child_frame_id) :
    angle_units_(0), transform_(tf::Transform(tf::Quaternion(qx,qy,qz,qw), tf::Vector3(x,y,z)), time, frame_id, child_frame_id)
  {
    reconfInit();
  };
  //Clean up ros connections
  ~TransformSender() { }

  //A pointer to the rosTFServer class
  tf::TransformBroadcaster broadcaster;



  // A function to call to send data periodically
  void send (ros::Time time) {
    transform_.stamp_ = time;
    broadcaster.sendTransform(transform_);
  };

  // Switch limits
  void reconfRPYLimits()
  {
    tf::TransformSenderConfig min_conf, max_conf;

    // Get current limits
    reconf_server_.getConfigMin(min_conf);
    reconf_server_.getConfigMax(max_conf);

    // Update only RPY limits
    if(angle_units_ == USE_RADIANS)
    {
      // Radians
      min_conf.roll = min_conf.pitch = min_conf.yaw = -M_PI;
      max_conf.roll = max_conf.pitch = max_conf.yaw = M_PI;
    }
    else
    {
      // Degrees
      min_conf.roll = min_conf.pitch = min_conf.yaw = -180.0;
      max_conf.roll = max_conf.pitch = max_conf.yaw = 180.0;
    }

    // Set new limits
    reconf_server_.setConfigMin(min_conf);
    reconf_server_.setConfigMax(max_conf);
  }

  // Dynamic reconfigure callback
  void reconfCallback(tf::TransformSenderConfig &config, uint32_t level)
  {
    tf::Quaternion q;
    tf::Transform t;
    double R, P, Y;

    ROS_INFO_STREAM("Level: " << level);

    switch(level)
    {
      // Sent by dynamic reconfigure at first run
      case CHANGE_ALL:
          transform_.getBasis().getRPY(R, P, Y);
          // Radians to degrees
          if(angle_units_ == USE_DEGREES) toDegrees(R, P, Y);

          // Update config with current values
          config.x = transform_.getOrigin().x();
          config.y = transform_.getOrigin().y();
          config.z = transform_.getOrigin().z();

          config.roll = R;
          config.pitch = P;
          config.yaw = Y;

          config.qw = transform_.getRotation().w();
          config.qx = transform_.getRotation().x();
          config.qy = transform_.getRotation().y();
          config.qz = transform_.getRotation().z();
          break;

      case CHANGE_XYZ:
        // Update translation only
        t = tf::Transform(transform_.getRotation(), tf::Vector3(config.x, config.y, config.z));
        transform_ = tf::StampedTransform(t, ros::Time::now(), transform_.frame_id_, transform_.child_frame_id_);
        break;

      case CHANGE_RPY:
        R = config.roll; P = config.pitch; Y = config.yaw;

        // Degrees to radians
        if(angle_units_ == USE_DEGREES) toRadians(R, P, Y);

        q.setRPY(R, P, Y);

        // Update orientation only
        t = tf::Transform(q, transform_.getOrigin());
        transform_ = tf::StampedTransform(t, ros::Time::now(), transform_.frame_id_, transform_.child_frame_id_);

        // Update quaternion
        config.qw = transform_.getRotation().w();
        config.qx = transform_.getRotation().x();
        config.qy = transform_.getRotation().y();
        config.qz = transform_.getRotation().z();
        break;

      case CHANGE_QUAT:
        q = tf::Quaternion(config.qx, config.qy, config.qz, config.qw);

        // If new quaternion is not valid use previous one and issue error
        if(q.length2() == 0.0){
          q = transform_.getRotation();
          ROS_ERROR("Reconfigure: quaternion length cannot be 0.0. Using previous value");
        }
        // Check normalization
        else if(q.length2() > 1.0 + DBL_EPSILON || q.length2() < 1.0 - DBL_EPSILON)
        {
          q = q.normalize();
          ROS_WARN("Reconfigure: quaternion is not normalized. Normalizing.");
        }

        // Update orientation only
        t = tf::Transform(q, transform_.getOrigin());
        transform_ = tf::StampedTransform(t, ros::Time::now(), transform_.frame_id_, transform_.child_frame_id_);

        // Update quaternion with corrected value
        config.qw = q.w();
        config.qx = q.x();
        config.qy = q.y();
        config.qz = q.z();

        // Update RPY
        transform_.getBasis().getRPY(R, P, Y);
        // Radians to degrees
        if(angle_units_ == USE_DEGREES) toDegrees(R, P, Y);

        config.roll = R;
        config.pitch = P;
        config.yaw = Y;

        // Reset checkbox
        config.use_quaternion = false;
        break;

      case CHANGE_UNITS:
        // if not changed then do nothing
        if (angle_units_ == config.angle_units) break;

        angle_units_ = config.angle_units;

        ROS_INFO_STREAM("UNITS: " << angle_units_);

        reconfRPYLimits();

        // Update RPY
        transform_.getBasis().getRPY(R, P, Y);

        // Radians to degrees
        if(angle_units_ == USE_DEGREES)
        {
          toDegrees(R, P, Y);
          config.angle_units = angle_units_;
        }

        config.roll = R;
        config.pitch = P;
        config.yaw = Y;

        break;
    }
  }

private:
  enum {
    CHANGE_NOTHING = 0,
    CHANGE_XYZ = 1 << 0,
    CHANGE_RPY = 1 << 1,
    CHANGE_QUAT = 1 << 2,
    CHANGE_UNITS = 1 << 3,
    CHANGE_ALL = 0xffffffff
  };

  enum {
    USE_RADIANS = 0,
    USE_DEGREES = 1
  };

  tf::StampedTransform transform_;
  dynamic_reconfigure::Server<tf::TransformSenderConfig> reconf_server_;
  int angle_units_;

  // Set dynamic reconfigure callback
  void reconfInit()
  {
    reconf_server_.setCallback(boost::bind(&TransformSender::reconfCallback, this, _1, _2));
  }

  void toDegrees(double &r, double &p, double &y)
  {
    r = r * 180.0 / M_PI;
    p = p * 180.0 / M_PI;
    y = y * 180.0 / M_PI;
  }

  void toRadians(double &r, double &p, double &y)
  {
    r = r / 180.0 * M_PI;
    p = p / 180.0 * M_PI;
    y = y / 180.0 * M_PI;
  }
};

int main(int argc, char ** argv)
{
  //Initialize ROS
  ros::init(argc, argv,"static_transform_publisher", ros::init_options::AnonymousName);

  if(argc == 11)
  {
    ros::Duration sleeper(atof(argv[10])/1000.0);

    if (strcmp(argv[8], argv[9]) == 0)
      ROS_FATAL("target_frame and source frame are the same (%s, %s) this cannot work", argv[8], argv[9]);

    TransformSender tf_sender(atof(argv[1]), atof(argv[2]), atof(argv[3]),
                              atof(argv[4]), atof(argv[5]), atof(argv[6]), atof(argv[7]),
                              ros::Time() + sleeper, //Future dating to allow slower sending w/o timeout
                              argv[8], argv[9]);



    while(tf_sender.node_.ok())
    {
      tf_sender.send(ros::Time::now() + sleeper);
      ROS_DEBUG("Sending transform from %s with parent %s\n", argv[8], argv[9]);
      ros::spinOnce();
      sleeper.sleep();
    }

    return 0;
  } 
  else if (argc == 10)
  {
    ros::Duration sleeper(atof(argv[9])/1000.0);

    if (strcmp(argv[7], argv[8]) == 0)
      ROS_FATAL("target_frame and source frame are the same (%s, %s) this cannot work", argv[7], argv[8]);

    TransformSender tf_sender(atof(argv[1]), atof(argv[2]), atof(argv[3]),
                              atof(argv[4]), atof(argv[5]), atof(argv[6]),
                              ros::Time() + sleeper, //Future dating to allow slower sending w/o timeout
                              argv[7], argv[8]);



    while(tf_sender.node_.ok())
    {
      tf_sender.send(ros::Time::now() + sleeper);
      ROS_DEBUG("Sending transform from %s with parent %s\n", argv[7], argv[8]);
      ros::spinOnce();
      sleeper.sleep();
    }

    return 0;

  }
  else
  {
    printf("A command line utility for manually sending a transform.\n");
    printf("It will periodicaly republish the given transform. \n");
    printf("Usage: static_transform_publisher x y z yaw pitch roll frame_id child_frame_id  period(milliseconds) \n");
    printf("OR \n");
    printf("Usage: static_transform_publisher x y z qx qy qz qw frame_id child_frame_id  period(milliseconds) \n");
    printf("\nThis transform is the transform of the coordinate frame from frame_id into the coordinate frame \n");
    printf("of the child_frame_id.  \n");
    ROS_ERROR("static_transform_publisher exited due to not having the right number of arguments");
    return -1;
  }


};

