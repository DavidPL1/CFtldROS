/*  Copyright 2011 AIT Austrian Institute of Technology
*
*   This file is part of OpenTLD.
*
*   OpenTLD is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   OpenTLD is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with OpenTLD.  If not, see <http://www.gnu.org/licenses/>.
*
*/

/*
 * main.h
 *
 *  Created on: Nov 18, 2011
 *      Author: Georg Nebehay
 */

#ifndef MAIN_H_
#define MAIN_H_

#include "TLD.h"
#include "ImAcq.h"
#include "cf_tracker.hpp"

// ROS
#include "ros/ros_grabber.hpp"
#include "ros/ros_grabber_depth.hpp"
#include <ros/service_server.h>
#include <std_srvs/Empty.h>
#include <clf_perception_vision_msgs/ToggleCFtldTrackingWithBB.h>

enum Retval
{
    PROGRAM_EXIT = 0,
    SUCCESS = 1
};

class Main
{
public:
    tld::TLD *tld;
    ImAcq *imAcq;
    ROSGrabber *ros_grabber;
    ROSGrabberDepth *ros_grabber_depth;
    std::mutex  toggleMutex;
    bool isToggeled;
    bool newBB;
    bool showOutput;
    bool showTrajectory;
    int trajectoryLength;
    const char *printResults;
    const char *saveDir;
    double threshold;
    bool showForeground;
    bool showNotConfident;
    int *initialBB;
    bool reinit;
    bool exportModelAfterRun;
    bool loadModel;
    bool isRosUsed;
    const char *modelPath;
    const char *modelExportFile;
    int seed;
    int last_frame_nr;
    unsigned int frame_modulo;

    bool toggleCB(clf_perception_vision_msgs::ToggleCFtldTrackingWithBB::Request& request, clf_perception_vision_msgs::ToggleCFtldTrackingWithBB::Response& response);

    Main()
    {
        tld = new tld::TLD();
        showOutput = 1;
        printResults = NULL;
        saveDir = ".";
        threshold = 0.5;
        showForeground = 0;

        showTrajectory = false;
        trajectoryLength = 0;

        isRosUsed = false;
        isToggeled = false;
        newBB = false;

        initialBB = NULL;
        showNotConfident = true;

        reinit = 0;

        loadModel = false;

        exportModelAfterRun = false;
        modelExportFile = "model";
        seed = 0;

        modelPath = NULL;
        imAcq = NULL;

        last_frame_nr = -1;
    }

    ~Main()
    {
        delete tld;
        imAcqFree(imAcq);
    }

    void doWork();
};

#endif /* MAIN_H_ */
