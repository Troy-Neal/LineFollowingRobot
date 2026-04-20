# Line Following Robot

## What Is It?

This project is an ESP32-based mobile robot that can be driven from a website over Wi-Fi and can also run a simple autonomous line-follow mode. It was originally intended to be a factory style component delivery bot that could follow lines on the ground to navigate between points of interest. However the IR sensors that I got arrived malfunctioning, and did not correctly identify white vs black despite the manufacturers description. Due to this, the project had to be simplified last minute resulting in the drivable car, with very very basic line following abilities.

The robot uses:

- DC drive motors through an L298N motor driver
- a TCS34725 colour sensor
- an ultrasonic distance sensor
- several digital line sensors
- a WebSocket-connected frontend for control and telemetry

At a high level, the robot can:

1. Be manually driven from a browser
2. Enter a line-follow mode based on detected surface colour
3. Report telemetry and logs back to the frontend

## What Does It Do?

The robot supports two main operating modes.

### Manual Drive

The browser sends left/right motor commands over WebSocket, and the ESP32 applies them to the motors while still enforcing front obstacle safety rules.

### Line Follow

The line-follow mode uses the colour sensor rather than depending entirely on the original HW-006 tracker sensors.

The process is:

- the user presses `Follow Line`
- the robot waits until it sees a valid starting line colour
- that colour becomes the locked target colour
- while the robot continues seeing that same colour, it drives forward
- if it loses that colour, it performs a reacquire search:
  - sweep left
  - sweep right farther
  - creep forward slightly
  - repeat

## What Would Someone Need To Reproduce This?

### Hardware

- ESP32 development board
- L298N motor driver
- 4 DC motors with wheels
- robot chassis
- TCS34725 colour sensor
- ultrasonic distance sensor
- HW-006 line sensors or equivalent
- suitable power source
- jumper wires

### Software / Tools

- PlatformIO or another ESP32 Arduino-compatible environment
- this firmware project
- the frontend project in `../FrontEnd/Robot-FrontEnd`
- Node.js for the frontend server
- access to a Wi-Fi network

## Pin Mapping

### Motors

- `ENA`: GPIO 25
- `IN1`: GPIO 26
- `IN2`: GPIO 27
- `IN3`: GPIO 14
- `IN4`: GPIO 23
- `ENB`: GPIO 17

### Colour Sensor

- `SDA`: GPIO 21
- `SCL`: GPIO 22

### Ultrasonic Sensor

Only one ultrasonic sensor is currently active in the final code.

- front `TRIG`: GPIO 13
- front `ECHO`: GPIO 15

### Line Sensors in `main.cpp`

Front trio:

- `FF`: GPIO 33
- `FS`: GPIO 19
- `FT`: GPIO 18

Back trio:

- `BF`: GPIO 35
- `BS`: GPIO 34
- `BT`: GPIO 32

## Software Process Overview

### 1. Boot and Initialization

On startup, the ESP32:

- initializes motor pins and PWM
- initializes line sensors
- initializes the ultrasonic sensor
- initializes the TCS34725 over I2C
- starts serial logging
- connects to Wi-Fi
- connects to the backend WebSocket server

### 2. WebSocket Architecture

The system uses a UI/device model:

- the browser connects as a UI client
- the ESP32 connects as a device client
- the backend relays commands from the UI to the robot
- the robot sends telemetry and logs back to the backend
- the backend forwards telemetry to the UI

Important message types include:

- `hello`
- `control`
- `drive`
- `line-follow`
- `device-state`
- `robot-log`

### 3. Manual Drive Flow

The frontend converts pointer input into left/right motor values and sends them over WebSocket. Those updates are throttled slightly in the browser to reduce flooding.

The ESP32 receives them and applies motor commands while preserving safety checks.

### 4. Motor Control and Safety

The firmware separates:

- requested motor commands
- actual applied motor commands

This allows safety logic to modify output after a command is received.

Current safety behavior includes:

- blocking forward motion if an obstacle is too close in front
- reducing forward speed in a caution zone before the hard block distance

The safety logic is reapplied continuously in the main loop, not just when a new command arrives.

### 5. Colour Sensing

The TCS34725 is used to classify:

- black
- white
- blue
- red
- green
- yellow

Classification is based on raw red/green/blue/clear readings and ratio-based thresholds.

### 6. Line Follow Logic

The final simplified line-follow mode uses the colour sensor:

- it waits for a valid starting line colour
- it locks onto that colour
- it drives forward only while the current colour matches the locked starting colour
- if the colour is lost, it enters the reacquire pattern

This is intentionally simple. A single centred colour sensor can only tell whether it is currently on the target colour, not whether the line is drifting left or right under the robot.

### 7. Telemetry and Logging

The robot reports:

- current motor outputs
- requested motor outputs
- line sensor readings
- distance readings
- detected colour
- line-follow debug state
- connection and event logs

This telemetry was important for debugging interactions between the frontend, server, and robot firmware.

## Obstacles Encountered

### 1. The HW-006 Sensors Were Unreliable

The HW-006 V1.3 sensors used in the build are fixed-threshold digital sensors with no adjustment trimmer.

Problems caused by this:

- they could misidentify white as line
- their behavior changed with height and surface reflectivity
- they were hard to calibrate for the actual floor and line

How this was handled:

- dedicated sensor test sketches were used
- polarity was changed to match observed readings
- line-following was simplified to rely mainly on the colour sensor instead

### 2. WebSocket Logging and State Feedback Problems

At one point the robot was receiving UI snapshots and logging them back, which polluted the log stream and made debugging confusing.

How this was handled:

- the server was changed so snapshots go only to the UI
- robot-side logging was limited to useful messages
- the frontend was updated to show live robot logs more clearly

### 3. Manual Commands Interfered With Follow Mode

Queued drive commands from the browser could interfere with line-follow mode and cancel it unexpectedly.

How this was handled:

- drive updates were throttled in the frontend
- queued drive commands were cleared before entering follow mode
- command flow between manual and follow modes was cleaned up

### 4. Obstacle Safety Was Initially Too Weak

The original obstacle stop logic only checked distance when a new command arrived.

How this was handled:

- requested and actual motor commands were separated
- distance readings were cached
- safety enforcement was moved into the main loop



## Final Notes

This project became as much a debugging and integration exercise as a robot build. A large part of the work involved figuring out whether problems came from:

- hardware limitations
- incorrect sensor interpretation
- wiring and mapping mismatches
- frontend/server command flow
- safety logic
- or motor drive behavior

If I wasn't so busy with everything else this would have gone a lot better.
