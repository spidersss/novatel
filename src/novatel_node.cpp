/*
 * The MIT License (MIT)
 * Copyright (c) vipioneers 2018
 * \author
 * \version 1.0
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a 
 * copy of this software and associated documentation files (the "Software"), 
 * to deal in the Software without restriction, including without limitation 
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, 
 * and/or sell copies of the Software, and to permit persons to whom the 
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included 
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL 
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
 * DEALINGS IN THE SOFTWARE.
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cfloat>
#include <algorithm>
#include <string>

#include <signal.h>

#include <ros/ros.h>
#include <tf/tf.h>

#ifdef WIN32
#ifdef DELETE
// ach, windows.h polluting everything again,
// clashes with autogenerated visualization_msgs/Marker.h
#undef DELETE
#endif
#endif
#include "nav_msgs/Odometry.h"
#include "sensor_msgs/NavSatFix.h"
#include "msgs_ht/Ephemeris.h"
#include "msgs_ht/L1L2Range.h"
#include "msgs_ht/Gps_Data_Ht.h"
#include "msgs_ht/Gps_Init_Data_Ht.h"

#include <boost/tokenizer.hpp>
#include <boost/thread/thread.hpp>

#include "novatel/novatel.h"
#include "novatel/novatel_node.h"

using namespace novatel;
using namespace std;

// Logging system message handlers
void handleInfoMessages(const std::string &msg) { ROS_INFO("%s", msg.c_str()); }
void handleWarningMessages(const std::string &msg) { ROS_WARN("%s", msg.c_str()); }
void handleErrorMessages(const std::string &msg) { ROS_ERROR("%s", msg.c_str()); }
void handleDebugMessages(const std::string &msg) { ROS_DEBUG("%s", msg.c_str()); }

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
//static double radians_to_degrees = 180.0 / M_PI;
static double degrees_to_radians = M_PI / 180.0;
static double degrees_square_to_radians_square = degrees_to_radians * degrees_to_radians;

static double sigma_v = 0.05; // velocity std dev in m/s

int NovatelNode::track_file_simulate_freq_ = 10; //1hz

NovatelNode::NovatelNode() : nh_("~")
{
  pub_init_data_done = 0;
  // set up logging handlers
  gps_.setLogInfoCallback(handleInfoMessages);
  gps_.setLogWarningCallback(handleWarningMessages);
  gps_.setLogErrorCallback(handleErrorMessages);
  gps_.setLogDebugCallback(handleDebugMessages);

  gps_.set_best_utm_position_callback(boost::bind(&NovatelNode::BestUtmHandler, this, _1, _2));
  gps_.set_best_velocity_callback(boost::bind(&NovatelNode::BestVelocityHandler, this, _1, _2));
  gps_.set_ins_position_velocity_attitude_callback(boost::bind(&NovatelNode::InsPvaHandler, this, _1, _2));

  //ori_track_file_channel = true;

  //signal(SIGINT, SIGINT_handler);
}

NovatelNode::~NovatelNode()
{
  this->disconnect();
}

std::string NovatelNode::track_file_output_path_xy_ = "";
std::string NovatelNode::track_file_output_path_xyv_ = "";
std::string NovatelNode::track_file_output_path_xy_hd_ = "";

bool gps_init_data_exhibition_service_cb(msgs_ht::Gps_Init_Data_Ht::Request &req, msgs_ht::Gps_Init_Data_Ht::Response &res)
{
  return 0;
}

int NovatelNode::get_ori_track_data_for_init_data()
{
  std::ifstream ori_track_file(ori_track_file_path_, std::ios::binary);

  if (!ori_track_file.is_open())
  {
    std::cout << "File " << ori_track_file_path_ << " did not open." << std::endl;
  }

  // Find file size
  ori_track_file.seekg(0, ori_track_file.end);
  size_t size = ori_track_file.tellg();

  // Put data into buffer
  unsigned char buff[size];

  //relocate the ptr of file
  ori_track_file.seekg(0, ori_track_file.beg);
  ori_track_file.read((char *)buff, size);

  // Finished with the file, close it
  ori_track_file.close();

  gps_.ReadFromFile(buff, sizeof(buff));

  return 0;
}

void NovatelNode::send_rest_locate_data_frq_func()
{
  static float th_zero = 50.0;
  static float x_zero_simulate = 0;
  static float y_zero_simulate = 0;
  std::ifstream track_file(track_file_input_path_for_test_simulate_.c_str(), std::ios::binary);

  if (!track_file.is_open())
  {
    std::cout << "track File " << track_file_input_path_for_test_simulate_ << " did not open." << std::endl;
  }
  else
    std::cout << "track File simulate " << track_file_input_path_for_test_simulate_ << " open ok!!!!!" << std::endl;

  std::vector<track_data> track_datas_t;
  string line;
  vector<string> strings_in_line;
  track_data track_data_temp;

  //tokenize:切分词
  typedef boost::tokenizer<boost::char_separator<char>> tokenizer;
  boost::char_separator<char> sep(" ");

  while (getline(track_file, line)) //会自动把\n换行符去掉
  {
    tokenizer tokens(line, sep);
    tokenizer::iterator current_token = tokens.begin();
    track_data_temp.num = atof((*(current_token++)).c_str());
    track_data_temp.x = atof((*(current_token++)).c_str());
    track_data_temp.y = atof((*(current_token++)).c_str());
    track_data_temp.h = atof((*(current_token)).c_str());
    cout << "num " << track_data_temp.num << " x " << track_data_temp.x << " y " << track_data_temp.y << endl;
    if (x_zero_simulate >= 0.0)
    {
      if (fabs(sqrt(pow(track_data_temp.x, 2) + pow(track_data_temp.y, 2))) >= th_zero)
      {
        x_zero_simulate = track_data_temp.x;
        y_zero_simulate = track_data_temp.y;
        cout << "zero track_data_temp.x" << track_data_temp.x << endl;
        cout << "zero track_data_temp.y" << track_data_temp.y << endl;
      }
      else
        x_zero_simulate = -1.0; //判断一次即可
    }
    track_datas_t.push_back(track_data_temp);
  }

  cout << "track_datas_t size is:" << track_datas_t.size() << endl;

  double num = 2;
  while (1)
  {
    for (vector<track_data>::iterator it = track_datas_t.begin(); it != track_datas_t.end(); ++it)
    {
      //只保留小数点后num个位数
      gps_data_ht_.odom.pose.pose.position.x =
          ((float)((int)((
                             (it->x - x_zero_simulate) //被转换的数据或者表达式放在这里
                             + (5 / pow(10, num))) *
                         pow(10, num)))) /
          pow(10, num);
      gps_data_ht_.odom.pose.pose.position.y =
          ((float)((int)((
                             (it->y - y_zero_simulate) //被转换的数据或者表达式放在这里
                             + (5 / pow(10, num))) *
                         pow(10, num)))) /
          pow(10, num);
      gps_data_ht_.heading =
          ((float)((int)((
                             (it->h) //被转换的数据或者表达式放在这里
                             + (5 / pow(10, num))) *
                         pow(10, num)))) /
          pow(10, num);

      // cout << "simulate publishing"
      //         << " x: " << gps_data_ht_.odom.pose.pose.position.x
      //         << " y: " << gps_data_ht_.odom.pose.pose.position.y
      //         << endl;

      ROS_INFO_STREAM("simulate pubing"
                      << " x: " << gps_data_ht_.odom.pose.pose.position.x << endl
                      << " y: " << gps_data_ht_.odom.pose.pose.position.y << endl
                      << " h: " << gps_data_ht_.heading << endl);

      exhibition_odom_publisher_.publish(gps_data_ht_);
      usleep(100 * 1000); //fucn sleep uinit is: 1 sec
      //ussleep(1); //fucn sleep uinit is: 1 sec
    }
  }
}

void NovatelNode::run()
{
  if (!this->getParameters())
    return;

  if (CODE_STATE == ori_gps_file_convert_)
  {
    get_ori_track_data_for_init_data();
  }

  this->odom_publisher_ = nh_.advertise<nav_msgs::Odometry>(odom_topic_, 0);
  this->exhibition_odom_publisher_ = nh_.advertise<msgs_ht::Gps_Data_Ht>(exhibition_odom_topic_, 0);
  this->nav_sat_fix_publisher_ = nh_.advertise<sensor_msgs::NavSatFix>("/gps_fix", 0);

  std::cout << "  latitude_zero: " << Novatel::latitude_zero << std::endl
            << "  longitude_zero: " << Novatel::longitude_zero << std::endl
            << "  x_zero: " << Novatel::x_zero << std::endl
            << "  y_zero: " << Novatel::y_zero << std::endl
            << std::endl;

  if ((simulate_debug_min_ <= CODE_STATE) && (CODE_STATE <= simulate_debug_max_))
  {
    ROS_INFO("simulate_mode!!!!!!!!!!!!!!!!!!!!!!");

    boost::shared_ptr<boost::thread> read_thread_ptr_ =
        boost::shared_ptr<boost::thread>(new boost::thread(boost::bind(&NovatelNode::send_rest_locate_data_frq_func, this)));
  }
  else
  {
    if (port_ != "")
    {
      gps_.Connect(port_, baudrate_);

      // configure default log sets
      if (gps_default_logs_period_ > 0)
      {
        // request default set of gps logs at given rate
        // convert rate to string
        ROS_INFO("Requesting default GPS messages: BESTUTMB, BESTVELB");
        std::stringstream default_logs;
        default_logs.precision(2);
        default_logs << "BESTUTMB ONTIME " << std::fixed << gps_default_logs_period_ << ";";
        default_logs << "BESTVELB ONTIME " << std::fixed << gps_default_logs_period_;
        gps_.ConfigureLogs(default_logs.str());
      }

      if (span_default_logs_period_ > 0)
      {
        ROS_INFO("Requesting default SPAN messages: INSPVAB, INSCOVB");
        // request default set of gps logs at given rate
        // convert rate to string
        std::stringstream default_logs;
        default_logs.precision(2);
        default_logs << "INSPVAB ONTIME " << std::fixed << span_default_logs_period_ << ";";
        default_logs << "INSCOVB ONTIME " << std::fixed << span_default_logs_period_;
        //ROS_INFO_STREAM("default logs: " << default_logs);
        gps_.ConfigureLogs(default_logs.str());
      }

      if (!ephem_log_.empty())
      {
        std::stringstream default_logs;
        default_logs << "GPSEPHEMB " << std::fixed << ephem_log_;
        gps_.ConfigureLogs(default_logs.str());
      }

      if (range_default_logs_period_ > 0)
      {
        std::stringstream default_logs;
        default_logs.precision(2);
        default_logs << "RANGECMPB ONTIME " << std::fixed << range_default_logs_period_ << ";";
        gps_.ConfigureLogs(default_logs.str());
      }

      if (psrpos_default_logs_period_ > 0)
      {
        std::stringstream default_logs;
        default_logs.precision(2);
        default_logs << "PSRPOSB ONTIME " << std::fixed << psrpos_default_logs_period_ << ";";
        gps_.ConfigureLogs(default_logs.str());
      }

      // configure additional logs
      //把在launch中设置的需要往惯导里边配置的log 命令 往惯导设置
      //config all the cmd setted in launch file
      if (log_commands_ != "")
      {
        gps_.ConfigureLogs(log_commands_);
      }

      // configure serial port (for rtk generally)
      if (configure_port_ != "")
      {
        // string should contain com_port,baud_rate,rx_mode,tx_mode
        // parse message body by tokening on ","
        //tokenize:切分词
        typedef boost::tokenizer<boost::char_separator<char>> tokenizer;
        boost::char_separator<char> sep(",");
        tokenizer tokens(configure_port_, sep);
        // set up iterator to go through token list
        tokenizer::iterator current_token = tokens.begin();
        std::string num_comps_string = *(current_token);
        //int number_components = atoi(num_comps_string.c_str());
        // make sure the correct number of tokens were found
        int token_count = 0;
        for (current_token = tokens.begin(); current_token != tokens.end(); ++current_token)
        {
          token_count++;
        }

        if (token_count != 4)
        {
          ROS_ERROR_STREAM("Incorrect number of tokens in configure port parameter: " << configure_port_);
        }
        else
        {
          current_token = tokens.begin();
          std::string com_port = *(current_token++);
          int baudrate = atoi((current_token++)->c_str());
          std::string rx_mode = *(current_token++);
          std::string tx_mode = *(current_token++);

          //come2 for rtk receiver
          ROS_INFO_STREAM("Configure com port baud rate and interface mode for " << com_port << ".");
          gps_.ConfigureInterfaceMode(com_port, rx_mode, tx_mode);
          gps_.ConfigureBaudRate(com_port, baudrate);
        }
      }
    }
    else
    {
      ROS_INFO("port is null!!!!!!!! ");
    }
  }

  ros::spin(); //programe will hang up here

} // function

void NovatelNode::BestVelocityHandler(Velocity &vel, double &timestamp)
{
  ROS_DEBUG("Received BestVel");
  cur_velocity_ = vel;
}

void NovatelNode::BestUtmHandler(UtmPosition &pos, double &timestamp)
{
  ROS_DEBUG("Received BestUtm");

  cur_utm_bestpos_ = pos;

  //#######################cur_odom_
  nav_msgs::Odometry cur_odom_;
  cur_odom_.header.stamp = ros::Time::now();
  cur_odom_.header.frame_id = "/odom";
  cur_odom_.pose.pose.position.x = pos.easting;
  cur_odom_.pose.pose.position.y = pos.northing;
  cur_odom_.pose.pose.position.z = pos.height;
  // covariance representation given in REP 103
  //http://www.ros.org/reps/rep-0103.html#covariance-representation
  // (x, y, z, rotation about X axis, rotation about Y axis, rotation about Z axis)
  // row major
  cur_odom_.pose.covariance[0] = pos.easting_standard_deviation * pos.easting_standard_deviation;
  cur_odom_.pose.covariance[7] = pos.northing_standard_deviation * pos.northing_standard_deviation;
  cur_odom_.pose.covariance[14] = pos.height_standard_deviation * pos.height_standard_deviation;
  // have no way of knowing roll and pitch with just GPS
  cur_odom_.pose.covariance[21] = DBL_MAX;
  cur_odom_.pose.covariance[28] = DBL_MAX;

  // see if there is a recent velocity message
  if ((cur_velocity_.header.gps_week == pos.header.gps_week) && (cur_velocity_.header.gps_millisecs == pos.header.gps_millisecs))
  {
    cur_odom_.twist.twist.linear.x = cur_velocity_.horizontal_speed * cos(cur_velocity_.track_over_ground * degrees_to_radians);
    cur_odom_.twist.twist.linear.y = cur_velocity_.horizontal_speed * sin(cur_velocity_.track_over_ground * degrees_to_radians);
    cur_odom_.twist.twist.linear.z = cur_velocity_.vertical_speed;

    cur_odom_.pose.pose.orientation = tf::createQuaternionMsgFromYaw(
        psi2theta(cur_velocity_.track_over_ground * degrees_to_radians));

    // if i have a fix, velocity std, dev is constant
    if (cur_velocity_.position_type > NONE)
    {
      // yaw covariance
      double heading_std_dev = sigma_v / cur_velocity_.horizontal_speed;
      cur_odom_.pose.covariance[35] = heading_std_dev * heading_std_dev;
      // x and y velocity covariance
      cur_odom_.twist.covariance[0] = sigma_v * sigma_v;
      cur_odom_.twist.covariance[7] = sigma_v * sigma_v;
    }
    else
    {
      cur_odom_.pose.covariance[35] = DBL_MAX;
      cur_odom_.twist.covariance[0] = DBL_MAX;
      cur_odom_.twist.covariance[7] = DBL_MAX;
    }
  }
  odom_publisher_.publish(cur_odom_);

  //#####################
  struct tm *local_time;
  time_t current_time;
  struct timeval tv;
  struct timezone tz;
  time(&current_time);
  local_time = localtime(&current_time);
  gettimeofday(&tv, &tz);
  double num = 2;
  gps_data_ht_.heading = sqrt(pow(cur_odom_.twist.twist.angular.x, 2) + pow(cur_odom_.twist.twist.angular.y, 2));
  gps_data_ht_.velocity = sqrt(pow(cur_odom_.twist.twist.linear.x, 2) + pow(cur_odom_.twist.twist.linear.y, 2));
  gps_data_ht_.velocity =
      ((float)((int)((
                         (gps_data_ht_.velocity) + (5 / pow(10, num))) *
                     pow(10, num)))) /
      pow(10, num);

  gps_data_ht_.odom.pose.pose.position.x = cur_odom_.pose.pose.position.x - Novatel::x_zero;
  gps_data_ht_.odom.pose.pose.position.y = cur_odom_.pose.pose.position.y - Novatel::y_zero;
  gps_data_ht_.odom.pose.pose.position.z = cur_odom_.pose.pose.position.z;
  exhibition_odom_publisher_.publish(gps_data_ht_);

  //#####################

  if (CODE_STATE == test_catch_track_file_only_xyv_)
  {
    static int track_point_cnt = 0;
    struct tm *local_time;
    time_t current_time;
    struct timeval tv;
    struct timezone tz;
    time(&current_time);
    local_time = localtime(&current_time);
    gettimeofday(&tv, &tz);

    //cost 5us

    std::ofstream track_file_out(NovatelNode::track_file_output_path_xyv_.c_str(), ios::app | ios::out);
    track_file_out.setf(std::ios::fixed, ios::floatfield);
    //track_file_out.precision(5);
    if (!track_file_out.is_open())
    {
      cout << "open fiile fail" << endl;
    }
    else
      track_file_out << setprecision(2)
                     << track_point_cnt++ << " "
                     << gps_data_ht_.odom.pose.pose.position.x << " "
                     << gps_data_ht_.odom.pose.pose.position.y << " "
                     << gps_data_ht_.velocity << " "
                     << endl;

    std::cout << local_time->tm_year + 1900 << "-"
              << local_time->tm_mon + 1 << "-"
              << local_time->tm_mday << " "
              << local_time->tm_hour << ":"
              << local_time->tm_min << ":"
              << local_time->tm_sec << "."
              << tv.tv_usec << "," << std::endl

              << "  x_zero: " << Novatel::x_zero << std::endl
              << "  y_zero: " << Novatel::y_zero << std::endl
              << "  x: " << gps_data_ht_.odom.pose.pose.position.x << std::endl
              << "  y: " << gps_data_ht_.odom.pose.pose.position.y << std::endl
              << "  z: " << gps_data_ht_.odom.pose.pose.position.z << std::endl
              << std::endl;
  }
  else
  {
    std::cout << "["
              << local_time->tm_year + 1900 << "-"
              << local_time->tm_mon + 1 << "-"
              << local_time->tm_mday << " "
              << local_time->tm_hour << ":"
              << local_time->tm_min << ":"
              << local_time->tm_sec << "."
              << tv.tv_usec << "]" << endl
              << "utm pubing, "
              << " [x]:" << gps_data_ht_.odom.pose.pose.position.x << ","
              << " [y]:" << gps_data_ht_.odom.pose.pose.position.y << ","
              << " [z]:" << gps_data_ht_.odom.pose.pose.position.z
              << " [hd]:" << gps_data_ht_.heading   //heading
              << " [v]:" << gps_data_ht_.velocity   //velocity
              << " [xz]:" << Novatel::x_zero << "," //x zero
              << " [yz]:" << Novatel::y_zero << "," //y zero
              << std::endl;
  }
}

void NovatelNode::InsPvaHandler(InsPositionVelocityAttitude &ins_pva, double &timestamp)
{
  //ROS_INFO("Received inspva.");

  // convert pva position to UTM
  double northing, easting;
  int zoneNum;
  bool north;

  struct tm *local_time;
  time_t current_time;
  struct timeval tv;
  struct timezone tz;
  time(&current_time);
  local_time = localtime(&current_time);
  gettimeofday(&tv, &tz);
  static int need_show_zero_lat_long = 1;
  
  if (need_show_zero_lat_long)
  {
    need_show_zero_lat_long = 0;
    cout << setprecision(10) << "lat @ start point:" << ins_pva.latitude << " long @ start point:"<<ins_pva.longitude << endl;
  }
  //real time convert the locate data
  gps_.ConvertLLaUTM(ins_pva.latitude, ins_pva.longitude, &northing, &easting, &zoneNum, &north);

  sensor_msgs::NavSatFix sat_fix;
  sat_fix.header.stamp = ros::Time::now();
  sat_fix.header.frame_id = "/odom";

  if (ins_pva.status == INS_SOLUTION_GOOD)
    sat_fix.status.status = sensor_msgs::NavSatStatus::STATUS_FIX;
  else
    sat_fix.status.status = sensor_msgs::NavSatStatus::STATUS_NO_FIX;

  sat_fix.status.service = sensor_msgs::NavSatStatus::SERVICE_GPS;

  sat_fix.latitude = ins_pva.latitude;
  sat_fix.longitude = ins_pva.longitude;
  sat_fix.altitude = ins_pva.height;

  sat_fix.position_covariance_type = sensor_msgs::NavSatFix::COVARIANCE_TYPE_DIAGONAL_KNOWN;

  nav_sat_fix_publisher_.publish(sat_fix);

  nav_msgs::Odometry cur_odom_;
  cur_odom_.header.stamp = sat_fix.header.stamp;
  cur_odom_.header.frame_id = "/odom";
  cur_odom_.pose.pose.position.x = easting;
  cur_odom_.pose.pose.position.y = northing;
  cur_odom_.pose.pose.position.z = ins_pva.height;
  cur_odom_.pose.pose.orientation = tf::createQuaternionMsgFromRollPitchYaw(ins_pva.roll * degrees_to_radians,
                                                                            ins_pva.pitch * degrees_to_radians,
                                                                            psi2theta(ins_pva.azimuth * degrees_to_radians));

  cur_odom_.twist.twist.linear.x = ins_pva.east_velocity;
  cur_odom_.twist.twist.linear.y = ins_pva.north_velocity;
  cur_odom_.twist.twist.linear.z = ins_pva.up_velocity;

  cur_odom_.twist.twist.angular.x = ins_pva.roll;
  cur_odom_.twist.twist.angular.y = ins_pva.pitch;
  cur_odom_.twist.twist.angular.z = ins_pva.azimuth;

  gps_data_ht_.heading = cur_odom_.twist.twist.angular.z;
  gps_data_ht_.velocity = sqrt(pow(cur_odom_.twist.twist.linear.x, 2) + pow(cur_odom_.twist.twist.linear.y, 2));
  double num = 2;
  gps_data_ht_.velocity =
      ((float)((int)((
                         (gps_data_ht_.velocity) + (5 / pow(10, num))) *
                     pow(10, num)))) /
      pow(10, num);
  gps_data_ht_.odom.pose.pose.position.x = cur_odom_.pose.pose.position.x - Novatel::x_zero;
  gps_data_ht_.odom.pose.pose.position.y = cur_odom_.pose.pose.position.y - Novatel::y_zero;
  gps_data_ht_.odom.pose.pose.position.z = cur_odom_.pose.pose.position.z;
  exhibition_odom_publisher_.publish(gps_data_ht_);

  if (CODE_STATE == test_catch_track_file_xy_hd_)
  {
    static int track_point_cnt = 0;

    std::ofstream track_file_out(NovatelNode::track_file_output_path_xy_hd_.c_str(), ios::app | ios::out);
    track_file_out.setf(std::ios::fixed, ios::floatfield);
    //track_file_out.precision(5);
    if (!track_file_out.is_open())
    {
      cout << "open track_file_out:" << track_file_output_path_xy_hd_ << " failed!!!" << endl;
    }
    else
    {
      track_file_out << setprecision(2)
                     << track_point_cnt++ << " "
                     << gps_data_ht_.odom.pose.pose.position.x << " "
                     << gps_data_ht_.odom.pose.pose.position.y << " "
                     << gps_data_ht_.heading << " "
                     << endl;
    }
    cout << "saving xyd to file" << NovatelNode::track_file_output_path_xy_hd_.c_str() << endl;
  }
  std::cout << "["
            << local_time->tm_year + 1900 << "-"
            << local_time->tm_mon + 1 << "-"
            << local_time->tm_mday << " "
            << local_time->tm_hour << ":"
            << local_time->tm_min << ":"
            << local_time->tm_sec << "."
            << tv.tv_usec << "]"
            << "inspva pubing, "
            << " [x]:" << gps_data_ht_.odom.pose.pose.position.x << ","
            << " [y]:" << gps_data_ht_.odom.pose.pose.position.y << ","
            << " [z]:" << gps_data_ht_.odom.pose.pose.position.z << ","
            << " [hd]:" << gps_data_ht_.heading << "," //heading
            << " [v]:" << gps_data_ht_.velocity << "," //velocity
            << " [xz]:" << Novatel::x_zero << ","      //x zero
            << " [yz]:" << Novatel::y_zero << ","      //y zero
            << std::endl;

  // TODO: add covariance

  // see if there is a matching ins covariance message
  if ((cur_ins_cov_.gps_week == ins_pva.gps_week) && (cur_ins_cov_.gps_millisecs == ins_pva.gps_millisecs))
  {

    cur_odom_.pose.covariance[0] = cur_ins_cov_.position_covariance[0];
    cur_odom_.pose.covariance[1] = cur_ins_cov_.position_covariance[1];
    cur_odom_.pose.covariance[2] = cur_ins_cov_.position_covariance[2];
    cur_odom_.pose.covariance[6] = cur_ins_cov_.position_covariance[3];
    cur_odom_.pose.covariance[7] = cur_ins_cov_.position_covariance[4];
    cur_odom_.pose.covariance[8] = cur_ins_cov_.position_covariance[5];
    cur_odom_.pose.covariance[12] = cur_ins_cov_.position_covariance[6];
    cur_odom_.pose.covariance[13] = cur_ins_cov_.position_covariance[7];
    cur_odom_.pose.covariance[14] = cur_ins_cov_.position_covariance[8];

    cur_odom_.pose.covariance[21] = cur_ins_cov_.attitude_covariance[0] * degrees_square_to_radians_square;
    cur_odom_.pose.covariance[22] = cur_ins_cov_.attitude_covariance[1] * degrees_square_to_radians_square;
    cur_odom_.pose.covariance[23] = cur_ins_cov_.attitude_covariance[2] * degrees_square_to_radians_square;
    cur_odom_.pose.covariance[27] = cur_ins_cov_.attitude_covariance[3] * degrees_square_to_radians_square;
    cur_odom_.pose.covariance[28] = cur_ins_cov_.attitude_covariance[4] * degrees_square_to_radians_square;
    cur_odom_.pose.covariance[29] = cur_ins_cov_.attitude_covariance[5] * degrees_square_to_radians_square;
    cur_odom_.pose.covariance[33] = cur_ins_cov_.attitude_covariance[6] * degrees_square_to_radians_square;
    cur_odom_.pose.covariance[34] = cur_ins_cov_.attitude_covariance[7] * degrees_square_to_radians_square;
    cur_odom_.pose.covariance[35] = cur_ins_cov_.attitude_covariance[8] * degrees_square_to_radians_square;

    cur_odom_.twist.covariance[0] = cur_ins_cov_.velocity_covariance[0];
    cur_odom_.twist.covariance[1] = cur_ins_cov_.velocity_covariance[1];
    cur_odom_.twist.covariance[2] = cur_ins_cov_.velocity_covariance[2];
    cur_odom_.twist.covariance[6] = cur_ins_cov_.velocity_covariance[3];
    cur_odom_.twist.covariance[7] = cur_ins_cov_.velocity_covariance[4];
    cur_odom_.twist.covariance[8] = cur_ins_cov_.velocity_covariance[5];
    cur_odom_.twist.covariance[12] = cur_ins_cov_.velocity_covariance[6];
    cur_odom_.twist.covariance[13] = cur_ins_cov_.velocity_covariance[7];
    cur_odom_.twist.covariance[14] = cur_ins_cov_.velocity_covariance[8];
  }

  odom_publisher_.publish(cur_odom_);
}

void NovatelNode::SIGINT_handler(int sig)
{ // can be called asynchronously
  ROS_INFO_STREAM("ctrl c");
  //gps_.disconnect();
}

bool NovatelNode::getParameters()
{
  name_ = ros::this_node::getName();

  //nh_.param("nav_sat_fix_topic", nav_sat_fix_topic_, std::string("/gps_fix"));

  nh_.param("odom_topic", odom_topic_, std::string("/gps_odom"));
  ROS_INFO_STREAM(name_ << ": Odom Topic: " << odom_topic_);

  nh_.param("exhibition_odom_topic", exhibition_odom_topic_, std::string("/gps_odom_exhibition"));
  ROS_INFO_STREAM(name_ << ": Exhibition Odom Topic: " << exhibition_odom_topic_);

  nh_.param("port", port_, std::string(""));
  if (port_ != "")
  {
    ROS_INFO_STREAM(name_ << ": serial Port: " << port_);
  }

  nh_.param("baudrate", baudrate_, 9600);
  ROS_INFO_STREAM(name_ << ": Baudrate: " << baudrate_);

  //nh_.param("log_commands", log_commands_, std::string("BESTUTMB ONTIME 1.0"));
  nh_.param("log_commands", log_commands_, std::string(""));
  if (log_commands_ != "")
  {
    ROS_INFO_STREAM(name_ << ": Log Commands: " << log_commands_);
  }

  nh_.param("configure_port", configure_port_, std::string(""));
  if (configure_port_ != "")
  {
    ROS_INFO_STREAM(name_ << ": Configure port: " << configure_port_);
  }

  nh_.param("latitude_zero", Novatel::latitude_zero, 0.0);
  if (Novatel::latitude_zero != 0.0)
  {
    ROS_INFO_STREAM(name_ << ": latitude_zero: " << Novatel::latitude_zero);
  }

  nh_.param("longitude_zero", Novatel::longitude_zero, 0.0);
  if (Novatel::longitude_zero != 0.0)
  {
    ROS_INFO_STREAM(name_ << ": longitude_zero: " << Novatel::longitude_zero);
  }

  if ((Novatel::longitude_zero != 0.0) && (Novatel::latitude_zero != 0.0))
  {
    cout << "get x_zero and y_zero by longitude_zero and longitude_zero" << endl;
    gps_.ConvertLLaUTM(Novatel::latitude_zero, Novatel::longitude_zero, &Novatel::y_zero, &Novatel::x_zero,
                       &Novatel::zoneNum, &Novatel::north);
    //Novatel::x_zero -= 18363.90;
    //Novatel::y_zero -= 10808;
  }

  nh_.param("height_zero", Novatel::z_zero, 0.0);
  if (Novatel::z_zero != 0.0)
  {
    ROS_INFO_STREAM(name_ << ": height_zero: " << Novatel::z_zero);
  }

  nh_.param("ori_track_file_path", ori_track_file_path_, std::string(""));
  if (ori_track_file_path_ != "")
  {
    CODE_STATE = ori_gps_file_convert_;
    ROS_INFO_STREAM(name_ << ": ori_track_file_path_: " << ori_track_file_path_);
  }

  nh_.param("track_file_output_path_xy", track_file_output_path_xy_, std::string(""));
  if (track_file_output_path_xy_ != "")
  {
    ROS_INFO_STREAM(name_ << ": track_file_output_path_xy_: " << track_file_output_path_xy_);
    CODE_STATE = test_catch_track_file_only_xy_;
    log_commands_ = "bestposb ontime 0.1";
  }

  nh_.param("track_file_output_path_xyv", track_file_output_path_xyv_, std::string(""));
  if (track_file_output_path_xyv_ != "")
  {
    ROS_INFO_STREAM(name_ << ": track_file_output_path_xyv: " << track_file_output_path_xyv_);
    CODE_STATE = test_catch_track_file_only_xyv_;
    log_commands_ = "bestutmb ontime 0.1; bestvelb ontime 0.1";
  }

  nh_.param("track_file_output_path_xy_hd", track_file_output_path_xy_hd_, std::string(""));
  if (track_file_output_path_xy_hd_ != "")
  {
    ROS_INFO_STREAM(name_ << ": track_file_output_path_xy_hd_: " << track_file_output_path_xy_hd_);
    CODE_STATE = test_catch_track_file_xy_hd_;
    log_commands_ = "inspvab ontime 0.1";
  }

  nh_.param("track_file_input_path_for_test_simulate", track_file_input_path_for_test_simulate_, std::string(""));
  if (track_file_input_path_for_test_simulate_ != "")
  {
    CODE_STATE = simulate_multi_nodes_debug_;
    ROS_INFO_STREAM(name_ << ": track_file_input_path_for_test_simulate_: " << track_file_input_path_for_test_simulate_);
  }

  nh_.param("track_file_simulate_freq", NovatelNode::track_file_simulate_freq_, 0);
  if (NovatelNode::track_file_simulate_freq_ != 0)
  {
    ROS_INFO_STREAM(name_ << ": track_file_simulate_freq_: " << NovatelNode::track_file_simulate_freq_);
  }

  return true;
}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "novatel_node");
  cout << "in main" << endl;
  NovatelNode node;

  node.run();

  return 0;
}
