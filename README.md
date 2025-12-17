# Smart Solar Load Management System

## Overview
A distributed IoT system for intelligent management of solar power, water resources, and electrical loads in off-grid or hybrid solar installations.

## System Architecture
The system consists of multiple ESP32-based nodes communicating via wireless protocols:

- **Sensor Nodes**: Monitor water tank levels using ultrasonic sensors and mechanical float switches
- **Solar Monitor Nodes**: Measure real-time solar power availability
- **Load Control Nodes**: Switch electrical loads on/off based on power availability
- **Source Switch Nodes**: Select between power sources (solar inverter, generator, grid)
- **Central Hub**: Coordinates all nodes and makes system-level decisions

## Key Features
- Real-time water level monitoring with dual-sensor redundancy
- Solar-aware load scheduling based on available power
- Automatic power source selection
- Deep sleep operation for sensor nodes to conserve energy
- Modular design allowing easy expansion

## Hardware Requirements
- ESP32 development boards
- HC-SR04 ultrasonic sensors
- Mechanical float switches
- Relay modules for load control
- Contactor relays for power source switching
- Current sensors (INA219/ACS712) for power measurement

## Communication
- MQTT-based communication between nodes
- WiFi/Bluetooth connectivity
- Configurable topics for sensor data and control commands

## Applications
- Off-grid solar installations
- Agricultural water management
- Remote monitoring systems
- Energy optimization in hybrid power systems

## Project Structure