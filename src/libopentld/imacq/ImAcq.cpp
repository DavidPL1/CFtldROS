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

#include "ImAcq.h"

#include <cstdio>

#include<opencv2/core/core.hpp>
#include<opencv2/highgui/highgui.hpp>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

static void msleep(int milliseconds)
{
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    Sleep(milliseconds);
#else
    usleep(static_cast<useconds_t>(milliseconds)*1000);
#endif
}

ImAcq *imAcqAlloc()
{
    ImAcq *imAcq = (ImAcq *)malloc(sizeof(ImAcq));
    imAcq->method = IMACQ_CAM;
    imAcq->currentFrame = 1;
    imAcq->lastFrame = 0;
    imAcq->camNo = 0;
    imAcq->fps = 24;
    return imAcq;
}

void imAcqInit(ImAcq *imAcq)
{
    if (imAcq->method == IMACQ_CAM)
    {
        imAcq->capture->open(imAcq->camNo);

        if (! imAcq->capture->isOpened())
        {
            printf("Error: Unable to initialize camera\n");
            exit(0);
        }
    }
    else if (imAcq->method == IMACQ_VID)
    {
        imAcq->capture->open(imAcq->imgPath);

        if (! imAcq->capture->isOpened())
        {
            printf("Error: Unable to open video\n");
            exit(0);
        }

        // take all frames
        if (imAcq->lastFrame == 0)
            imAcq->lastFrame = imAcqVidGetNumberOfFrames(imAcq); //This sometimes returns garbage

        // lastFrame out of bounds
        if (imAcq->lastFrame > imAcqVidGetNumberOfFrames(imAcq))
        {
            printf("Error: video has only %d frames you selected %d as last frame.\n",
                imAcqVidGetNumberOfFrames(imAcq), imAcq->lastFrame);
            exit(1);
        }

        // something is wrong with startFrame and/or lastFrame
        if ((imAcq->lastFrame < 1) || (imAcq->currentFrame < 1) || ((imAcq->currentFrame > imAcq->lastFrame)))
        {
            printf("Error: something is wrong with the start and last frame number. startFrame: %d lastFrame: %d\n",
                imAcq->currentFrame, imAcq->lastFrame);
            exit(1);
        }

        // set the video position to the correct frame
        //This produces strange results on some videos and is deactivated for now.
        //imAcqVidSetNextFrameNumber(imAcq, imAcq->currentFrame);
    }
    else if (imAcq->method == IMACQ_STREAM)
    {
        imAcq->capture->open(imAcq->imgPath);

        if (! imAcq->capture->isOpened())
        {
            printf("Error: Unable to open video\n");
            exit(0);
        }
    }
    else if (imAcq->method == IMACQ_ROS) { }

    imAcq->startFrame = imAcq->currentFrame;
    imAcq->startTime = cv::getTickCount();
}

void imAcqFree(ImAcq *imAcq)
{
    if ((imAcq->method == IMACQ_CAM) || (imAcq->method == IMACQ_VID) || (imAcq->method == IMACQ_STREAM))
    {
        imAcq->capture->release();
    }

    free(imAcq);
}

cv::Mat imAcqLoadImg(ImAcq *imAcq, char *path)
{
    cv::Mat image = cv::imread(path);

    if (cv::countNonZero(image) < 1)
    {
        printf("Error: %s does not exist or is not an image.\n", path);
    }

    return image;
}

cv::Mat imAcqLoadFrame(ImAcq *imAcq, int fNo)
{
    char path[255];
    sprintf(path, imAcq->imgPath, fNo);

    return cv::imread(path);
}

cv::Mat imAcqLoadCurrentFrame(ImAcq *imAcq)
{
    return imAcqLoadFrame(imAcq, imAcq->currentFrame);
}

cv::Mat imAcqGetImgByCurrentTime(ImAcq *imAcq)
{
    //Calculate current image number
    if ((imAcq->method == IMACQ_CAM) || (imAcq->method == IMACQ_STREAM))
    {
        //printf("grabbing image from sensor");
        return imAcqGrab(imAcq->capture);
    }

    float secondsPassed = static_cast<float>((cv::getTickCount() - imAcq->startTime) / cv::getTickFrequency());
    secondsPassed = secondsPassed / 1000000;

    int framesPassed = static_cast<int>(secondsPassed * imAcq->fps);
    int currentFrame = imAcq->startFrame + framesPassed;

    if (imAcq->lastFrame > 0 && currentFrame > imAcq->lastFrame) return cv::Mat();

    return imAcqLoadFrame(imAcq, currentFrame);
}

cv::Mat imAcqGetImg(ImAcq *imAcq)
{
    cv::Mat img;

    if (imAcq->method == IMACQ_CAM || imAcq->method == IMACQ_VID)
    {
        imAcq->capture->read(img);
    }

    if (imAcq->method == IMACQ_IMGS)
    {
        img = imAcqLoadCurrentFrame(imAcq);
    }

    if ((imAcq->method == IMACQ_LIVESIM) || (imAcq->method == IMACQ_STREAM))
    {
        img = imAcqGetImgByCurrentTime(imAcq);
    }

    imAcqAdvance(imAcq);

    return img;
}

cv::Mat imAcqGrab(cv::VideoCapture *capture)
{
    cv::Mat frame;

    capture->read(frame);
    
    if (cv::countNonZero(frame) < 1)
    {
        // Sometimes the camera driver needs some time to start
        // sleep 100ms and try again
        for (int i = 0; i < 5; ++i)
        {
            printf("Error: Unable to grab image... retry\n");
            msleep(100);
            capture->read(frame);
            if (cv::countNonZero(frame) > 0)
            {
                break;
            }
        }
        if (cv::countNonZero(frame) < 1)
        {
            exit(-1);
        }
    }

    return frame;
}

cv::Mat imAcqGetImgByFrame(ImAcq *imAcq, int fNo)
{
    int oldFNo = imAcq->currentFrame;
    imAcq->currentFrame = fNo;

    cv::Mat img = imAcqGetImg(imAcq);

    imAcq->currentFrame = oldFNo;

    return img;
}

cv::Mat imAcqGetImgAndAdvance(ImAcq *imAcq)
{
    cv::Mat img = imAcqGetImg(imAcq);
    imAcq->currentFrame++;

    return img;
}

void imAcqAdvance(ImAcq *imAcq)
{
    imAcq->currentFrame++;
}

int imAcqHasMoreFrames(ImAcq *imAcq)
{
    if (imAcq->lastFrame < 1) return 1;

    if (imAcq->currentFrame > imAcq->lastFrame)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

int imAcqVidGetNextFrameNumber(ImAcq *imAcq)
{
    // OpenCV index starts with 0
    // maybe a OpenCV bug: cvGetCaptureProperty with CV_CAP_PROP_POS_FRAMES returns the LAST
    // frame number to be encoded not the NEXT
    return imAcq->capture->get(cv::CAP_PROP_POS_FRAMES) + 2;
}

void imAcqVidSetNextFrameNumber(ImAcq *imAcq, int nFrame)
{
    // OpenCV index starts with 0
    
    imAcq->capture->set(cv::CAP_PROP_POS_FRAMES, nFrame - 2.0);
}

int imAcqVidGetNumberOfFrames(ImAcq *imAcq)
{
    return (imAcq->capture->get(cv::CAP_PROP_FRAME_COUNT));
}
