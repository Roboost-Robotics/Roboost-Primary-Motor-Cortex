#ifndef VELOCITYCONTROLLER_H
#define VELOCITYCONTROLLER_H

#ifdef JOINTSTATECONTROLLER_H
#error                                                                         \
    "joint_state_controller.hpp and velocity_controller.hpp cannot be used at the same time!"
#endif

#include <ArduinoEigen.h>
#include <nav_msgs/msg/odometry.h>

#include "kinematics/kinematics.hpp"
#include "motor-control/motor_control_manager.hpp"

/**
 * @brief The VelocityController class manages the control of a robot's motors
 * and implements odometry calculations based on its kinematics model.
 */
class VelocityController
{
public:
    /**
     * @brief Construct a new Robot Controller object.
     *
     * @param motor_manager The motor control manager responsible for motor
     *                      control.
     * @param kinematics_model The kinematics model used for odometry
     * calculations.
     */
    VelocityController(MotorControllerManager& motor_manager,
                       Kinematics* kinematics_model);

    /**
     * @brief Update the robot's control loop. This method should be called
     *        periodically to control the robot's motors and update odometry.
     */
    void update();

    /**
     * @brief Get the current velocity estimation estimation.
     *
     * @return Eigen::Vector3d The current robot velocity estimation.
     */
    Eigen::Vector3d get_robot_velocity();

    /**
     * @brief Get the current set wheel velocities.
     *
     * @return Eigen::VectorXd The current set wheel velocities based on latest
     * command.
     */
    Eigen::VectorXd get_set_wheel_velocities();

    /**
     * @brief Set the latest command for the robot's motion control.
     *
     * @param latest_command A vector containing the latest command for the
     * robot's motion control, currently only representing linear velocities
     * (vx, vy, vz).
     */
    void set_latest_command(const Eigen::Vector3d& latest_command);

private:
    MotorControllerManager& motor_manager_;
    Kinematics* kinematics_model_;

    Eigen::Vector3d latest_command_;
    Eigen::Vector3d robot_velocity_;
};

#endif // VELOCITYCONTROLLER_H
