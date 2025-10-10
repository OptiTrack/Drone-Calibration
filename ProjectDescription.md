<!-- TABLE OF CONTENTS -->
<details>
  <summary>Table of Contents</summary>
  <ol>
    <li><a href="#project-statement">Project Statement</a></li>
    <li>
      <a href="#project-scope">Getting Started</a>
      <ul>
        <li><a href="#scope">Scope</a></li>
        <li><a href="#stretch-goals">Stretch Goals</a></li>
      </ul>
    </li>
    <li><a href="#success-criteria">Success Criteria</a></li>
    <li><a href="#learning-goals">Learning Goals</a></li>
  </ol>
</details>


<!-- PROJECT STATEMENT -->
# Project Statement

Some of our customers have camera volumes that can be as large as a sports field or as tall as a multi-story building. Having a human calibrate this volume is complicated and difficult. 

The primary goal of this project is to have a drone calibrate the volume in order to automate this calibration process for large customers. Not all spaces are reachable by humans, it can be done by flying drones recoding the trajectory and repeating the process whenever calibration is needed.

The program made here would allow a users to roughly define the trajectory of a drone being used to calibrate a volume for the users. 

<img width="700" height="486" alt="Untitled" src="https://github.com/user-attachments/assets/b24bca2b-953a-434b-84da-572385f3b002" />

<!-- PROJECT SCOPE -->
# Project Scope

## Scope and Deliverables

1. The goal of the Project is to use a Open-Source Drone hardware/software to calibrate the 3D volume
2. Qt based user interface for controlling the Drone Flight Path
3. Settings can be changed to Select Different Drones, we will have only one Drone to start with
4. Record and Playback the Drone flight path to pre-determined and recorded paths
5. Ability to Carry different Calibration Rods
6. Ideally be able to interface with Motive to start and stop the Calibrating Drone
7. Spin the Calibration rod for full coverage of the 3D Volume, The Drone should be able to spin the Calibration rod by flying or have a robotic mechanism to spin in place
8. Drone should Land Properly while carry the Calibration Rod

## Stretch Goals

9. Edit the Flight path, for all recorded flight paths
10. Display all the Flight Trajectories in the QT GUI
11. Obstacle Avoidance and Restart Calibration without human intervention, if possible, change the screens in Motive using Command Protocol (example NatNet commands)

Out of Scope (Don’t do)
* Get stuck down a rabbit hole. There's probably a long list of other features that you could have here. Keeping focus is key.

# Success Criteria

At minimum, a successful project would have a program where you can define the trajectory of a drone in a space to automatically calibrate the volmue. The calibrtaion given from this flight path should be at least on par with a manual Calibration.


# Learning Goals 

Since this is aimed at being a student project it’s important to have some learning goals associated with the project. Those are roughly summarized below. 
* Learn how to interface with a third party API
* Learn how to establish requirements and stakeholders for a project to validate progress.
* Learn basic 3D viewport manipulation skills.
* Learn how to write standardized file formats.
* Learn how to work with physical hardware.
* Learn the basics of motion capture techniques.
