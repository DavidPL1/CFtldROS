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
 * MainX.cpp
 *
 *  Created on: Nov 17, 2011
 *      Author: Georg Nebehay
 */

#include <chrono>
#include <thread>
#include <signal.h>

#include "Main.h"
#include "Config.h"
#include "ImAcq.h"
#include "TLDUtil.h"
#include "Trajectory.h"
#include "opencv2/imgproc/imgproc.hpp"

// ROS
#include <ros/ros.h>
#include <cv_bridge/cv_bridge.h>
#include <image_transport/image_transport.h>
#include <geometry_msgs/PoseStamped.h>
#include <people_msgs/People.h>
#include <people_msgs/Person.h>

using namespace tld;
using namespace cv;

bool stop = false;

bool Main::toggleCB(clf_perception_vision_msgs::ToggleCFtldTrackingWithBB::Request& request, clf_perception_vision_msgs::ToggleCFtldTrackingWithBB::Response& response) {
    ROS_INFO("Received toggle service call");
    toggleMutex.lock();
    if (request.roi.width != 0 && request.roi.height != 0) {
        ROS_INFO("Tracking is now active");
        initialBB = new int[4];
        initialBB[0] = request.roi.x_offset;
        initialBB[1] = request.roi.y_offset;
        initialBB[2] = request.roi.width;
        initialBB[3] = request.roi.height;
        newBB = true;
        isToggeled = true;
        ROS_DEBUG("Bounding Box x: %d, y: %d, w: %d, h: %d received", initialBB[0], initialBB[1], initialBB[2], initialBB[3]);
    } else {
        ROS_WARN("Deactivated tracking (invalid BB)!");
        isToggeled = false;
    }
    toggleMutex.unlock();
    return true;
}

void inthand(int signum) {
    printf(">> CTRL+C...\n");
    stop = true;
}

void Main::doWork() {

    signal(SIGINT, inthand);

    Trajectory trajectory;

    Mat colorImage, depthImage;

    ROS_INFO(">>> Setting up ros subcribers");
    image_transport::ImageTransport it(ros_grabber->node_handle_);
    image_transport::Publisher pub = it.advertise("cftld/detection", 1);
    ros::Publisher pub_detection = ros_grabber_depth->node_handle_.advertise<people_msgs::People>("/cftld/sensor/person_to_follow", 1);
    ros::Publisher pub_marker_array = ros_grabber_depth->node_handle_.advertise<visualization_msgs::MarkerArray>("/cftld/marker_array", 1);
    ROS_INFO(">>> Subscribers initialized");

    std::string toggleServiceTopic = "/cftld/toggle";
    ros::ServiceServer serviceCrowd = ros_grabber_depth->node_handle_.advertiseService(toggleServiceTopic, &Main::toggleCB, this);

    if (!isRosUsed) {
        printf(">> ROS IS OFF\n");
        colorImage = imAcqGetImg(imAcq);
    } else {
        ROS_DEBUG(">>> ROS IS ON");
        // first spin, to get callback in the queue
        ros::spinOnce();
        ros_grabber->getImage(&colorImage);
        ros_grabber_depth->getImage(&depthImage);
        while (colorImage.rows*colorImage.cols < 1 || depthImage.rows * depthImage.cols < 1) {
            ros::spinOnce();
            ros_grabber->getImage(&colorImage);
            ros_grabber_depth->getImage(&depthImage);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            last_frame_nr = ros_grabber->getLastFrameNr();
            if (stop) {
                break;
            }
        }
        // Don't resize depth image, it is already 320x240 on Pepper!
        // cv::resize(colorImage, colorImage, cv::Size(), 0.375, 0.375);
    }

    if (colorImage.channels() == 1)
        cv::cvtColor(colorImage, colorImage, cv::COLOR_GRAY2BGR);

    if (showTrajectory)     {
        trajectory.init(trajectoryLength);
    }

    FILE *resultsFile = NULL;

    if (printResults != NULL) {
        resultsFile = fopen(printResults, "w");
        if (!resultsFile) {
            fprintf(stderr, "Error: Unable to create results-file \"%s\"\n", printResults);
            exit(-1);
        }
    }

    bool reuseFrameOnce = false;
    bool skipProcessingOnce = false;
    bool paused = false;
    bool step = false;
    double tic = 0;
    double toc = 0;
    double tic_global = 0;
    double toc_global = 0;

    if (initialBB != NULL) {
        Rect bb = tldArrayToRect(initialBB);
        printf("Starting at %d %d %d %d\n", bb.x, bb.y, bb.width, bb.height);
        tld->selectObject(colorImage, &bb);
        skipProcessingOnce = true;
        reuseFrameOnce = true;
    }

    // imAcqHasMoreFrames(imAcq)
    tic_global = static_cast<double>(getTickCount());
    unsigned int pubFrameCount = 0;
    while (stop == false) {
        // Loop spinner
        ros::spinOnce();
        // Make sure we only run with image framerate to save CPU cycles
        if(isRosUsed) {
            if(!(ros_grabber->getLastFrameNr() != last_frame_nr)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                last_frame_nr = ros_grabber->getLastFrameNr();
                // ROS_DEBUG(">>> Skipping computation step ...");
                continue;
            }
        }
        if(isToggeled) {

            toc_global = static_cast<double>(getTickCount()) - tic_global;
            
            if (static_cast<float>(toc_global)/getTickFrequency() >= 1) {
                ROS_DEBUG("Passed time: %f", static_cast<float>(toc_global)/getTickFrequency());
                ROS_DEBUG("FPS: %d", pubFrameCount);
                pubFrameCount = 0;
                tic_global = static_cast<double>(getTickCount());
            }

            if (newBB) {
                if (!isRosUsed) {
                    colorImage.release();
                    colorImage = imAcqGetImg(imAcq);
                } else {
                    ros_grabber->getImage(&colorImage);
                    ros_grabber_depth->getImage(&depthImage);
                    //cv::resize(colorImage, colorImage, cv::Size(), 0.375, 0.375);
                    last_frame_nr = ros_grabber->getLastFrameNr();
                }

                ROS_INFO("---> Passing new bounding box to tld");
                Rect bb = tldArrayToRect(initialBB);
                tic = static_cast<double>(getTickCount());
                tld->selectObject(colorImage, &bb);
                toc = (static_cast<double>(getTickCount()) - tic)/static_cast<float>(getTickFrequency());
                skipProcessingOnce = true;
                reuseFrameOnce = true;
                ROS_DEBUG("---> Re-init of bounding box took %f seconds", toc);
                
                newBB = false;
            }

	        if (ros_grabber->getLastFrameNr() % frame_modulo == 0) {

                ROS_DEBUG("\tProcessing image with frame nr: %d", ros_grabber->getLastFrameNr());

                if (!reuseFrameOnce && (!paused || step)) {

                    if (!isRosUsed) {
                        colorImage.release();
                        colorImage = imAcqGetImg(imAcq);
                    } else {
                        ros_grabber->getImage(&colorImage);
                        ros_grabber_depth->getImage(&depthImage);
                        //cv::resize(colorImage, colorImage, cv::Size(), 0.375, 0.375);
                        last_frame_nr = ros_grabber->getLastFrameNr();
                    }

                    if (colorImage.channels() == 1)
                        cv::cvtColor(colorImage, colorImage, cv::COLOR_GRAY2BGR);

                    if (cv::countNonZero(colorImage) < 1) {
                        printf("current image is NULL, assuming end of input.\n");
                        break;
                    }
		        }

                if (!skipProcessingOnce && (!paused || step)) {
                    tic = static_cast<double>(getTickCount());
                    tld->processImage(colorImage);
                    toc = static_cast<double>(getTickCount()) - tic;
                }
                else {
                    skipProcessingOnce = false;
                }

                float fps = static_cast<float>(getTickFrequency()) / toc;

                if (printResults != NULL) {
                    if (tld->currBB != NULL) {
                        fprintf(resultsFile, "%d, %.2d, %.2d, %.2d, %.2d, %f, %f\n", imAcq->currentFrame - 1,
                            tld->currBB->x, tld->currBB->y, tld->currBB->width, tld->currBB->height, tld->currConf,
                            fps);
                    }
                    else {
                        fprintf(resultsFile, "%d, NaN, NaN, NaN, NaN, NaN, %f\n", imAcq->currentFrame - 1, fps);
                    }
                }

                if (saveDir != NULL || isRosUsed) {

                    char string[128];
                    char learningString[10] = "";

                    if (paused && step)
                        step = false;

                    if (tld->learning) {
                        strcpy(learningString, "Learning");
                    }

                    sprintf(string, "#%d, fps: %.2f, #numwin:%d, %s", imAcq->currentFrame - 1,
                            fps, tld->detectorCascade->numWindows, learningString);
                    cv::Scalar yellow = cv::Scalar(0, 255, 255, 0);
                    cv::Scalar black = cv::Scalar(0, 0, 0, 0);
                    cv::Scalar white = cv::Scalar(255, 255, 255, 0);
                    cv::Scalar blue = cv::Scalar(255, 144, 30, 0);

                    people_msgs::People detections;

                    if (tld->currBB != NULL) {
                        cv::Scalar rectangleColor = blue;
                        cv::rectangle(colorImage, tld->currBB->tl(), tld->currBB->br(), rectangleColor, 2, 8, 0);
                        geometry_msgs::PoseStamped pose = ros_grabber_depth->getDetectionPose(depthImage, tld->currBB);
                        if (pose.header.frame_id != "invalid") {
                            detections.header = pose.header;
                            people_msgs::Person person;
                            person.name = "tracked_person";
                            person.position = pose.pose.position;
                            person.reliability = 1.0;
                            detections.people.push_back(person);
                            ros_grabber_depth->createVisualisation(pose.pose, pub_marker_array);
                        }
                    }

                    if (pub_detection.getNumSubscribers() > 0) {
                        pub_detection.publish(detections);
                    }
                    cv::putText(colorImage, string, cv::Point(15, 15), cv::FONT_HERSHEY_SIMPLEX, 0.4, blue, 1, 8);
                    pubFrameCount++;;

                    // Publish every 6th cycle
                    if (pub.getNumSubscribers() > 0 and last_frame_nr % 6 == 0) {
                        sensor_msgs::ImagePtr msg = cv_bridge::CvImage(std_msgs::Header(), "bgr8", colorImage).toImageMsg();
                        pub.publish(msg);
                    }

                    if (saveDir != NULL) {
                        char fileName[256];
                        sprintf(fileName, "%s/%.5d.png", saveDir, imAcq->currentFrame - 1);
                        cv::imwrite(fileName, colorImage);
                    }
		        }
            // frame % 2 == 0
            } else {
                ROS_DEBUG("\tOmitting frame with id: %d", ros_grabber->getLastFrameNr());
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }

	    } // is toggle

	    std::this_thread::sleep_for(std::chrono::milliseconds(20));

        if (reuseFrameOnce) {
            reuseFrameOnce = false;
        }
    
        if(stop) { break; }
    }

    ROS_INFO(">>> Bye Bye!");

    colorImage.release();
    
    delete ros_grabber;
    delete ros_grabber_depth;

    if (resultsFile) {
        fclose(resultsFile);
    }
}
