/**
 * @file core.cpp
 * @author Friedl Jakob (friedl.jak@gmail.com)
 * @brief This file contains the main functionality for controlling a mecanum
 * robot using micro-ROS via Serial.
 * @version 1.1
 * @date 2023-08-21
 *
 * @copyright Copyright (c) 2023
 *
 * @todo Refactor the code to use ROS data types instead of Eigen.
 * @todo Add joint state controller (similar to velocity controller).
 *
 */

#define DEBUG
#define DEBUG_TIME

#include <Arduino.h>
#include <ArduinoEigen.h>
#include <micro_ros_platformio.h>

#include "rcl_checks.h"
#include <rcl/rcl.h>
#include <rclc/executor.h>

#include <rclc/rclc.h>
#include <rosidl_runtime_c/string_functions.h>

#include <geometry_msgs/msg/twist.h>
#include <nav_msgs/msg/odometry.h>
#include <sensor_msgs/msg/joint_state.h>

#ifdef DEBUG
#include <diagnostic_msgs/msg/diagnostic_status.h>
#endif

#include "conf_hardware.h"
// #include "conf_network.h"
#include "motor-control/encoder.hpp"
#include "motor-control/motor-drivers/l298n_motor_driver.hpp"
#include "motor-control/pid_motor_controller.hpp"
#include "motor-control/simple_motor_controller.hpp"
#include "velocity_controller.hpp"

L298NMotorDriver driver_M0(M0_IN1, M0_IN2, M0_ENA, M0_PWM_CNL);
L298NMotorDriver driver_M1(M1_IN1, M1_IN2, M1_ENA, M1_PWM_CNL);
L298NMotorDriver driver_M2(M2_IN1, M2_IN2, M2_ENA, M2_PWM_CNL);
L298NMotorDriver driver_M3(M3_IN1, M3_IN2, M3_ENA, M3_PWM_CNL);

HalfQuadEncoder encoder_M0(M0_ENC_A, M0_ENC_B, M0_ENC_RESOLUTION);
HalfQuadEncoder encoder_M1(M1_ENC_A, M1_ENC_B, M1_ENC_RESOLUTION);
HalfQuadEncoder encoder_M2(M2_ENC_A, M2_ENC_B, M2_ENC_RESOLUTION);
HalfQuadEncoder encoder_M3(M3_ENC_A, M3_ENC_B, M3_ENC_RESOLUTION);

double base_kp = 0.105;
// double modifier_kp = 1.0;
double base_ki = 0.125;
double modifier_ki_linear = 2.0;
double modifier_ki_rotational = 1.1;
double base_kd = 0.005;
// double modifier_kd = 1.0;
double max_expected_sampling_time = 0.2;
double max_integral = 5.2;

PIDController controller_M0(base_kp, base_ki, base_kd,
                            max_expected_sampling_time, max_integral);
PIDController controller_M1(base_kp, base_ki, base_kd,
                            max_expected_sampling_time, max_integral);
PIDController controller_M2(base_kp, base_ki, base_kd,
                            max_expected_sampling_time, max_integral);
PIDController controller_M3(base_kp, base_ki, base_kd,
                            max_expected_sampling_time, max_integral);

// LowPassFilter encoder_input_filter_M0 = LowPassFilter(100.0, 0.01);
// LowPassFilter encoder_input_filter_M1 = LowPassFilter(100.0, 0.01);
// LowPassFilter encoder_input_filter_M2 = LowPassFilter(100.0, 0.01);
// LowPassFilter encoder_input_filter_M3 = LowPassFilter(100.0, 0.01);

NoFilter encoder_input_filter_M0 = NoFilter();
NoFilter encoder_input_filter_M1 = NoFilter();
NoFilter encoder_input_filter_M2 = NoFilter();
NoFilter encoder_input_filter_M3 = NoFilter();

// MovingAverageFilter motor_output_filter_M0 = MovingAverageFilter(2);
// MovingAverageFilter motor_output_filter_M1 = MovingAverageFilter(2);
// MovingAverageFilter motor_output_filter_M2 = MovingAverageFilter(2);
// MovingAverageFilter motor_output_filter_M3 = MovingAverageFilter(2);

// LowPassFilter motor_output_filter_M0 = LowPassFilter(1.0, 0.2);
// LowPassFilter motor_output_filter_M1 = LowPassFilter(1.0, 0.2);
// LowPassFilter motor_output_filter_M2 = LowPassFilter(1.0, 0.2);
// LowPassFilter motor_output_filter_M3 = LowPassFilter(1.0, 0.2);

NoFilter motor_output_filter_M0 = NoFilter();
NoFilter motor_output_filter_M1 = NoFilter();
NoFilter motor_output_filter_M2 = NoFilter();
NoFilter motor_output_filter_M3 = NoFilter();

static double MIN_OUTPUT = 0.35;

PIDMotorController motor_controller_M0(driver_M0, encoder_M0, controller_M0,
                                       encoder_input_filter_M0,
                                       motor_output_filter_M0, MIN_OUTPUT);
PIDMotorController motor_controller_M1(driver_M1, encoder_M1, controller_M1,
                                       encoder_input_filter_M1,
                                       motor_output_filter_M1, MIN_OUTPUT);
PIDMotorController motor_controller_M2(driver_M2, encoder_M2, controller_M2,
                                       encoder_input_filter_M2,
                                       motor_output_filter_M2, MIN_OUTPUT);
PIDMotorController motor_controller_M3(driver_M3, encoder_M3, controller_M3,
                                       encoder_input_filter_M3,
                                       motor_output_filter_M3, MIN_OUTPUT);

MotorControllerManager motor_control_manager{
    {&motor_controller_M0, &motor_controller_M1, &motor_controller_M2,
     &motor_controller_M3}};

MecanumKinematics4W kinematics(WHEEL_RADIUS, WHEEL_BASE, TRACK_WIDTH);
VelocityController robot_controller(motor_control_manager, &kinematics);

MovingAverageFilter cmd_vel_filter_x = MovingAverageFilter(2);
MovingAverageFilter cmd_vel_filter_y = MovingAverageFilter(2);
MovingAverageFilter cmd_vel_filter_rot = MovingAverageFilter(4);

Eigen::Matrix<double, 3, 1> smoothed_cmd_vel;

rcl_subscription_t cmd_vel_subscriber;
rcl_publisher_t odom_publisher;
rcl_publisher_t joint_state_publisher, wanted_joint_state_publisher;
#ifdef DEBUG
rcl_publisher_t diagnostic_publisher;
#endif

geometry_msgs__msg__Twist twist_msg;
nav_msgs__msg__Odometry odom_msg;
sensor_msgs__msg__JointState joint_state_msg, wanted_joint_state_msg;

#ifdef DEBUG
diagnostic_msgs__msg__DiagnosticStatus diagnostic_msg;
#endif

rclc_executor_t executor;
rclc_support_t support;
rcl_allocator_t allocator;
rcl_node_t node;

unsigned long last_time = 0;
Eigen::Vector3d pose = Eigen::Vector3d::Zero();

unsigned long last_time_sync_ms = 0;
unsigned long last_time_sync_ns = 0;
const unsigned long time_sync_interval = 1000;
const int timeout_ms = 500;
int64_t synced_time_ms = 0;
int64_t synced_time_ns = 0;

#ifdef DEBUG
#ifdef DEBUG_TIME
unsigned long last_time_debug = 0;
#endif
#endif

/**
 * @brief Callback function for handling incoming cmd_vel (velocity command)
 * messages.
 *
 * @param msgin Pointer to the received geometry_msgs__msg__Twist message.
 *
 * @note The Twist message has following structure:
 *
 * std_msgs/Header header
 * geometry_msgs/Vector3 linear
 * geometry_msgs/Vector3 angular
 *
 */
void cmd_vel_subscription_callback(const void* msgin)
{
    const auto* msg = reinterpret_cast<const geometry_msgs__msg__Twist*>(msgin);

    smoothed_cmd_vel(0) = cmd_vel_filter_x.update(msg->linear.x);
    smoothed_cmd_vel(1) = cmd_vel_filter_y.update(msg->linear.y);
    smoothed_cmd_vel(2) = cmd_vel_filter_rot.update(msg->angular.z);

    // Based on max velocity multiply the ki value by a factor
    if (abs(smoothed_cmd_vel(0)) > 0.5 || abs(smoothed_cmd_vel(1)) > 0.5)
    {
        controller_M0.set_ki(base_ki * modifier_ki_linear);
        controller_M1.set_ki(base_ki * modifier_ki_linear);
        controller_M2.set_ki(base_ki * modifier_ki_linear);
        controller_M3.set_ki(base_ki * modifier_ki_linear);
    }
    else if (abs(smoothed_cmd_vel(2)) > 1.0)
    {
        controller_M0.set_ki(base_ki * modifier_ki_rotational);
        controller_M1.set_ki(base_ki * modifier_ki_rotational);
        controller_M2.set_ki(base_ki * modifier_ki_rotational);
        controller_M3.set_ki(base_ki * modifier_ki_rotational);
    }
    else
    {
        controller_M0.set_ki(base_ki);
        controller_M1.set_ki(base_ki);
        controller_M2.set_ki(base_ki);
        controller_M3.set_ki(base_ki);
    }

    robot_controller.set_latest_command(smoothed_cmd_vel);
}

#ifdef DEBUG
/**
 * @brief Publishes a diagnostic message with the given message.
 *
 * @param message The message to publish.
 */
void publishDiagnosticMessage(const char* message)
{
    diagnostic_msg.level = diagnostic_msgs__msg__DiagnosticStatus__STALE;
    diagnostic_msg.message.data = (char*)message;
    diagnostic_msg.message.size = strlen(diagnostic_msg.message.data);
    diagnostic_msg.message.capacity = diagnostic_msg.message.size + 1;

    RCSOFTCHECK(rcl_publish(&diagnostic_publisher, &diagnostic_msg, NULL));
}
#endif

bool performInitializationWithFeedback(std::function<rcl_ret_t()> initFunction)
{
    while (true)
    {
        if (initFunction() == RCL_RET_OK)
        {
            return true; // Initialization successful
        }
        else
        {
            // Flash LED to indicate failure
            digitalWrite(LED_BUILTIN, HIGH);
            delay(100);
            digitalWrite(LED_BUILTIN, LOW);
            delay(100);
        }
    }
}

#define INIT(initCall)                                                         \
    performInitializationWithFeedback([&]() { return (initCall); })

/**
 * @brief Setup function for initializing micro-ROS, pin modes, etc.
 *
 */
void setup()
{
    // Configure serial transport
    Serial.begin(115200); // disable in production

    // IPAddress agent_ip(AGENT_IP);
    // uint16_t agent_port = AGENT_PORT;

    // ! uncomment this for serial and remove the wifi transport below and in
    // ! the platformio.ini
    set_microros_serial_transports(Serial);

    // set_microros_wifi_transports((char*)SSID, (char*)SSID_PW, agent_ip,
    //                              agent_port);
    delay(2000);

    allocator = rcl_get_default_allocator();

    // create init_options
    // clang-format off
    INIT(rclc_support_init(&support, 0, NULL, &allocator));
    INIT(rclc_node_init_default(&node, "roboost_pmc_node", "", &support));
    INIT(rclc_publisher_init_default(&odom_publisher, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(nav_msgs, msg, Odometry), "odom"));
    INIT(rclc_publisher_init_default(&joint_state_publisher, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, JointState), "joint_states"));
    INIT(rclc_publisher_init_default(&wanted_joint_state_publisher, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, JointState), "wanted_joint_states"));
#ifdef DEBUG
    INIT(rclc_publisher_init_default(&diagnostic_publisher, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(diagnostic_msgs, msg, DiagnosticStatus), "diagnostics"));
#endif
    INIT(rclc_subscription_init_default(&cmd_vel_subscriber, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Twist), "cmd_vel"));
    INIT(rclc_executor_init(&executor, &support.context, 1, &allocator));
    INIT(rclc_executor_add_subscription(&executor, &cmd_vel_subscriber, &twist_msg, &cmd_vel_subscription_callback, ON_NEW_DATA));
    // clang-format on

    delay(500);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    odom_msg.header.frame_id.data = "odom";
    odom_msg.header.frame_id.size = strlen(odom_msg.header.frame_id.data);
    odom_msg.header.frame_id.capacity = odom_msg.header.frame_id.size + 1;
    odom_msg.child_frame_id.data = "base_link";
    odom_msg.child_frame_id.size = strlen(odom_msg.child_frame_id.data);
    odom_msg.child_frame_id.capacity = odom_msg.child_frame_id.size + 1;

    double* temp_covariance = (double*)malloc(36 * sizeof(double));

    // TODO: determine correct covariance values
    for (int i = 0; i < 36; i++)
    {
        if (i % 7 != 0)
        {
            temp_covariance[i] = 0.0;
        }
    }
    temp_covariance[0 + 0] = 0.8;  // x
    temp_covariance[6 + 1] = 0.8;  // y
    temp_covariance[12 + 2] = 0.8; // z
    temp_covariance[18 + 3] = 0;   // rotation about X axis
    temp_covariance[24 + 4] = 0;   // rotation about Y axis
    temp_covariance[30 + 5] = 0.8; // rotation about Z axis

    memcpy(odom_msg.pose.covariance, temp_covariance, 36 * sizeof(double));
    memcpy(odom_msg.twist.covariance, temp_covariance, 36 * sizeof(double));
    free(temp_covariance);

    // Initialize the joint state message
    joint_state_msg.header.frame_id.data = "base_link";
    rosidl_runtime_c__String__Sequence__init(&joint_state_msg.name, 4);
    rosidl_runtime_c__String__assign(&joint_state_msg.name.data[0],
                                     "wheel_front_left_joint");
    rosidl_runtime_c__String__assign(&joint_state_msg.name.data[1],
                                     "wheel_front_right_joint");
    rosidl_runtime_c__String__assign(&joint_state_msg.name.data[2],
                                     "wheel_back_left_joint");
    rosidl_runtime_c__String__assign(&joint_state_msg.name.data[3],
                                     "wheel_back_right_joint");
    joint_state_msg.name.size = 4;
    joint_state_msg.name.capacity = 4;

    joint_state_msg.position.data = (double*)malloc(4 * sizeof(double));
    joint_state_msg.position.size = 4;
    joint_state_msg.position.capacity = 4;

    joint_state_msg.velocity.data = (double*)malloc(4 * sizeof(double));
    joint_state_msg.velocity.size = 4;
    joint_state_msg.velocity.capacity = 4;
}

/**
 * @brief Main loop for continuously updating and publishing the robot's
 * odometry.
 *
 */
void loop()
{
#ifdef DEBUG
#ifdef DEBUG_TIME
    char debug_str[200];
    strcpy(debug_str, "");

    // Publish debug time information for diagnostics 0
    unsigned long now_debug = millis();
    double dt_debug = (now_debug - last_time_debug) / 1000.0;
    char dt_part[50];
    sprintf(dt_part, "[0]: %f; ", dt_debug);
    strcat(debug_str, dt_part);
    last_time_debug = now_debug;
#endif
#endif

    // Time synchronization
    if (millis() - last_time_sync_ms > time_sync_interval)
    {
        // Synchronize time with the agent
        rmw_uros_sync_session(timeout_ms);

        if (rmw_uros_epoch_synchronized())
        {
            // Get time in milliseconds or nanoseconds
            synced_time_ms = rmw_uros_epoch_millis();
            synced_time_ns = rmw_uros_epoch_nanos();
            last_time_sync_ms = millis();
            last_time_sync_ns = micros() * 1000;
        }
    }

#ifdef DEBUG
#ifdef DEBUG_TIME
    // Publish debug time information to diagnostics 1
    now_debug = millis();
    dt_debug = (now_debug - last_time_debug) / 1000.0;
    sprintf(dt_part, "[1]: %f; ", dt_debug);
    strcat(debug_str, dt_part);
    last_time_debug = now_debug;
#endif
#endif

    RCSOFTCHECK(rclc_executor_spin_some(&executor, RCL_MS_TO_NS(10)));

#ifdef DEBUG
#ifdef DEBUG_TIME
    // Publish debug time information to diagnostics 2
    now_debug = millis();
    dt_debug = (now_debug - last_time_debug) / 1000.0;
    sprintf(dt_part, "[2]: %f; ", dt_debug);
    strcat(debug_str, dt_part);
    last_time_debug = now_debug;
#endif
#endif

    robot_controller.update();

#ifdef DEBUG
#ifdef DEBUG_TIME
    // Publish debug time information to diagnostics 3
    now_debug = millis();
    dt_debug = (now_debug - last_time_debug) / 1000.0;
    sprintf(dt_part, "[3]: %f; ", dt_debug);
    strcat(debug_str, dt_part);
    last_time_debug = now_debug;
#endif
#endif

    Eigen::Vector3d robot_velocity = robot_controller.get_robot_velocity();

    // Calculate the delta time for odometry calculation
    unsigned long now = millis();
    double dt = (now - last_time) / 1000.0;
    last_time = now;

    pose(0) += robot_velocity(0) * cos(pose(2)) * dt -
               robot_velocity(1) * sin(pose(2)) * dt;
    pose(1) += robot_velocity(0) * sin(pose(2)) * dt +
               robot_velocity(1) * cos(pose(2)) * dt;
    pose(2) += robot_velocity(2) * dt;
    pose(2) = atan2(sin(pose(2)), cos(pose(2)));

    odom_msg.pose.pose.position.x = pose(0);
    odom_msg.pose.pose.position.y = pose(1);
    // Orientation in quaternion notation
    odom_msg.pose.pose.orientation.w = cos(pose(2) / 2.0);
    odom_msg.pose.pose.orientation.z = sin(pose(2) / 2.0);

    odom_msg.twist.twist.linear.x = robot_velocity(0);
    odom_msg.twist.twist.linear.y = robot_velocity(1);
    odom_msg.twist.twist.angular.z = robot_velocity(2);

    odom_msg.header.stamp.sec =
        (synced_time_ms + millis() - last_time_sync_ms) / 1000;
    odom_msg.header.stamp.nanosec =
        synced_time_ns + (micros() * 1000 - last_time_sync_ns);
    odom_msg.header.stamp.nanosec %= 1000000000; // nanoseconds are in [0, 1e9)

    RCSOFTCHECK(rcl_publish(&odom_publisher, &odom_msg, NULL));

#ifdef DEBUG
#ifdef DEBUG_TIME
    // Publish debug time information to diagnostics 4
    now_debug = millis();
    dt_debug = (now_debug - last_time_debug) / 1000.0;
    sprintf(dt_part, "[4]: %f; [dt]: %f s", dt_debug, dt);
    strcat(debug_str, dt_part);
    last_time_debug = now_debug;

    publishDiagnosticMessage(debug_str);
#endif
#endif

    // Update the joint state message

    Eigen::Vector4d wheel_velocities =
        kinematics.calculate_wheel_velocity(robot_velocity);

    joint_state_msg.position.data[0] += wheel_velocities(0) * dt;
    joint_state_msg.position.data[1] += wheel_velocities(1) * dt;
    joint_state_msg.position.data[2] += wheel_velocities(2) * dt;
    joint_state_msg.position.data[3] += wheel_velocities(3) * dt;

    joint_state_msg.velocity.data[0] = wheel_velocities(0);
    joint_state_msg.velocity.data[1] = wheel_velocities(1);
    joint_state_msg.velocity.data[2] = wheel_velocities(2);
    joint_state_msg.velocity.data[3] = wheel_velocities(3);

    joint_state_msg.header.stamp.sec = synced_time_ms / 1000;
    joint_state_msg.header.stamp.nanosec = synced_time_ns;

    RCSOFTCHECK(rcl_publish(&joint_state_publisher, &joint_state_msg, NULL));

    // Update the wanted joint state message

    Eigen::Vector4d wanted_wheel_velocities =
        robot_controller.get_set_wheel_velocities();

    wanted_joint_state_msg.velocity.data[0] = wanted_wheel_velocities(0);
    wanted_joint_state_msg.velocity.data[1] = wanted_wheel_velocities(1);
    wanted_joint_state_msg.velocity.data[2] = wanted_wheel_velocities(2);
    wanted_joint_state_msg.velocity.data[3] = wanted_wheel_velocities(3);

    wanted_joint_state_msg.header.stamp.sec = synced_time_ms / 1000;
    wanted_joint_state_msg.header.stamp.nanosec = synced_time_ns;

    RCSOFTCHECK(rcl_publish(&wanted_joint_state_publisher,
                            &wanted_joint_state_msg, NULL));

    delay(10);
}
