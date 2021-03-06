
#include <OpenCV/OpenCV.h>
#include <cassert>
#include <iostream>
#include "lo/lo.h"


const char  * WINDOW_NAME  = "Virtual Conductor Tracker";
const CFIndex CASCADE_NAME_LEN = 2048;
      char    CASCADE_NAME[CASCADE_NAME_LEN] = "haarcascade_frontalface_alt2.xml"; // this is a dummy

using namespace std;

int main (int argc, char * const argv[]) 
{
    //const int scale = 2;
    lo_address oscHandle = lo_address_new(NULL, "7001");
    lo_send(oscHandle, "/hello", ""); // make the connection

    // locate haar cascade from inside application bundle
    // (this is the mac way to package application resources)
    CFBundleRef mainBundle  = CFBundleGetMainBundle ();
    assert (mainBundle);
    CFURLRef    cascade_url = CFBundleCopyResourceURL (mainBundle, CFSTR("haarcascade_frontalface_alt2"), CFSTR("xml"), NULL);
    assert (cascade_url);
    Boolean     got_it      = CFURLGetFileSystemRepresentation (cascade_url, true, 
                                                                reinterpret_cast<UInt8 *>(CASCADE_NAME), CASCADE_NAME_LEN);
    if (! got_it)
        abort ();
    
    // create all necessary instances
    cvNamedWindow (WINDOW_NAME, CV_WINDOW_AUTOSIZE);
    CvCapture * camera = cvCreateCameraCapture (CV_CAP_ANY);
    CvHaarClassifierCascade* cascade = (CvHaarClassifierCascade*) cvLoad (CASCADE_NAME, 0, 0, 0);
    CvMemStorage* storage = cvCreateMemStorage(0);
    assert (storage);

    // you do own an iSight, don't you ?!?
    if (! camera)
        abort ();

    // did we load the cascade?!?
    if (! cascade)
        abort ();

    // get an initial frame and duplicate it for later work
    IplImage *  current_frame = cvQueryFrame (camera);
    IplImage *  draw_image    = cvCreateImage(cvSize (current_frame->width, current_frame->height), IPL_DEPTH_8U, 3);
    IplImage *  gray_image    = cvCreateImage(cvSize (current_frame->width, current_frame->height), IPL_DEPTH_8U, 1);
    //IplImage *  small_image   = cvCreateImage(cvSize (current_frame->width / scale, current_frame->height / scale), IPL_DEPTH_8U, 1);
    IplImage *  hsv_image     = cvCreateImage(cvSize (current_frame->width, current_frame->height), IPL_DEPTH_8U, 3);
    IplImage *  thresh_image  = cvCreateImage(cvSize (current_frame->width, current_frame->height), IPL_DEPTH_8U, 1);    
    assert (current_frame && gray_image && draw_image && hsv_image && thresh_image);
    
    // H=[24,50]deg S=[.5,1] V=[.5,1]
    CvScalar hsv_min = cvScalar(12, 160, 128, 0);
    CvScalar hsv_max = cvScalar(25, 256, 256, 0);
    
    // for debouncing gestures
    bool lastPointWasNotAGesture = true;
    
    // as long as there are images ...
    while (current_frame = cvQueryFrame (camera))
    {
        // convert to gray and downsize
        cvCvtColor (current_frame, gray_image, CV_BGR2GRAY);
        //cvResize (gray_image, small_image, CV_INTER_LINEAR);
        cvCvtColor(current_frame, hsv_image, CV_BGR2HSV);
        
        cvInRangeS(hsv_image, hsv_min, hsv_max, thresh_image);
        
#if 0
        // detect faces
        CvSeq* faces = cvHaarDetectObjects (small_image, cascade, storage,
                                            1.1, 2, CV_HAAR_DO_CANNY_PRUNING,
                                            cvSize (30, 30));
        
        // draw faces
        cvFlip (current_frame, draw_image, 1);
        for (int i = 0; i < (faces ? faces->total : 0); i++)
        {
            CvRect* r = (CvRect*) cvGetSeqElem (faces, i);
            CvPoint center;
            int radius;
            center.x = cvRound((small_image->width - r->width*0.5 - r->x) *scale);
            center.y = cvRound((r->y + r->height*0.5)*scale);
            radius = cvRound((r->width + r->height)*0.25*scale);
            cvCircle (draw_image, center, radius, CV_RGB(0,255,0), 3, 8, 0 );
        }
#endif

        // draw boxes around gesture areas
        int boxLength = current_frame->height / 4;
        int boxLeft = (current_frame->width / 2) - (boxLength / 2);
        int boxRight = (current_frame->width / 2) + (boxLength / 2);
        
        cvRectangle(current_frame, cvPoint(boxLeft, 0), cvPoint(boxRight, boxLength), CV_RGB(0,255,0), 3, 8, 0);
        cvRectangle(current_frame, cvPoint(boxLeft, current_frame->height), cvPoint(boxRight, current_frame->height - boxLength), CV_RGB(0,255,0), 3, 8, 0);

        // add circles
        cvSmooth( thresh_image, thresh_image, CV_GAUSSIAN, 9, 9 );
        //CvSeq* circles = cvHoughCircles( gray_image, storage, CV_HOUGH_GRADIENT, 2, gray_image->height/4, 200, 100 );
        CvSeq* circles = cvHoughCircles(thresh_image, storage, CV_HOUGH_GRADIENT, 2, thresh_image->height/4, 100, 40, 10, 200);
        
        for(int i = 0; i < circles->total; i++ )
        {
            float* p = (float*)cvGetSeqElem( circles, i );
            printf("Ball! x=%f y=%f r=%f\n", p[0], p[1], p[2]);
            cvCircle(current_frame, cvPoint(cvRound(p[0]),cvRound(p[1])), cvRound(p[2]), CV_RGB(255,0,0), 3, 8, 0 );
            
            // send OSC message with "head angle" = ball position normalized to [-45,45]
            int angle = cvRound((p[0] - (current_frame->width / 2))/(current_frame->width / 2) * (-45));
            lo_send(oscHandle, "/vcon/head", "i", angle);
            
            // test if ball is in gesture area, with debouncing
            if (p[0] > boxLeft && p[0] < boxRight) {
                if (p[1] < boxLength) {
                    if (lastPointWasNotAGesture) {
                        // send volume up gesture
                        printf("vol up!\n");
                        lo_send(oscHandle, "/vcon/gesture", "i", 1);
                        lastPointWasNotAGesture = false;
                    }
                } else if (p[1] > current_frame->height - boxLength) {
                    if (lastPointWasNotAGesture) {
                        // send volume down gesture
                        printf("vol down!\n");
                        lo_send(oscHandle, "/vcon/gesture", "i", 2);
                        lastPointWasNotAGesture = false;
                    }
                } else {
                    lastPointWasNotAGesture = true;
                }
            } else {
                lastPointWasNotAGesture = true;
            }

        }
        
        // show the flipped version of the image, for mirror effect
        cvFlip (current_frame, draw_image, 1);
        cvShowImage (WINDOW_NAME, draw_image);
        
        // wait a tenth of a second for keypress and window drawing
        int key = cvWaitKey (100);
        if (key == 'q' || key == 'Q')
            break;
    }
    
    // be nice and return no error
    return 0;
}
