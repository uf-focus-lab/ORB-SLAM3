/**
 * This file is part of ORB-SLAM3
 *
 * Copyright (C) 2017-2021 Carlos Campos, Richard Elvira, Juan J. Gómez
 * Rodríguez, José M.M. Montiel and Juan D. Tardós, University of Zaragoza.
 * Copyright (C) 2014-2016 Raúl Mur-Artal, José M.M. Montiel and Juan D. Tardós,
 * University of Zaragoza.
 *
 * ORB-SLAM3 is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * ORB-SLAM3 is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * ORB-SLAM3. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Tracking.h"

#include "Debug.h"

#include "CameraModels/KannalaBrandt8.h"
#include "CameraModels/Pinhole.h"
#include "Converter.h"
#include "FrameDrawer.h"
#include "G2oTypes.h"
#include "GeometricTools.h"
#include "MLPnPsolver.h"
#include "ORB/extractor.h"
#include "ORB/matcher.h"
#include "ORB_SLAM3.h"
#include "Optimizer.h"
#include "System.h"

#include <iostream>

#include <chrono>
#include <mutex>

using namespace std;

namespace ORB_SLAM3 {

Tracking::Tracking(System *pSys, ORBVocabulary *pVoc, FrameDrawer *pFrameDrawer,
                   MapDrawer *pMapDrawer, Atlas *pAtlas,
                   KeyFrameDatabase *pKFDB, const string &strSettingPath,
                   const SensorType sensor_type, Settings *settings,
                   const string &_nameSeq)
    : mState(NO_IMAGES_YET), sensor_type(sensor_type), mTrackedFr(0),
      mbStep(false), mbOnlyTracking(false), mbMapUpdated(false), mbVO(false),
      mpORBVocabulary(pVoc), mpKeyFrameDB(pKFDB), mbReadyToInitialize(false),
      mpSystem(pSys), mpViewer(NULL), bStepByStep(false),
      mpFrameDrawer(pFrameDrawer), mpMapDrawer(pMapDrawer), mpAtlas(pAtlas),
      mnLastRelocFrameId(0), time_recently_lost(5.0), mnInitialFrameId(0),
      mbCreatedMap(false), mnFirstFrameId(0), mpCamera2(nullptr),
      mpLastKeyFrame(static_cast<KeyFrame *>(NULL)) {
  // Load camera parameters from settings file
  if (settings) {
    newParameterLoader(settings);
  } else {
    cv::FileStorage fSettings(strSettingPath, cv::FileStorage::READ);

    bool b_parse_cam = ParseCamParamFile(fSettings);
    if (!b_parse_cam) {
      cerr << "*Error with the camera parameters in the config file*" << endl;
    }

    // Load ORB parameters
    bool b_parse_orb = ParseORBParamFile(fSettings);
    if (!b_parse_orb) {
      cerr << "*Error with the ORB parameters in the config file*" << endl;
    }

    bool b_parse_imu = true;
    if (sensor_type & SensorType::USE_IMU) {
      b_parse_imu = ParseIMUParamFile(fSettings);
      if (!b_parse_imu) {
        cerr << "*Error with the IMU parameters in the config file*" << endl;
      }

      mnFramesToResetIMU = mMaxFrames;
    }

    if (!b_parse_cam || !b_parse_orb || !b_parse_imu) {
      throw std::runtime_error("Error parsing config file, format not correct");
    }
  }

  initID = 0;
  lastID = 0;
  mbInitWith3KFs = false;
  mnNumDataset = 0;

  vector<GeometricCamera *> vpCams = mpAtlas->GetAllCameras();
  cerr << "There are " << vpCams.size() << " cameras in the atlas" << endl;
  for (GeometricCamera *pCam : vpCams) {
    cerr << "Camera " << pCam->GetId();
    if (pCam->GetType() == GeometricCamera::CAM_PINHOLE) {
      cerr << " is pinhole" << endl;
    } else if (pCam->GetType() == GeometricCamera::CAM_FISHEYE) {
      cerr << " is fisheye" << endl;
    } else {
      cerr << " is unknown" << endl;
    }
  }

#ifdef REGISTER_TIMES
  vdRectStereo_ms.clear();
  vdResizeImage_ms.clear();
  vdORBExtract_ms.clear();
  vdStereoMatch_ms.clear();
  vdIMUInteg_ms.clear();
  vdPosePred_ms.clear();
  vdLMTrack_ms.clear();
  vdNewKF_ms.clear();
  vdTrackTotal_ms.clear();
#endif
}

#ifdef REGISTER_TIMES
double calcAverage(vector<double> v_times) {
  double accum = 0;
  for (double value : v_times) {
    accum += value;
  }

  return accum / v_times.size();
}

double calcDeviation(vector<double> v_times, double average) {
  double accum = 0;
  for (double value : v_times) {
    accum += pow(value - average, 2);
  }
  return sqrt(accum / v_times.size());
}

double calcAverage(vector<int> v_values) {
  double accum = 0;
  int total = 0;
  for (double value : v_values) {
    if (value == 0)
      continue;
    accum += value;
    total++;
  }

  return accum / total;
}

double calcDeviation(vector<int> v_values, double average) {
  double accum = 0;
  int total = 0;
  for (double value : v_values) {
    if (value == 0)
      continue;
    accum += pow(value - average, 2);
    total++;
  }
  return sqrt(accum / total);
}

void Tracking::LocalMapStats2File() {
  ofstream f;
  f.open("LocalMapTimeStats.txt");
  f << fixed << setprecision(6);
  f << "#Stereo rect[ms], MP culling[ms], MP creation[ms], LBA[ms], KF "
       "culling[ms], Total[ms]"
    << endl;
  for (int i = 0; i < mpLocalMapper->vdLMTotal_ms.size(); ++i) {
    f << mpLocalMapper->vdKFInsert_ms[i] << ","
      << mpLocalMapper->vdMPCulling_ms[i] << ","
      << mpLocalMapper->vdMPCreation_ms[i] << ","
      << mpLocalMapper->vdLBASync_ms[i] << ","
      << mpLocalMapper->vdKFCullingSync_ms[i] << ","
      << mpLocalMapper->vdLMTotal_ms[i] << endl;
  }

  f.close();

  f.open("LBA_Stats.txt");
  f << fixed << setprecision(6);
  f << "#LBA time[ms], KF opt[#], KF fixed[#], MP[#], Edges[#]" << endl;
  for (int i = 0; i < mpLocalMapper->vdLBASync_ms.size(); ++i) {
    f << mpLocalMapper->vdLBASync_ms[i] << "," << mpLocalMapper->vnLBA_KFopt[i]
      << "," << mpLocalMapper->vnLBA_KFfixed[i] << ","
      << mpLocalMapper->vnLBA_MPs[i] << "," << mpLocalMapper->vnLBA_edges[i]
      << endl;
  }

  f.close();
}

void Tracking::TrackStats2File() {
  ofstream f;
  f.open("SessionInfo.txt");
  f << fixed;
  f << "Number of KFs: " << mpAtlas->GetAllKeyFrames().size() << endl;
  f << "Number of MPs: " << mpAtlas->GetAllMapPoints().size() << endl;

  f << "OpenCV version: " << CV_VERSION << endl;

  f.close();

  f.open("TrackingTimeStats.txt");
  f << fixed << setprecision(6);

  f << "#Image Rect[ms], Image Resize[ms], ORB ext[ms], Stereo match[ms], IMU "
       "preint[ms], Pose pred[ms], LM track[ms], KF dec[ms], Total[ms]"
    << endl;

  for (int i = 0; i < vdTrackTotal_ms.size(); ++i) {
    double stereo_rect = 0.0;
    if (!vdRectStereo_ms.empty()) {
      stereo_rect = vdRectStereo_ms[i];
    }

    double resize_image = 0.0;
    if (!vdResizeImage_ms.empty()) {
      resize_image = vdResizeImage_ms[i];
    }

    double stereo_match = 0.0;
    if (!vdStereoMatch_ms.empty()) {
      stereo_match = vdStereoMatch_ms[i];
    }

    double imu_preint = 0.0;
    if (!vdIMUInteg_ms.empty()) {
      imu_preint = vdIMUInteg_ms[i];
    }

    f << stereo_rect << "," << resize_image << "," << vdORBExtract_ms[i] << ","
      << stereo_match << "," << imu_preint << "," << vdPosePred_ms[i] << ","
      << vdLMTrack_ms[i] << "," << vdNewKF_ms[i] << "," << vdTrackTotal_ms[i]
      << endl;
  }

  f.close();
}

void Tracking::PrintTimeStats() {
  // Save data in files
  TrackStats2File();
  LocalMapStats2File();

  ofstream f;
  f.open("ExecMean.txt");
  f << fixed;
  // Report the mean and std of each one
  cerr << endl << " TIME STATS in ms (mean$\\pm$std)" << endl;
  f << " TIME STATS in ms (mean$\\pm$std)" << endl;
  cerr << "OpenCV version: " << CV_VERSION << endl;
  f << "OpenCV version: " << CV_VERSION << endl;
  cerr << "---------------------------" << endl;
  cerr << "Tracking" << setprecision(5) << endl << endl;
  f << "---------------------------" << endl;
  f << "Tracking" << setprecision(5) << endl << endl;
  double average, deviation;
  if (!vdRectStereo_ms.empty()) {
    average = calcAverage(vdRectStereo_ms);
    deviation = calcDeviation(vdRectStereo_ms, average);
    cerr << "Stereo Rectification: " << average << "$\\pm$" << deviation
         << endl;
    f << "Stereo Rectification: " << average << "$\\pm$" << deviation << endl;
  }

  if (!vdResizeImage_ms.empty()) {
    average = calcAverage(vdResizeImage_ms);
    deviation = calcDeviation(vdResizeImage_ms, average);
    cerr << "Image Resize: " << average << "$\\pm$" << deviation << endl;
    f << "Image Resize: " << average << "$\\pm$" << deviation << endl;
  }

  average = calcAverage(vdORBExtract_ms);
  deviation = calcDeviation(vdORBExtract_ms, average);
  cerr << "ORB Extraction: " << average << "$\\pm$" << deviation << endl;
  f << "ORB Extraction: " << average << "$\\pm$" << deviation << endl;

  if (!vdStereoMatch_ms.empty()) {
    average = calcAverage(vdStereoMatch_ms);
    deviation = calcDeviation(vdStereoMatch_ms, average);
    cerr << "Stereo Matching: " << average << "$\\pm$" << deviation << endl;
    f << "Stereo Matching: " << average << "$\\pm$" << deviation << endl;
  }

  if (!vdIMUInteg_ms.empty()) {
    average = calcAverage(vdIMUInteg_ms);
    deviation = calcDeviation(vdIMUInteg_ms, average);
    cerr << "IMU Preintegration: " << average << "$\\pm$" << deviation << endl;
    f << "IMU Preintegration: " << average << "$\\pm$" << deviation << endl;
  }

  average = calcAverage(vdPosePred_ms);
  deviation = calcDeviation(vdPosePred_ms, average);
  cerr << "Pose Prediction: " << average << "$\\pm$" << deviation << endl;
  f << "Pose Prediction: " << average << "$\\pm$" << deviation << endl;

  average = calcAverage(vdLMTrack_ms);
  deviation = calcDeviation(vdLMTrack_ms, average);
  cerr << "LM Track: " << average << "$\\pm$" << deviation << endl;
  f << "LM Track: " << average << "$\\pm$" << deviation << endl;

  average = calcAverage(vdNewKF_ms);
  deviation = calcDeviation(vdNewKF_ms, average);
  cerr << "New KF decision: " << average << "$\\pm$" << deviation << endl;
  f << "New KF decision: " << average << "$\\pm$" << deviation << endl;

  average = calcAverage(vdTrackTotal_ms);
  deviation = calcDeviation(vdTrackTotal_ms, average);
  cerr << "Total Tracking: " << average << "$\\pm$" << deviation << endl;
  f << "Total Tracking: " << average << "$\\pm$" << deviation << endl;

  // Local Mapping time stats
  cerr << endl << endl << endl;
  cerr << "Local Mapping" << endl << endl;
  f << endl << "Local Mapping" << endl << endl;

  average = calcAverage(mpLocalMapper->vdKFInsert_ms);
  deviation = calcDeviation(mpLocalMapper->vdKFInsert_ms, average);
  cerr << "KF Insertion: " << average << "$\\pm$" << deviation << endl;
  f << "KF Insertion: " << average << "$\\pm$" << deviation << endl;

  average = calcAverage(mpLocalMapper->vdMPCulling_ms);
  deviation = calcDeviation(mpLocalMapper->vdMPCulling_ms, average);
  cerr << "MP Culling: " << average << "$\\pm$" << deviation << endl;
  f << "MP Culling: " << average << "$\\pm$" << deviation << endl;

  average = calcAverage(mpLocalMapper->vdMPCreation_ms);
  deviation = calcDeviation(mpLocalMapper->vdMPCreation_ms, average);
  cerr << "MP Creation: " << average << "$\\pm$" << deviation << endl;
  f << "MP Creation: " << average << "$\\pm$" << deviation << endl;

  average = calcAverage(mpLocalMapper->vdLBA_ms);
  deviation = calcDeviation(mpLocalMapper->vdLBA_ms, average);
  cerr << "LBA: " << average << "$\\pm$" << deviation << endl;
  f << "LBA: " << average << "$\\pm$" << deviation << endl;

  average = calcAverage(mpLocalMapper->vdKFCulling_ms);
  deviation = calcDeviation(mpLocalMapper->vdKFCulling_ms, average);
  cerr << "KF Culling: " << average << "$\\pm$" << deviation << endl;
  f << "KF Culling: " << average << "$\\pm$" << deviation << endl;

  average = calcAverage(mpLocalMapper->vdLMTotal_ms);
  deviation = calcDeviation(mpLocalMapper->vdLMTotal_ms, average);
  cerr << "Total Local Mapping: " << average << "$\\pm$" << deviation << endl;
  f << "Total Local Mapping: " << average << "$\\pm$" << deviation << endl;

  // Local Mapping LBA complexity
  cerr << "---------------------------" << endl;
  cerr << endl << "LBA complexity (mean$\\pm$std)" << endl;
  f << "---------------------------" << endl;
  f << endl << "LBA complexity (mean$\\pm$std)" << endl;

  average = calcAverage(mpLocalMapper->vnLBA_edges);
  deviation = calcDeviation(mpLocalMapper->vnLBA_edges, average);
  cerr << "LBA Edges: " << average << "$\\pm$" << deviation << endl;
  f << "LBA Edges: " << average << "$\\pm$" << deviation << endl;

  average = calcAverage(mpLocalMapper->vnLBA_KFopt);
  deviation = calcDeviation(mpLocalMapper->vnLBA_KFopt, average);
  cerr << "LBA KF optimized: " << average << "$\\pm$" << deviation << endl;
  f << "LBA KF optimized: " << average << "$\\pm$" << deviation << endl;

  average = calcAverage(mpLocalMapper->vnLBA_KFfixed);
  deviation = calcDeviation(mpLocalMapper->vnLBA_KFfixed, average);
  cerr << "LBA KF fixed: " << average << "$\\pm$" << deviation << endl;
  f << "LBA KF fixed: " << average << "$\\pm$" << deviation << endl;

  average = calcAverage(mpLocalMapper->vnLBA_MPs);
  deviation = calcDeviation(mpLocalMapper->vnLBA_MPs, average);
  cerr << "LBA MP: " << average << "$\\pm$" << deviation << endl << endl;
  f << "LBA MP: " << average << "$\\pm$" << deviation << endl << endl;

  cerr << "LBA executions: " << mpLocalMapper->nLBA_exec << endl;
  cerr << "LBA aborts: " << mpLocalMapper->nLBA_abort << endl;
  f << "LBA executions: " << mpLocalMapper->nLBA_exec << endl;
  f << "LBA aborts: " << mpLocalMapper->nLBA_abort << endl;

  // Map complexity
  cerr << "---------------------------" << endl;
  cerr << endl << "Map complexity" << endl;
  cerr << "KFs in map: " << mpAtlas->GetAllKeyFrames().size() << endl;
  cerr << "MPs in map: " << mpAtlas->GetAllMapPoints().size() << endl;
  f << "---------------------------" << endl;
  f << endl << "Map complexity" << endl;
  vector<Map *> vpMaps = mpAtlas->GetAllMaps();
  Map *pBestMap = vpMaps[0];
  for (int i = 1; i < vpMaps.size(); ++i) {
    if (pBestMap->GetAllKeyFrames().size() <
        vpMaps[i]->GetAllKeyFrames().size()) {
      pBestMap = vpMaps[i];
    }
  }

  f << "KFs in map: " << pBestMap->GetAllKeyFrames().size() << endl;
  f << "MPs in map: " << pBestMap->GetAllMapPoints().size() << endl;

  f << "---------------------------" << endl;
  f << endl << "Place Recognition (mean$\\pm$std)" << endl;
  cerr << "---------------------------" << endl;
  cerr << endl << "Place Recognition (mean$\\pm$std)" << endl;
  average = calcAverage(mpLoopClosing->vdDataQuery_ms);
  deviation = calcDeviation(mpLoopClosing->vdDataQuery_ms, average);
  f << "Database Query: " << average << "$\\pm$" << deviation << endl;
  cerr << "Database Query: " << average << "$\\pm$" << deviation << endl;
  average = calcAverage(mpLoopClosing->vdEstSim3_ms);
  deviation = calcDeviation(mpLoopClosing->vdEstSim3_ms, average);
  f << "SE3 estimation: " << average << "$\\pm$" << deviation << endl;
  cerr << "SE3 estimation: " << average << "$\\pm$" << deviation << endl;
  average = calcAverage(mpLoopClosing->vdPRTotal_ms);
  deviation = calcDeviation(mpLoopClosing->vdPRTotal_ms, average);
  f << "Total Place Recognition: " << average << "$\\pm$" << deviation << endl
    << endl;
  cerr << "Total Place Recognition: " << average << "$\\pm$" << deviation
       << endl
       << endl;

  f << endl << "Loop Closing (mean$\\pm$std)" << endl;
  cerr << endl << "Loop Closing (mean$\\pm$std)" << endl;
  average = calcAverage(mpLoopClosing->vdLoopFusion_ms);
  deviation = calcDeviation(mpLoopClosing->vdLoopFusion_ms, average);
  f << "Loop Fusion: " << average << "$\\pm$" << deviation << endl;
  cerr << "Loop Fusion: " << average << "$\\pm$" << deviation << endl;
  average = calcAverage(mpLoopClosing->vdLoopOptEss_ms);
  deviation = calcDeviation(mpLoopClosing->vdLoopOptEss_ms, average);
  f << "Essential Graph: " << average << "$\\pm$" << deviation << endl;
  cerr << "Essential Graph: " << average << "$\\pm$" << deviation << endl;
  average = calcAverage(mpLoopClosing->vdLoopTotal_ms);
  deviation = calcDeviation(mpLoopClosing->vdLoopTotal_ms, average);
  f << "Total Loop Closing: " << average << "$\\pm$" << deviation << endl
    << endl;
  cerr << "Total Loop Closing: " << average << "$\\pm$" << deviation << endl
       << endl;

  f << "Numb exec: " << mpLoopClosing->nLoop << endl;
  cerr << "Num exec: " << mpLoopClosing->nLoop << endl;
  average = calcAverage(mpLoopClosing->vnLoopKFs);
  deviation = calcDeviation(mpLoopClosing->vnLoopKFs, average);
  f << "Number of KFs: " << average << "$\\pm$" << deviation << endl;
  cerr << "Number of KFs: " << average << "$\\pm$" << deviation << endl;

  f << endl << "Map Merging (mean$\\pm$std)" << endl;
  cerr << endl << "Map Merging (mean$\\pm$std)" << endl;
  average = calcAverage(mpLoopClosing->vdMergeMaps_ms);
  deviation = calcDeviation(mpLoopClosing->vdMergeMaps_ms, average);
  f << "Merge Maps: " << average << "$\\pm$" << deviation << endl;
  cerr << "Merge Maps: " << average << "$\\pm$" << deviation << endl;
  average = calcAverage(mpLoopClosing->vdWeldingBA_ms);
  deviation = calcDeviation(mpLoopClosing->vdWeldingBA_ms, average);
  f << "Welding BA: " << average << "$\\pm$" << deviation << endl;
  cerr << "Welding BA: " << average << "$\\pm$" << deviation << endl;
  average = calcAverage(mpLoopClosing->vdMergeOptEss_ms);
  deviation = calcDeviation(mpLoopClosing->vdMergeOptEss_ms, average);
  f << "Optimization Ess.: " << average << "$\\pm$" << deviation << endl;
  cerr << "Optimization Ess.: " << average << "$\\pm$" << deviation << endl;
  average = calcAverage(mpLoopClosing->vdMergeTotal_ms);
  deviation = calcDeviation(mpLoopClosing->vdMergeTotal_ms, average);
  f << "Total Map Merging: " << average << "$\\pm$" << deviation << endl
    << endl;
  cerr << "Total Map Merging: " << average << "$\\pm$" << deviation << endl
       << endl;

  f << "Numb exec: " << mpLoopClosing->nMerges << endl;
  cerr << "Num exec: " << mpLoopClosing->nMerges << endl;
  average = calcAverage(mpLoopClosing->vnMergeKFs);
  deviation = calcDeviation(mpLoopClosing->vnMergeKFs, average);
  f << "Number of KFs: " << average << "$\\pm$" << deviation << endl;
  cerr << "Number of KFs: " << average << "$\\pm$" << deviation << endl;
  average = calcAverage(mpLoopClosing->vnMergeMPs);
  deviation = calcDeviation(mpLoopClosing->vnMergeMPs, average);
  f << "Number of MPs: " << average << "$\\pm$" << deviation << endl;
  cerr << "Number of MPs: " << average << "$\\pm$" << deviation << endl;

  f << endl << "Full GBA (mean$\\pm$std)" << endl;
  cerr << endl << "Full GBA (mean$\\pm$std)" << endl;
  average = calcAverage(mpLoopClosing->vdGBA_ms);
  deviation = calcDeviation(mpLoopClosing->vdGBA_ms, average);
  f << "GBA: " << average << "$\\pm$" << deviation << endl;
  cerr << "GBA: " << average << "$\\pm$" << deviation << endl;
  average = calcAverage(mpLoopClosing->vdUpdateMap_ms);
  deviation = calcDeviation(mpLoopClosing->vdUpdateMap_ms, average);
  f << "Map Update: " << average << "$\\pm$" << deviation << endl;
  cerr << "Map Update: " << average << "$\\pm$" << deviation << endl;
  average = calcAverage(mpLoopClosing->vdFGBATotal_ms);
  deviation = calcDeviation(mpLoopClosing->vdFGBATotal_ms, average);
  f << "Total Full GBA: " << average << "$\\pm$" << deviation << endl << endl;
  cerr << "Total Full GBA: " << average << "$\\pm$" << deviation << endl
       << endl;

  f << "Numb exec: " << mpLoopClosing->nFGBA_exec << endl;
  cerr << "Num exec: " << mpLoopClosing->nFGBA_exec << endl;
  f << "Numb abort: " << mpLoopClosing->nFGBA_abort << endl;
  cerr << "Num abort: " << mpLoopClosing->nFGBA_abort << endl;
  average = calcAverage(mpLoopClosing->vnGBAKFs);
  deviation = calcDeviation(mpLoopClosing->vnGBAKFs, average);
  f << "Number of KFs: " << average << "$\\pm$" << deviation << endl;
  cerr << "Number of KFs: " << average << "$\\pm$" << deviation << endl;
  average = calcAverage(mpLoopClosing->vnGBAMPs);
  deviation = calcDeviation(mpLoopClosing->vnGBAMPs, average);
  f << "Number of MPs: " << average << "$\\pm$" << deviation << endl;
  cerr << "Number of MPs: " << average << "$\\pm$" << deviation << endl;

  f.close();
}

#endif

Tracking::~Tracking() {
  // f_track_stats.close();
}

void Tracking::newParameterLoader(Settings *settings) {
  mpCamera = settings->camera1();
  mpCamera = mpAtlas->AddCamera(mpCamera);

  if (settings->needToUndistort()) {
    mDistCoef = settings->camera1DistortionCoef();
  } else {
    mDistCoef = cv::Mat::zeros(4, 1, CV_32F);
  }

  // TODO: missing image scaling and rectification
  mImageScale = 1.0f;

  mK = cv::Mat::eye(3, 3, CV_32F);
  mK.at<float>(0, 0) = mpCamera->getParameter(0);
  mK.at<float>(1, 1) = mpCamera->getParameter(1);
  mK.at<float>(0, 2) = mpCamera->getParameter(2);
  mK.at<float>(1, 2) = mpCamera->getParameter(3);

  mK_.setIdentity();
  mK_(0, 0) = mpCamera->getParameter(0);
  mK_(1, 1) = mpCamera->getParameter(1);
  mK_(0, 2) = mpCamera->getParameter(2);
  mK_(1, 2) = mpCamera->getParameter(3);

  if ((sensor_type == SensorType::STEREO ||
       sensor_type == SensorType::IMU_STEREO ||
       sensor_type == SensorType::IMU_RGB_D) &&
      settings->cameraType() == Settings::KannalaBrandt) {
    mpCamera2 = settings->camera2();
    mpCamera2 = mpAtlas->AddCamera(mpCamera2);

    mTlr = settings->Tlr();

    mpFrameDrawer->both = true;
  }

  if ((sensor_type & SensorType::CAMERA_MASK) == SensorType::STEREO ||
      (sensor_type & SensorType::CAMERA_MASK) == SensorType::RGB_D) {
    mbf = settings->bf();
    mThDepth = settings->b() * settings->thDepth();
  }

  if ((sensor_type & SensorType::CAMERA_MASK) == SensorType::RGB_D) {
    mDepthMapFactor = settings->depthMapFactor();
    if (fabs(mDepthMapFactor) < 1e-5)
      mDepthMapFactor = 1;
    else
      mDepthMapFactor = 1.0f / mDepthMapFactor;
  }

  mMinFrames = 0;
  mMaxFrames = settings->fps();
  mbRGB = settings->rgb();

  // ORB parameters
  int nFeatures = settings->nFeatures();
  int nLevels = settings->nLevels();
  int fIniThFAST = settings->initThFAST();
  int fMinThFAST = settings->minThFAST();
  float fScaleFactor = settings->scaleFactor();

  mpORBextractorLeft = new ORBextractor(nFeatures, fScaleFactor, nLevels,
                                        fIniThFAST, fMinThFAST);

  if ((sensor_type & SensorType::CAMERA_MASK) == SensorType::STEREO)
    mpORBextractorRight = new ORBextractor(nFeatures, fScaleFactor, nLevels,
                                           fIniThFAST, fMinThFAST);

  if ((sensor_type & SensorType::CAMERA_MASK) == SensorType::MONOCULAR)
    mpIniORBextractor = new ORBextractor(5 * nFeatures, fScaleFactor, nLevels,
                                         fIniThFAST, fMinThFAST);

  // IMU parameters
  Sophus::SE3f Tbc = settings->Tbc();
  mInsertKFsLost = settings->insertKFsWhenLost();
  mImuFreq = settings->imuFrequency();
  mImuPer = 0.001; // 1.0 / (double) mImuFreq;     //TODO: ESTO ESTA BIEN?
  float Ng = settings->noiseGyro();
  float Na = settings->noiseAcc();
  float Ngw = settings->gyroWalk();
  float Naw = settings->accWalk();

  const float sf = sqrt(mImuFreq);
  mpImuCalib = new IMU::Calib(Tbc, Ng * sf, Na * sf, Ngw / sf, Naw / sf);

  mpImuPreintegratedFromLastKF =
      new IMU::Preintegrated(IMU::Bias(), *mpImuCalib);
}

bool Tracking::ParseCamParamFile(cv::FileStorage &fSettings) {
  mDistCoef = cv::Mat::zeros(4, 1, CV_32F);
  cerr << endl << "Camera Parameters: " << endl;
  bool b_miss_params = false;

  string sCameraName = fSettings["Camera.type"];
  if (sCameraName == "PinHole") {
    float fx, fy, cx, cy;
    mImageScale = 1.f;

    // Camera calibration parameters
    cv::FileNode node = fSettings["Camera.fx"];
    if (!node.empty() && node.isReal()) {
      fx = node.real();
    } else {
      cerr << "*Camera.fx parameter doesn't exist or is not a real number*"
           << endl;
      b_miss_params = true;
    }

    node = fSettings["Camera.fy"];
    if (!node.empty() && node.isReal()) {
      fy = node.real();
    } else {
      cerr << "*Camera.fy parameter doesn't exist or is not a real number*"
           << endl;
      b_miss_params = true;
    }

    node = fSettings["Camera.cx"];
    if (!node.empty() && node.isReal()) {
      cx = node.real();
    } else {
      cerr << "*Camera.cx parameter doesn't exist or is not a real number*"
           << endl;
      b_miss_params = true;
    }

    node = fSettings["Camera.cy"];
    if (!node.empty() && node.isReal()) {
      cy = node.real();
    } else {
      cerr << "*Camera.cy parameter doesn't exist or is not a real number*"
           << endl;
      b_miss_params = true;
    }

    // Distortion parameters
    node = fSettings["Camera.k1"];
    if (!node.empty() && node.isReal()) {
      mDistCoef.at<float>(0) = node.real();
    } else {
      cerr << "*Camera.k1 parameter doesn't exist or is not a real number*"
           << endl;
      b_miss_params = true;
    }

    node = fSettings["Camera.k2"];
    if (!node.empty() && node.isReal()) {
      mDistCoef.at<float>(1) = node.real();
    } else {
      cerr << "*Camera.k2 parameter doesn't exist or is not a real number*"
           << endl;
      b_miss_params = true;
    }

    node = fSettings["Camera.p1"];
    if (!node.empty() && node.isReal()) {
      mDistCoef.at<float>(2) = node.real();
    } else {
      cerr << "*Camera.p1 parameter doesn't exist or is not a real number*"
           << endl;
      b_miss_params = true;
    }

    node = fSettings["Camera.p2"];
    if (!node.empty() && node.isReal()) {
      mDistCoef.at<float>(3) = node.real();
    } else {
      cerr << "*Camera.p2 parameter doesn't exist or is not a real number*"
           << endl;
      b_miss_params = true;
    }

    node = fSettings["Camera.k3"];
    if (!node.empty() && node.isReal()) {
      mDistCoef.resize(5);
      mDistCoef.at<float>(4) = node.real();
    }

    node = fSettings["Camera.imageScale"];
    if (!node.empty() && node.isReal()) {
      mImageScale = node.real();
    }

    if (b_miss_params) {
      return false;
    }

    if (mImageScale != 1.f) {
      // K matrix parameters must be scaled.
      fx = fx * mImageScale;
      fy = fy * mImageScale;
      cx = cx * mImageScale;
      cy = cy * mImageScale;
    }

    vector<float> vCamCalib{fx, fy, cx, cy};

    mpCamera = new Pinhole(vCamCalib);

    mpCamera = mpAtlas->AddCamera(mpCamera);

    cerr << "- Camera: Pinhole" << endl;
    cerr << "- Image scale: " << mImageScale << endl;
    cerr << "- fx: " << fx << endl;
    cerr << "- fy: " << fy << endl;
    cerr << "- cx: " << cx << endl;
    cerr << "- cy: " << cy << endl;
    cerr << "- k1: " << mDistCoef.at<float>(0) << endl;
    cerr << "- k2: " << mDistCoef.at<float>(1) << endl;

    cerr << "- p1: " << mDistCoef.at<float>(2) << endl;
    cerr << "- p2: " << mDistCoef.at<float>(3) << endl;

    if (mDistCoef.rows == 5)
      cerr << "- k3: " << mDistCoef.at<float>(4) << endl;

    mK = cv::Mat::eye(3, 3, CV_32F);
    mK.at<float>(0, 0) = fx;
    mK.at<float>(1, 1) = fy;
    mK.at<float>(0, 2) = cx;
    mK.at<float>(1, 2) = cy;

    mK_.setIdentity();
    mK_(0, 0) = fx;
    mK_(1, 1) = fy;
    mK_(0, 2) = cx;
    mK_(1, 2) = cy;
  } else if (sCameraName == "KannalaBrandt8") {
    float fx, fy, cx, cy;
    float k1, k2, k3, k4;
    mImageScale = 1.f;

    // Camera calibration parameters
    cv::FileNode node = fSettings["Camera.fx"];
    if (!node.empty() && node.isReal()) {
      fx = node.real();
    } else {
      cerr << "*Camera.fx parameter doesn't exist or is not a real number*"
           << endl;
      b_miss_params = true;
    }
    node = fSettings["Camera.fy"];
    if (!node.empty() && node.isReal()) {
      fy = node.real();
    } else {
      cerr << "*Camera.fy parameter doesn't exist or is not a real number*"
           << endl;
      b_miss_params = true;
    }

    node = fSettings["Camera.cx"];
    if (!node.empty() && node.isReal()) {
      cx = node.real();
    } else {
      cerr << "*Camera.cx parameter doesn't exist or is not a real number*"
           << endl;
      b_miss_params = true;
    }

    node = fSettings["Camera.cy"];
    if (!node.empty() && node.isReal()) {
      cy = node.real();
    } else {
      cerr << "*Camera.cy parameter doesn't exist or is not a real number*"
           << endl;
      b_miss_params = true;
    }

    // Distortion parameters
    node = fSettings["Camera.k1"];
    if (!node.empty() && node.isReal()) {
      k1 = node.real();
    } else {
      cerr << "*Camera.k1 parameter doesn't exist or is not a real number*"
           << endl;
      b_miss_params = true;
    }
    node = fSettings["Camera.k2"];
    if (!node.empty() && node.isReal()) {
      k2 = node.real();
    } else {
      cerr << "*Camera.k2 parameter doesn't exist or is not a real number*"
           << endl;
      b_miss_params = true;
    }

    node = fSettings["Camera.k3"];
    if (!node.empty() && node.isReal()) {
      k3 = node.real();
    } else {
      cerr << "*Camera.k3 parameter doesn't exist or is not a real number*"
           << endl;
      b_miss_params = true;
    }

    node = fSettings["Camera.k4"];
    if (!node.empty() && node.isReal()) {
      k4 = node.real();
    } else {
      cerr << "*Camera.k4 parameter doesn't exist or is not a real number*"
           << endl;
      b_miss_params = true;
    }

    node = fSettings["Camera.imageScale"];
    if (!node.empty() && node.isReal()) {
      mImageScale = node.real();
    }

    if (!b_miss_params) {
      if (mImageScale != 1.f) {
        // K matrix parameters must be scaled.
        fx = fx * mImageScale;
        fy = fy * mImageScale;
        cx = cx * mImageScale;
        cy = cy * mImageScale;
      }

      vector<float> vCamCalib{fx, fy, cx, cy, k1, k2, k3, k4};
      mpCamera = new KannalaBrandt8(vCamCalib);
      mpCamera = mpAtlas->AddCamera(mpCamera);
      cerr << "- Camera: Fisheye" << endl;
      cerr << "- Image scale: " << mImageScale << endl;
      cerr << "- fx: " << fx << endl;
      cerr << "- fy: " << fy << endl;
      cerr << "- cx: " << cx << endl;
      cerr << "- cy: " << cy << endl;
      cerr << "- k1: " << k1 << endl;
      cerr << "- k2: " << k2 << endl;
      cerr << "- k3: " << k3 << endl;
      cerr << "- k4: " << k4 << endl;

      mK = cv::Mat::eye(3, 3, CV_32F);
      mK.at<float>(0, 0) = fx;
      mK.at<float>(1, 1) = fy;
      mK.at<float>(0, 2) = cx;
      mK.at<float>(1, 2) = cy;

      mK_.setIdentity();
      mK_(0, 0) = fx;
      mK_(1, 1) = fy;
      mK_(0, 2) = cx;
      mK_(1, 2) = cy;
    }

    if ((sensor_type & SensorType::CAMERA_MASK) == SensorType::STEREO ||
        (sensor_type & SensorType::CAMERA_MASK) == SensorType::RGB_D) {
      // Right camera
      // Camera calibration parameters
      cv::FileNode node = fSettings["Camera2.fx"];
      if (!node.empty() && node.isReal()) {
        fx = node.real();
      } else {
        cerr << "*Camera2.fx parameter doesn't exist or is not a real number*"
             << endl;
        b_miss_params = true;
      }
      node = fSettings["Camera2.fy"];
      if (!node.empty() && node.isReal()) {
        fy = node.real();
      } else {
        cerr << "*Camera2.fy parameter doesn't exist or is not a real number*"
             << endl;
        b_miss_params = true;
      }

      node = fSettings["Camera2.cx"];
      if (!node.empty() && node.isReal()) {
        cx = node.real();
      } else {
        cerr << "*Camera2.cx parameter doesn't exist or is not a real number*"
             << endl;
        b_miss_params = true;
      }

      node = fSettings["Camera2.cy"];
      if (!node.empty() && node.isReal()) {
        cy = node.real();
      } else {
        cerr << "*Camera2.cy parameter doesn't exist or is not a real number*"
             << endl;
        b_miss_params = true;
      }

      // Distortion parameters
      node = fSettings["Camera2.k1"];
      if (!node.empty() && node.isReal()) {
        k1 = node.real();
      } else {
        cerr << "*Camera2.k1 parameter doesn't exist or is not a real number*"
             << endl;
        b_miss_params = true;
      }
      node = fSettings["Camera2.k2"];
      if (!node.empty() && node.isReal()) {
        k2 = node.real();
      } else {
        cerr << "*Camera2.k2 parameter doesn't exist or is not a real number*"
             << endl;
        b_miss_params = true;
      }

      node = fSettings["Camera2.k3"];
      if (!node.empty() && node.isReal()) {
        k3 = node.real();
      } else {
        cerr << "*Camera2.k3 parameter doesn't exist or is not a real number*"
             << endl;
        b_miss_params = true;
      }

      node = fSettings["Camera2.k4"];
      if (!node.empty() && node.isReal()) {
        k4 = node.real();
      } else {
        cerr << "*Camera2.k4 parameter doesn't exist or is not a real number*"
             << endl;
        b_miss_params = true;
      }

      int leftLappingBegin = -1;
      int leftLappingEnd = -1;

      int rightLappingBegin = -1;
      int rightLappingEnd = -1;

      node = fSettings["Camera.lappingBegin"];
      if (!node.empty() && node.isInt()) {
        leftLappingBegin = node.operator int();
      } else {
        cerr << "WARNING: Camera.lappingBegin not correctly defined" << endl;
      }
      node = fSettings["Camera.lappingEnd"];
      if (!node.empty() && node.isInt()) {
        leftLappingEnd = node.operator int();
      } else {
        cerr << "WARNING: Camera.lappingEnd not correctly defined" << endl;
      }
      node = fSettings["Camera2.lappingBegin"];
      if (!node.empty() && node.isInt()) {
        rightLappingBegin = node.operator int();
      } else {
        cerr << "WARNING: Camera2.lappingBegin not correctly defined" << endl;
      }
      node = fSettings["Camera2.lappingEnd"];
      if (!node.empty() && node.isInt()) {
        rightLappingEnd = node.operator int();
      } else {
        cerr << "WARNING: Camera2.lappingEnd not correctly defined" << endl;
      }

      node = fSettings["Tlr"];
      cv::Mat cvTlr;
      if (!node.empty()) {
        cvTlr = node.mat();
        if (cvTlr.rows != 3 || cvTlr.cols != 4) {
          cerr << "*Tlr matrix have to be a 3x4 transformation matrix*" << endl;
          b_miss_params = true;
        }
      } else {
        cerr << "*Tlr matrix doesn't exist*" << endl;
        b_miss_params = true;
      }

      if (!b_miss_params) {
        if (mImageScale != 1.f) {
          // K matrix parameters must be scaled.
          fx = fx * mImageScale;
          fy = fy * mImageScale;
          cx = cx * mImageScale;
          cy = cy * mImageScale;

          leftLappingBegin = leftLappingBegin * mImageScale;
          leftLappingEnd = leftLappingEnd * mImageScale;
          rightLappingBegin = rightLappingBegin * mImageScale;
          rightLappingEnd = rightLappingEnd * mImageScale;
        }

        static_cast<KannalaBrandt8 *>(mpCamera)->mvLappingArea[0] =
            leftLappingBegin;
        static_cast<KannalaBrandt8 *>(mpCamera)->mvLappingArea[1] =
            leftLappingEnd;

        mpFrameDrawer->both = true;

        vector<float> vCamCalib2{fx, fy, cx, cy, k1, k2, k3, k4};
        mpCamera2 = new KannalaBrandt8(vCamCalib2);
        mpCamera2 = mpAtlas->AddCamera(mpCamera2);

        mTlr = Converter::toSophus(cvTlr);

        static_cast<KannalaBrandt8 *>(mpCamera2)->mvLappingArea[0] =
            rightLappingBegin;
        static_cast<KannalaBrandt8 *>(mpCamera2)->mvLappingArea[1] =
            rightLappingEnd;

        cerr << "- Camera1 Lapping: " << leftLappingBegin << ", "
             << leftLappingEnd << endl;

        cerr << endl << "Camera2 Parameters:" << endl;
        cerr << "- Camera: Fisheye" << endl;
        cerr << "- Image scale: " << mImageScale << endl;
        cerr << "- fx: " << fx << endl;
        cerr << "- fy: " << fy << endl;
        cerr << "- cx: " << cx << endl;
        cerr << "- cy: " << cy << endl;
        cerr << "- k1: " << k1 << endl;
        cerr << "- k2: " << k2 << endl;
        cerr << "- k3: " << k3 << endl;
        cerr << "- k4: " << k4 << endl;

        cerr << "- mTlr: \n" << cvTlr << endl;

        cerr << "- Camera2 Lapping: " << rightLappingBegin << ", "
             << rightLappingEnd << endl;
      }
    }

    if (b_miss_params) {
      return false;
    }

  } else {
    cerr << "*Not Supported Camera Sensor*" << endl;
    cerr << "Check an example configuration file with the desired sensor"
         << endl;
  }

  if ((sensor_type & SensorType::CAMERA_MASK) == SensorType::STEREO ||
      (sensor_type & SensorType::CAMERA_MASK) == SensorType::RGB_D) {
    cv::FileNode node = fSettings["Camera.bf"];
    if (!node.empty() && node.isReal()) {
      mbf = node.real();
      if (mImageScale != 1.f) {
        mbf *= mImageScale;
      }
    } else {
      cerr << "*Camera.bf parameter doesn't exist or is not a real number*"
           << endl;
      b_miss_params = true;
    }
  }

  float fps = fSettings["Camera.fps"];
  if (fps == 0)
    fps = 30;

  // Max/Min Frames to insert keyframes and to check relocalisation
  mMinFrames = 0;
  mMaxFrames = fps;

  cerr << "- fps: " << fps << endl;

  int nRGB = fSettings["Camera.RGB"];
  mbRGB = nRGB;

  if (mbRGB)
    cerr << "- color order: RGB (ignored if grayscale)" << endl;
  else
    cerr << "- color order: BGR (ignored if grayscale)" << endl;

  if ((sensor_type & SensorType::CAMERA_MASK) == SensorType::STEREO ||
      (sensor_type & SensorType::CAMERA_MASK) == SensorType::RGB_D) {
    float fx = mpCamera->getParameter(0);
    cv::FileNode node = fSettings["ThDepth"];
    if (!node.empty() && node.isReal()) {
      mThDepth = node.real();
      mThDepth = mbf * mThDepth / fx;
      cerr << endl
           << "Depth Threshold (Close/Far Points): " << mThDepth << endl;
    } else {
      cerr << "*ThDepth parameter doesn't exist or is not a real number*"
           << endl;
      b_miss_params = true;
    }
  }

  if ((sensor_type & SensorType::CAMERA_MASK) == SensorType::RGB_D) {
    cv::FileNode node = fSettings["DepthMapFactor"];
    if (!node.empty() && node.isReal()) {
      mDepthMapFactor = node.real();
      if (fabs(mDepthMapFactor) < 1e-5)
        mDepthMapFactor = 1;
      else
        mDepthMapFactor = 1.0f / mDepthMapFactor;
    } else {
      cerr << "*DepthMapFactor parameter doesn't exist or is not a real number*"
           << endl;
      b_miss_params = true;
    }
  }

  if (b_miss_params) {
    return false;
  }

  return true;
}

bool Tracking::ParseORBParamFile(cv::FileStorage &fSettings) {
  bool b_miss_params = false;
  int nFeatures, nLevels, fIniThFAST, fMinThFAST;
  float fScaleFactor;

  cv::FileNode node = fSettings["ORBextractor.nFeatures"];
  if (!node.empty() && node.isInt()) {
    nFeatures = node.operator int();
  } else {
    cerr << "*ORBextractor.nFeatures parameter doesn't exist or is not an "
            "integer*"
         << endl;
    b_miss_params = true;
  }

  node = fSettings["ORBextractor.scaleFactor"];
  if (!node.empty() && node.isReal()) {
    fScaleFactor = node.real();
  } else {
    cerr << "*ORBextractor.scaleFactor parameter doesn't exist or is not a "
            "real number*"
         << endl;
    b_miss_params = true;
  }

  node = fSettings["ORBextractor.nLevels"];
  if (!node.empty() && node.isInt()) {
    nLevels = node.operator int();
  } else {
    cerr
        << "*ORBextractor.nLevels parameter doesn't exist or is not an integer*"
        << endl;
    b_miss_params = true;
  }

  node = fSettings["ORBextractor.iniThFAST"];
  if (!node.empty() && node.isInt()) {
    fIniThFAST = node.operator int();
  } else {
    cerr << "*ORBextractor.iniThFAST parameter doesn't exist or is not an "
            "integer*"
         << endl;
    b_miss_params = true;
  }

  node = fSettings["ORBextractor.minThFAST"];
  if (!node.empty() && node.isInt()) {
    fMinThFAST = node.operator int();
  } else {
    cerr << "*ORBextractor.minThFAST parameter doesn't exist or is not an "
            "integer*"
         << endl;
    b_miss_params = true;
  }

  if (b_miss_params) {
    return false;
  }

  mpORBextractorLeft = new ORBextractor(nFeatures, fScaleFactor, nLevels,
                                        fIniThFAST, fMinThFAST);

  if ((sensor_type & SensorType::CAMERA_MASK) == SensorType::STEREO)
    mpORBextractorRight = new ORBextractor(nFeatures, fScaleFactor, nLevels,
                                           fIniThFAST, fMinThFAST);

  if ((sensor_type & SensorType::CAMERA_MASK) == SensorType::MONOCULAR)
    mpIniORBextractor = new ORBextractor(5 * nFeatures, fScaleFactor, nLevels,
                                         fIniThFAST, fMinThFAST);

  cerr << endl << "ORB Extractor Parameters: " << endl;
  cerr << "- Number of Features: " << nFeatures << endl;
  cerr << "- Scale Levels: " << nLevels << endl;
  cerr << "- Scale Factor: " << fScaleFactor << endl;
  cerr << "- Initial Fast Threshold: " << fIniThFAST << endl;
  cerr << "- Minimum Fast Threshold: " << fMinThFAST << endl;

  return true;
}

bool Tracking::ParseIMUParamFile(cv::FileStorage &fSettings) {
  bool b_miss_params = false;

  cv::Mat cvTbc;
  cv::FileNode node = fSettings["Tbc"];
  if (!node.empty()) {
    cvTbc = node.mat();
    if (cvTbc.rows != 4 || cvTbc.cols != 4) {
      cerr << "*Tbc matrix have to be a 4x4 transformation matrix*" << endl;
      b_miss_params = true;
    }
  } else {
    cerr << "*Tbc matrix doesn't exist*" << endl;
    b_miss_params = true;
  }
  cerr << endl;
  cerr << "Left camera to Imu Transform (Tbc): " << endl << cvTbc << endl;
  Eigen::Matrix<float, 4, 4, Eigen::RowMajor> eigTbc(cvTbc.ptr<float>(0));
  Sophus::SE3f Tbc(eigTbc);

  node = fSettings["InsertKFsWhenLost"];
  mInsertKFsLost = true;
  if (!node.empty() && node.isInt()) {
    mInsertKFsLost = (bool)node.operator int();
  }

  if (!mInsertKFsLost)
    cerr << "Do not insert keyframes when lost visual tracking " << endl;

  float Ng, Na, Ngw, Naw;

  node = fSettings["IMU.Frequency"];
  if (!node.empty() && node.isInt()) {
    mImuFreq = node.operator int();
    mImuPer = 0.001; // 1.0 / (double) mImuFreq;
  } else {
    cerr << "*IMU.Frequency parameter doesn't exist or is not an integer*"
         << endl;
    b_miss_params = true;
  }

  node = fSettings["IMU.NoiseGyro"];
  if (!node.empty() && node.isReal()) {
    Ng = node.real();
  } else {
    cerr << "*IMU.NoiseGyro parameter doesn't exist or is not a real number*"
         << endl;
    b_miss_params = true;
  }

  node = fSettings["IMU.NoiseAcc"];
  if (!node.empty() && node.isReal()) {
    Na = node.real();
  } else {
    cerr << "*IMU.NoiseAcc parameter doesn't exist or is not a real number*"
         << endl;
    b_miss_params = true;
  }

  node = fSettings["IMU.GyroWalk"];
  if (!node.empty() && node.isReal()) {
    Ngw = node.real();
  } else {
    cerr << "*IMU.GyroWalk parameter doesn't exist or is not a real number*"
         << endl;
    b_miss_params = true;
  }

  node = fSettings["IMU.AccWalk"];
  if (!node.empty() && node.isReal()) {
    Naw = node.real();
  } else {
    cerr << "*IMU.AccWalk parameter doesn't exist or is not a real number*"
         << endl;
    b_miss_params = true;
  }

  node = fSettings["IMU.fastInit"];
  mFastInit = false;
  if (!node.empty()) {
    mFastInit = static_cast<int>(fSettings["IMU.fastInit"]) != 0;
  }

  if (mFastInit)
    cerr << "Fast IMU initialization. Acceleration is not checked \n";

  if (b_miss_params) {
    return false;
  }

  const float sf = sqrt(mImuFreq);
  cerr << endl;
  cerr << "IMU frequency: " << mImuFreq << " Hz" << endl;
  cerr << "IMU gyro noise: " << Ng << " rad/s/sqrt(Hz)" << endl;
  cerr << "IMU gyro walk: " << Ngw << " rad/s^2/sqrt(Hz)" << endl;
  cerr << "IMU accelerometer noise: " << Na << " m/s^2/sqrt(Hz)" << endl;
  cerr << "IMU accelerometer walk: " << Naw << " m/s^3/sqrt(Hz)" << endl;

  mpImuCalib = new IMU::Calib(Tbc, Ng * sf, Na * sf, Ngw / sf, Naw / sf);

  mpImuPreintegratedFromLastKF =
      new IMU::Preintegrated(IMU::Bias(), *mpImuCalib);

  return true;
}

void Tracking::SetLocalMapper(LocalMapping *pLocalMapper) {
  mpLocalMapper = pLocalMapper;
}

void Tracking::SetLoopClosing(LoopClosing *pLoopClosing) {
  mpLoopClosing = pLoopClosing;
}

void Tracking::SetViewer(Viewer *pViewer) { mpViewer = pViewer; }

void Tracking::SetStepByStep(bool bSet) { bStepByStep = bSet; }

bool Tracking::GetStepByStep() { return bStepByStep; }

Sophus::SE3f Tracking::GrabImageStereo(const cv::Mat &imRectLeft,
                                       const cv::Mat &imRectRight,
                                       const double &timestamp,
                                       string filename) {
  // cerr << "GrabImageStereo" << endl;

  mImGray = imRectLeft;
  cv::Mat imGrayRight = imRectRight;
  mImRight = imRectRight;

  if (mImGray.channels() == 3) {
    // cerr << "Image with 3 channels" << endl;
    if (mbRGB) {
      cvtColor(mImGray, mImGray, cv::COLOR_RGB2GRAY);
      cvtColor(imGrayRight, imGrayRight, cv::COLOR_RGB2GRAY);
    } else {
      cvtColor(mImGray, mImGray, cv::COLOR_BGR2GRAY);
      cvtColor(imGrayRight, imGrayRight, cv::COLOR_BGR2GRAY);
    }
  } else if (mImGray.channels() == 4) {
    // cerr << "Image with 4 channels" << endl;
    if (mbRGB) {
      cvtColor(mImGray, mImGray, cv::COLOR_RGBA2GRAY);
      cvtColor(imGrayRight, imGrayRight, cv::COLOR_RGBA2GRAY);
    } else {
      cvtColor(mImGray, mImGray, cv::COLOR_BGRA2GRAY);
      cvtColor(imGrayRight, imGrayRight, cv::COLOR_BGRA2GRAY);
    }
  }

  // cerr << "Incoming frame creation" << endl;

  if (sensor_type == SensorType::STEREO && !mpCamera2)
    mCurrentFrame = Frame(mImGray, imGrayRight, timestamp, mpORBextractorLeft,
                          mpORBextractorRight, mpORBVocabulary, mK, mDistCoef,
                          mbf, mThDepth, mpCamera);
  else if (sensor_type == SensorType::STEREO && mpCamera2)
    mCurrentFrame = Frame(mImGray, imGrayRight, timestamp, mpORBextractorLeft,
                          mpORBextractorRight, mpORBVocabulary, mK, mDistCoef,
                          mbf, mThDepth, mpCamera, mpCamera2, mTlr);
  else if (sensor_type == SensorType::IMU_STEREO && !mpCamera2)
    mCurrentFrame = Frame(mImGray, imGrayRight, timestamp, mpORBextractorLeft,
                          mpORBextractorRight, mpORBVocabulary, mK, mDistCoef,
                          mbf, mThDepth, mpCamera, &mLastFrame, *mpImuCalib);
  else if (sensor_type == SensorType::IMU_STEREO && mpCamera2)
    mCurrentFrame =
        Frame(mImGray, imGrayRight, timestamp, mpORBextractorLeft,
              mpORBextractorRight, mpORBVocabulary, mK, mDistCoef, mbf,
              mThDepth, mpCamera, mpCamera2, mTlr, &mLastFrame, *mpImuCalib);

  // cerr << "Incoming frame ended" << endl;

  mCurrentFrame.mNameFile = filename;
  mCurrentFrame.mnDataset = mnNumDataset;

#ifdef REGISTER_TIMES
  vdORBExtract_ms.push_back(mCurrentFrame.mTimeORB_Ext);
  vdStereoMatch_ms.push_back(mCurrentFrame.mTimeStereoMatch);
#endif

  // cerr << "Tracking start" << endl;
  Track();
  // cerr << "Tracking end" << endl;

  return mCurrentFrame.GetPose();
}

Sophus::SE3f Tracking::GrabImageRGBD(const cv::Mat &imRGB, const cv::Mat &imD,
                                     const double &timestamp, string filename) {
  mImGray = imRGB;
  cv::Mat imDepth = imD;

  if (mImGray.channels() == 3) {
    if (mbRGB)
      cvtColor(mImGray, mImGray, cv::COLOR_RGB2GRAY);
    else
      cvtColor(mImGray, mImGray, cv::COLOR_BGR2GRAY);
  } else if (mImGray.channels() == 4) {
    if (mbRGB)
      cvtColor(mImGray, mImGray, cv::COLOR_RGBA2GRAY);
    else
      cvtColor(mImGray, mImGray, cv::COLOR_BGRA2GRAY);
  }

  if ((fabs(mDepthMapFactor - 1.0f) > 1e-5) || imDepth.type() != CV_32F)
    imDepth.convertTo(imDepth, CV_32F, mDepthMapFactor);

  if (sensor_type == SensorType::RGB_D)
    mCurrentFrame =
        Frame(mImGray, imDepth, timestamp, mpORBextractorLeft, mpORBVocabulary,
              mK, mDistCoef, mbf, mThDepth, mpCamera);
  else if (sensor_type == SensorType::IMU_RGB_D)
    mCurrentFrame =
        Frame(mImGray, imDepth, timestamp, mpORBextractorLeft, mpORBVocabulary,
              mK, mDistCoef, mbf, mThDepth, mpCamera, &mLastFrame, *mpImuCalib);

  mCurrentFrame.mNameFile = filename;
  mCurrentFrame.mnDataset = mnNumDataset;

#ifdef REGISTER_TIMES
  vdORBExtract_ms.push_back(mCurrentFrame.mTimeORB_Ext);
#endif

  Track();

  return mCurrentFrame.GetPose();
}

Sophus::SE3f Tracking::GrabImageMonocular(const cv::Mat &im,
                                          const double &timestamp,
                                          string filename) {
  mImGray = im;
  if (mImGray.channels() == 3) {
    if (mbRGB)
      cvtColor(mImGray, mImGray, cv::COLOR_RGB2GRAY);
    else
      cvtColor(mImGray, mImGray, cv::COLOR_BGR2GRAY);
  } else if (mImGray.channels() == 4) {
    if (mbRGB)
      cvtColor(mImGray, mImGray, cv::COLOR_RGBA2GRAY);
    else
      cvtColor(mImGray, mImGray, cv::COLOR_BGRA2GRAY);
  }

  if (sensor_type == SensorType::MONOCULAR) {
    if (mState == NOT_INITIALIZED || mState == NO_IMAGES_YET ||
        (lastID - initID) < mMaxFrames)
      mCurrentFrame =
          Frame(mImGray, timestamp, mpIniORBextractor, mpORBVocabulary,
                mpCamera, mDistCoef, mbf, mThDepth);
    else
      mCurrentFrame =
          Frame(mImGray, timestamp, mpORBextractorLeft, mpORBVocabulary,
                mpCamera, mDistCoef, mbf, mThDepth);
  } else if (sensor_type == SensorType::IMU_MONOCULAR) {
    if (mState == NOT_INITIALIZED || mState == NO_IMAGES_YET) {
      mCurrentFrame =
          Frame(mImGray, timestamp, mpIniORBextractor, mpORBVocabulary,
                mpCamera, mDistCoef, mbf, mThDepth, &mLastFrame, *mpImuCalib);
    } else
      mCurrentFrame =
          Frame(mImGray, timestamp, mpORBextractorLeft, mpORBVocabulary,
                mpCamera, mDistCoef, mbf, mThDepth, &mLastFrame, *mpImuCalib);
  }

  if (mState == NO_IMAGES_YET)
    t0 = timestamp;

  mCurrentFrame.mNameFile = filename;
  mCurrentFrame.mnDataset = mnNumDataset;

#ifdef REGISTER_TIMES
  vdORBExtract_ms.push_back(mCurrentFrame.mTimeORB_Ext);
#endif

  lastID = mCurrentFrame.mnId;
  Track();

  return mCurrentFrame.GetPose();
}

void Tracking::GrabImuData(const IMU::Point &imuMeasurement) {
  unique_lock<mutex> lock(mMutexImuQueue);
  mlQueueImuData.push_back(imuMeasurement);
}

void Tracking::PreintegrateIMU() {

  if (!mCurrentFrame.mpPrevFrame) {
    Verbose::Log("non prev frame ", Verbose::VERBOSITY_NORMAL);
    mCurrentFrame.setIntegrated();
    return;
  }

  mvImuFromLastFrame.clear();
  mvImuFromLastFrame.reserve(mlQueueImuData.size());
  if (mlQueueImuData.size() == 0) {
    DEBUG_MSG("No IMU data recorded for current frame\n");
    mCurrentFrame.setIntegrated();
    return;
  }

  while (true) {
    bool bSleep = false;
    {
      unique_lock<mutex> lock(mMutexImuQueue);
      if (!mlQueueImuData.empty()) {
        IMU::Point *m = &mlQueueImuData.front();
        cout.precision(17);
        if (m->t < mCurrentFrame.mpPrevFrame->mTimeStamp - mImuPer) {
          mlQueueImuData.pop_front();
        } else if (m->t < mCurrentFrame.mTimeStamp - mImuPer) {
          mvImuFromLastFrame.push_back(*m);
          mlQueueImuData.pop_front();
        } else {
          mvImuFromLastFrame.push_back(*m);
          break;
        }
      } else {
        break;
        bSleep = true;
      }
    }
    if (bSleep)
      usleep(500);
  }

  const int n = mvImuFromLastFrame.size() - 1;
  if (n == 0) {
    DEBUG_MSG("No IMU measurements\n");
    return;
  }

  IMU::Preintegrated *pImuPreintegratedFromLastFrame =
      new IMU::Preintegrated(mLastFrame.mImuBias, mCurrentFrame.mImuCalib);

  for (int i = 0; i < n; i++) {
    float tstep;
    Eigen::Vector3f acc, angVel;
    if ((i == 0) && (i < (n - 1))) {
      float tab = mvImuFromLastFrame[i + 1].t - mvImuFromLastFrame[i].t;
      float tini =
          mvImuFromLastFrame[i].t - mCurrentFrame.mpPrevFrame->mTimeStamp;
      acc = (mvImuFromLastFrame[i].a + mvImuFromLastFrame[i + 1].a -
             (mvImuFromLastFrame[i + 1].a - mvImuFromLastFrame[i].a) *
                 (tini / tab)) *
            0.5f;
      angVel = (mvImuFromLastFrame[i].w + mvImuFromLastFrame[i + 1].w -
                (mvImuFromLastFrame[i + 1].w - mvImuFromLastFrame[i].w) *
                    (tini / tab)) *
               0.5f;
      tstep =
          mvImuFromLastFrame[i + 1].t - mCurrentFrame.mpPrevFrame->mTimeStamp;
    } else if (i < (n - 1)) {
      acc = (mvImuFromLastFrame[i].a + mvImuFromLastFrame[i + 1].a) * 0.5f;
      angVel = (mvImuFromLastFrame[i].w + mvImuFromLastFrame[i + 1].w) * 0.5f;
      tstep = mvImuFromLastFrame[i + 1].t - mvImuFromLastFrame[i].t;
    } else if ((i > 0) && (i == (n - 1))) {
      float tab = mvImuFromLastFrame[i + 1].t - mvImuFromLastFrame[i].t;
      float tend = mvImuFromLastFrame[i + 1].t - mCurrentFrame.mTimeStamp;
      acc = (mvImuFromLastFrame[i].a + mvImuFromLastFrame[i + 1].a -
             (mvImuFromLastFrame[i + 1].a - mvImuFromLastFrame[i].a) *
                 (tend / tab)) *
            0.5f;
      angVel = (mvImuFromLastFrame[i].w + mvImuFromLastFrame[i + 1].w -
                (mvImuFromLastFrame[i + 1].w - mvImuFromLastFrame[i].w) *
                    (tend / tab)) *
               0.5f;
      tstep = mCurrentFrame.mTimeStamp - mvImuFromLastFrame[i].t;
    } else if ((i == 0) && (i == (n - 1))) {
      acc = mvImuFromLastFrame[i].a;
      angVel = mvImuFromLastFrame[i].w;
      tstep = mCurrentFrame.mTimeStamp - mCurrentFrame.mpPrevFrame->mTimeStamp;
    }

    if (!mpImuPreintegratedFromLastKF)
      DEBUG_MSG("mpImuPreintegratedFromLastKF does not exist\n");
    mpImuPreintegratedFromLastKF->IntegrateNewMeasurement(acc, angVel, tstep);
    pImuPreintegratedFromLastFrame->IntegrateNewMeasurement(acc, angVel, tstep);
  }

  mCurrentFrame.mpImuPreintegratedFrame = pImuPreintegratedFromLastFrame;
  mCurrentFrame.mpImuPreintegrated = mpImuPreintegratedFromLastKF;
  mCurrentFrame.mpLastKeyFrame = mpLastKeyFrame;

  mCurrentFrame.setIntegrated();
}

bool Tracking::PredictStateIMU() {
  if (!mCurrentFrame.mpPrevFrame) {
    DEBUG_MSG("No last frame\n");
    return false;
  }

  if (mbMapUpdated && mpLastKeyFrame) {
    const Eigen::Vector3f twb1 = mpLastKeyFrame->GetImuPosition();
    const Eigen::Matrix3f Rwb1 = mpLastKeyFrame->GetImuRotation();
    const Eigen::Vector3f Vwb1 = mpLastKeyFrame->GetVelocity();

    const Eigen::Vector3f Gz(0, 0, -IMU::GRAVITY_VALUE);
    const float t12 = mpImuPreintegratedFromLastKF->dT;

    Eigen::Matrix3f Rwb2 = IMU::NormalizeRotation(
        Rwb1 * mpImuPreintegratedFromLastKF->GetDeltaRotation(
                   mpLastKeyFrame->GetImuBias()));
    Eigen::Vector3f twb2 =
        twb1 + Vwb1 * t12 + 0.5f * t12 * t12 * Gz +
        Rwb1 * mpImuPreintegratedFromLastKF->GetDeltaPosition(
                   mpLastKeyFrame->GetImuBias());
    Eigen::Vector3f Vwb2 =
        Vwb1 + t12 * Gz +
        Rwb1 * mpImuPreintegratedFromLastKF->GetDeltaVelocity(
                   mpLastKeyFrame->GetImuBias());
    mCurrentFrame.SetImuPoseVelocity(Rwb2, twb2, Vwb2);

    mCurrentFrame.mImuBias = mpLastKeyFrame->GetImuBias();
    mCurrentFrame.mPredBias = mCurrentFrame.mImuBias;
    return true;
  } else if (!mbMapUpdated) {
    const Eigen::Vector3f twb1 = mLastFrame.GetImuPosition();
    const Eigen::Matrix3f Rwb1 = mLastFrame.GetImuRotation();
    const Eigen::Vector3f Vwb1 = mLastFrame.GetVelocity();
    const Eigen::Vector3f Gz(0, 0, -IMU::GRAVITY_VALUE);
    const float t12 = mCurrentFrame.mpImuPreintegratedFrame->dT;

    Eigen::Matrix3f Rwb2 = IMU::NormalizeRotation(
        Rwb1 * mCurrentFrame.mpImuPreintegratedFrame->GetDeltaRotation(
                   mLastFrame.mImuBias));
    Eigen::Vector3f twb2 =
        twb1 + Vwb1 * t12 + 0.5f * t12 * t12 * Gz +
        Rwb1 * mCurrentFrame.mpImuPreintegratedFrame->GetDeltaPosition(
                   mLastFrame.mImuBias);
    Eigen::Vector3f Vwb2 =
        Vwb1 + t12 * Gz +
        Rwb1 * mCurrentFrame.mpImuPreintegratedFrame->GetDeltaVelocity(
                   mLastFrame.mImuBias);

    mCurrentFrame.SetImuPoseVelocity(Rwb2, twb2, Vwb2);

    mCurrentFrame.mImuBias = mLastFrame.mImuBias;
    mCurrentFrame.mPredBias = mCurrentFrame.mImuBias;
    return true;
  } else
    cerr << "not IMU prediction!!" << endl;

  return false;
}

void Tracking::ResetFrameIMU() {
  // TODO To implement...
}

void Tracking::Track() {

  if (bStepByStep) {
    cerr << "Tracking: Waiting to the next step" << endl;
    while (!mbStep && bStepByStep)
      usleep(500);
    mbStep = false;
  }

  if (mpLocalMapper->mbBadImu) {
    cerr << "TRACK: Reset map because local mapper set the bad imu flag "
         << endl;
    mpSystem->ResetActiveMap();
    return;
  }

  Map *pCurrentMap = mpAtlas->GetCurrentMap();
  if (!pCurrentMap) {
    cerr << "ERROR: There is not an active map in the atlas" << endl;
  }

  if (mState != NO_IMAGES_YET) {
    if (mLastFrame.mTimeStamp > mCurrentFrame.mTimeStamp) {
      cerr
          << "ERROR: Frame with a timestamp older than previous frame detected!"
          << endl;
      unique_lock<mutex> lock(mMutexImuQueue);
      mlQueueImuData.clear();
      CreateMapInAtlas();
      return;
    } else if (mCurrentFrame.mTimeStamp > mLastFrame.mTimeStamp + 1.0) {
      // cerr << mCurrentFrame.mTimeStamp << ", " << mLastFrame.mTimeStamp <<
      // endl; cerr << "id last: " << mLastFrame.mnId << "    id curr: " <<
      // mCurrentFrame.mnId << endl;
      if (mpAtlas->isInertial()) {

        if (mpAtlas->isImuInitialized()) {
          cerr << "Timestamp jump detected. State set to LOST. Reseting IMU "
                  "integration..."
               << endl;
          if (!pCurrentMap->GetIniertialBA2()) {
            mpSystem->ResetActiveMap();
          } else {
            CreateMapInAtlas();
          }
        } else {
          cerr << "Timestamp jump detected, before IMU initialization. "
                  "Reseting..."
               << endl;
          mpSystem->ResetActiveMap();
        }
        return;
      }
    }
  }

  if ((sensor_type & SensorType::USE_IMU) && mpLastKeyFrame)
    mCurrentFrame.SetNewBias(mpLastKeyFrame->GetImuBias());

  if (mState == NO_IMAGES_YET) {
    mState = NOT_INITIALIZED;
  }

  mLastProcessedState = mState;

  if ((sensor_type & SensorType::USE_IMU) && !mbCreatedMap) {
#ifdef REGISTER_TIMES
    chrono::steady_clock::time_point time_StartPreIMU =
        chrono::steady_clock::now();
#endif
    PreintegrateIMU();
#ifdef REGISTER_TIMES
    chrono::steady_clock::time_point time_EndPreIMU =
        chrono::steady_clock::now();

    double timePreImu = chrono::duration_cast<chrono::duration<double, milli>>(
                            time_EndPreIMU - time_StartPreIMU)
                            .count();
    vdIMUInteg_ms.push_back(timePreImu);
#endif
  }
  mbCreatedMap = false;

  // Get Map Mutex -> Map cannot be changed
  unique_lock<mutex> lock(pCurrentMap->mMutexMapUpdate);

  mbMapUpdated = false;

  int nCurMapChangeIndex = pCurrentMap->GetMapChangeIndex();
  int nMapChangeIndex = pCurrentMap->GetLastMapChange();
  if (nCurMapChangeIndex > nMapChangeIndex) {
    pCurrentMap->SetLastMapChange(nCurMapChangeIndex);
    mbMapUpdated = true;
  }

  if (mState == NOT_INITIALIZED) {
    if ((sensor_type & SensorType::CAMERA_MASK) == SensorType::STEREO ||
        (sensor_type & SensorType::CAMERA_MASK) == SensorType::RGB_D) {
      InitializeStereo();
    } else {
      InitializeMonocular();
    }

    mpFrameDrawer->Update(this);

    // Check if initialization was successful
    if (mState != OK) {
      mLastFrame = Frame(mCurrentFrame);
      return;
    }

    if (mpAtlas->GetAllMaps().size() == 1) {
      mnFirstFrameId = mCurrentFrame.mnId;
    }
  } else {
    // System is initialized. Track Frame.
    bool bOK;

#ifdef REGISTER_TIMES
    chrono::steady_clock::time_point time_StartPosePred =
        chrono::steady_clock::now();
#endif

    // Initial camera pose estimation using motion model or relocalization (if
    // tracking is lost)
    if (!mbOnlyTracking) {

      // State OK
      // Local Mapping is activated. This is the normal behaviour, unless
      // you explicitly activate the "only tracking" mode.
      if (mState == OK) {

        // Local Mapping might have changed some MapPoints tracked in last frame
        CheckReplacedInLastFrame();

        if ((!mbVelocity && !pCurrentMap->isImuInitialized()) ||
            mCurrentFrame.mnId < mnLastRelocFrameId + 2) {
          Verbose::Log("TRACK: Track with respect to the reference KF ",
                       Verbose::VERBOSITY_DEBUG);
          bOK = TrackReferenceKeyFrame();
        } else {
          Verbose::Log("TRACK: Track with motion model",
                       Verbose::VERBOSITY_DEBUG);
          bOK = TrackWithMotionModel();
          if (!bOK)
            bOK = TrackReferenceKeyFrame();
        }

        if (!bOK) {
          if (mCurrentFrame.mnId <= (mnLastRelocFrameId + mnFramesToResetIMU) &&
              (sensor_type & SensorType::USE_IMU)) {
            mState = LOST;
          } else if (pCurrentMap->KeyFramesInMap() > 10) {
            // cerr << "KF in map: " << pCurrentMap->KeyFramesInMap() << endl;
            mState = RECENTLY_LOST;
            mTimeStampLost = mCurrentFrame.mTimeStamp;
          } else {
            mState = LOST;
          }
        }
      } else {

        if (mState == RECENTLY_LOST) {
          Verbose::Log("Lost for a short time", Verbose::VERBOSITY_NORMAL);

          bOK = true;
          if ((sensor_type & SensorType::USE_IMU)) {
            if (pCurrentMap->isImuInitialized())
              PredictStateIMU();
            else
              bOK = false;

            if (mCurrentFrame.mTimeStamp - mTimeStampLost >
                time_recently_lost) {
              mState = LOST;
              Verbose::Log("Track Lost...", Verbose::VERBOSITY_NORMAL);
              bOK = false;
            }
          } else {
            // Relocalization
            bOK = Relocalization();
            // cerr << "mCurrentFrame.mTimeStamp:" <<
            // to_string(mCurrentFrame.mTimeStamp) << endl; cerr <<
            // "mTimeStampLost:" << to_string(mTimeStampLost) << endl;
            if (mCurrentFrame.mTimeStamp - mTimeStampLost > 3.0f && !bOK) {
              mState = LOST;
              Verbose::Log("Track Lost...", Verbose::VERBOSITY_NORMAL);
              bOK = false;
            }
          }
        } else if (mState == LOST) {

          Verbose::Log("A new map is started...", Verbose::VERBOSITY_NORMAL);

          if (pCurrentMap->KeyFramesInMap() < 10) {
            mpSystem->ResetActiveMap();
            Verbose::Log("Reseting current map...", Verbose::VERBOSITY_NORMAL);
          } else
            CreateMapInAtlas();

          if (mpLastKeyFrame)
            mpLastKeyFrame = static_cast<KeyFrame *>(NULL);

          Verbose::Log("done", Verbose::VERBOSITY_NORMAL);

          return;
        }
      }

    } else {
      // Localization Mode: Local Mapping is deactivated (TODO Not available in
      // inertial mode)
      if (mState == LOST) {
        if (sensor_type & SensorType::USE_IMU)
          Verbose::Log("IMU. State LOST", Verbose::VERBOSITY_NORMAL);
        bOK = Relocalization();
      } else {
        if (!mbVO) {
          // In last frame we tracked enough MapPoints in the map
          if (mbVelocity) {
            bOK = TrackWithMotionModel();
          } else {
            bOK = TrackReferenceKeyFrame();
          }
        } else {
          // In last frame we tracked mainly "visual odometry" points.

          // We compute two camera poses, one from motion model and one doing
          // relocalization. If relocalization is sucessfull we choose that
          // solution, otherwise we retain the "visual odometry" solution.

          bool bOKMM = false;
          bool bOKReloc = false;
          vector<MapPoint *> vpMPsMM;
          vector<bool> vbOutMM;
          Sophus::SE3f TcwMM;
          if (mbVelocity) {
            bOKMM = TrackWithMotionModel();
            vpMPsMM = mCurrentFrame.mvpMapPoints;
            vbOutMM = mCurrentFrame.mvbOutlier;
            TcwMM = mCurrentFrame.GetPose();
          }
          bOKReloc = Relocalization();

          if (bOKMM && !bOKReloc) {
            mCurrentFrame.SetPose(TcwMM);
            mCurrentFrame.mvpMapPoints = vpMPsMM;
            mCurrentFrame.mvbOutlier = vbOutMM;

            if (mbVO) {
              for (int i = 0; i < mCurrentFrame.N; i++) {
                if (mCurrentFrame.mvpMapPoints[i] &&
                    !mCurrentFrame.mvbOutlier[i]) {
                  mCurrentFrame.mvpMapPoints[i]->IncreaseFound();
                }
              }
            }
          } else if (bOKReloc) {
            mbVO = false;
          }

          bOK = bOKReloc || bOKMM;
        }
      }
    }

    if (!mCurrentFrame.mpReferenceKF)
      mCurrentFrame.mpReferenceKF = mpReferenceKF;

#ifdef REGISTER_TIMES
    chrono::steady_clock::time_point time_EndPosePred =
        chrono::steady_clock::now();

    double timePosePred =
        chrono::duration_cast<chrono::duration<double, milli>>(
            time_EndPosePred - time_StartPosePred)
            .count();
    vdPosePred_ms.push_back(timePosePred);
#endif

#ifdef REGISTER_TIMES
    chrono::steady_clock::time_point time_StartLMTrack =
        chrono::steady_clock::now();
#endif
    // If we have an initial estimation of the camera pose and matching. Track
    // the local map.
    if (!mbOnlyTracking) {
      if (bOK) {
        bOK = TrackLocalMap();
      }
      if (!bOK)
        cerr << "Fail to track local map!" << endl;
    } else {
      // mbVO true means that there are few matches to MapPoints in the map. We
      // cannot retrieve a local map and therefore we do not perform
      // TrackLocalMap(). Once the system relocalizes the camera we will use the
      // local map again.
      if (bOK && !mbVO)
        bOK = TrackLocalMap();
    }

    if (bOK)
      mState = OK;
    else if (mState == OK) {
      if (sensor_type & SensorType::USE_IMU) {
        Verbose::Log("Track lost for less than one second...",
                     Verbose::VERBOSITY_NORMAL);
        if (!pCurrentMap->isImuInitialized() ||
            !pCurrentMap->GetIniertialBA2()) {
          cerr << "IMU is not or recently initialized. Reseting active map..."
               << endl;
          mpSystem->ResetActiveMap();
        }

        mState = RECENTLY_LOST;
      } else
        mState = RECENTLY_LOST; // visual to lost

      /*if(mCurrentFrame.mnId>mnLastRelocFrameId+mMaxFrames)
      {*/
      mTimeStampLost = mCurrentFrame.mTimeStamp;
      //}
    }

    // Save frame if recent relocalization, since they are used for IMU reset
    // (as we are making copy, it shluld be once mCurrFrame is completely
    // modified)
    if ((mCurrentFrame.mnId < (mnLastRelocFrameId + mnFramesToResetIMU)) &&
        (mCurrentFrame.mnId > mnFramesToResetIMU) &&
        (sensor_type & SensorType::USE_IMU) &&
        pCurrentMap->isImuInitialized()) {
      // TODO check this situation
      Verbose::Log("Saving pointer to frame. imu needs reset...",
                   Verbose::VERBOSITY_NORMAL);
      Frame *pF = new Frame(mCurrentFrame);
      pF->mpPrevFrame = new Frame(mLastFrame);

      // Load preintegration
      pF->mpImuPreintegratedFrame =
          new IMU::Preintegrated(mCurrentFrame.mpImuPreintegratedFrame);
    }

    if (pCurrentMap->isImuInitialized()) {
      if (bOK) {
        if (mCurrentFrame.mnId == (mnLastRelocFrameId + mnFramesToResetIMU)) {
          cerr << "RESETING FRAME!!!" << endl;
          ResetFrameIMU();
        } else if (mCurrentFrame.mnId > (mnLastRelocFrameId + 30))
          mLastBias = mCurrentFrame.mImuBias;
      }
    }

#ifdef REGISTER_TIMES
    chrono::steady_clock::time_point time_EndLMTrack =
        chrono::steady_clock::now();

    double timeLMTrack = chrono::duration_cast<chrono::duration<double, milli>>(
                             time_EndLMTrack - time_StartLMTrack)
                             .count();
    vdLMTrack_ms.push_back(timeLMTrack);
#endif

    // Update drawer
    mpFrameDrawer->Update(this);
    if (mCurrentFrame.isSet())
      mpMapDrawer->SetCurrentCameraPose(mCurrentFrame.GetPose());

    if (bOK || mState == RECENTLY_LOST) {
      // Update motion model
      if (mLastFrame.isSet() && mCurrentFrame.isSet()) {
        Sophus::SE3f LastTwc = mLastFrame.GetPose().inverse();
        mVelocity = mCurrentFrame.GetPose() * LastTwc;
        mbVelocity = true;
      } else {
        mbVelocity = false;
      }

      if (sensor_type & SensorType::USE_IMU)
        mpMapDrawer->SetCurrentCameraPose(mCurrentFrame.GetPose());

      // Clean VO matches
      for (int i = 0; i < mCurrentFrame.N; i++) {
        MapPoint *pMP = mCurrentFrame.mvpMapPoints[i];
        if (pMP)
          if (pMP->Observations() < 1) {
            mCurrentFrame.mvbOutlier[i] = false;
            mCurrentFrame.mvpMapPoints[i] = static_cast<MapPoint *>(NULL);
          }
      }

      // Delete temporal MapPoints
      for (list<MapPoint *>::iterator lit = mlpTemporalPoints.begin(),
                                      lend = mlpTemporalPoints.end();
           lit != lend; lit++) {
        MapPoint *pMP = *lit;
        delete pMP;
      }
      mlpTemporalPoints.clear();

#ifdef REGISTER_TIMES
      chrono::steady_clock::time_point time_StartNewKF =
          chrono::steady_clock::now();
#endif
      bool bNeedKF = NeedNewKeyFrame();

      // Check if we need to insert a new keyframe
      // if(bNeedKF && bOK)
      if (bNeedKF && (bOK || (mInsertKFsLost && mState == RECENTLY_LOST &&
                              (sensor_type & SensorType::USE_IMU))))
        CreateNewKeyFrame();

#ifdef REGISTER_TIMES
      chrono::steady_clock::time_point time_EndNewKF =
          chrono::steady_clock::now();

      double timeNewKF = chrono::duration_cast<chrono::duration<double, milli>>(
                             time_EndNewKF - time_StartNewKF)
                             .count();
      vdNewKF_ms.push_back(timeNewKF);
#endif

      // We allow points with high innovation (considererd outliers by the Huber
      // Function) pass to the new keyframe, so that bundle adjustment will
      // finally decide if they are outliers or not. We don't want next frame to
      // estimate its position with those points so we discard them in the
      // frame. Only has effect if lastframe is tracked
      for (int i = 0; i < mCurrentFrame.N; i++) {
        if (mCurrentFrame.mvpMapPoints[i] && mCurrentFrame.mvbOutlier[i])
          mCurrentFrame.mvpMapPoints[i] = static_cast<MapPoint *>(NULL);
      }
    }

    // Reset if the camera get lost soon after initialization
    if (mState == LOST) {
      if (pCurrentMap->KeyFramesInMap() <= 10) {
        mpSystem->ResetActiveMap();
        return;
      }
      if (sensor_type & SensorType::USE_IMU)
        if (!pCurrentMap->isImuInitialized()) {
          Verbose::Log("Track lost before IMU initialisation, reseting...",
                       Verbose::VERBOSITY_QUIET);
          mpSystem->ResetActiveMap();
          return;
        }

      CreateMapInAtlas();

      return;
    }

    if (!mCurrentFrame.mpReferenceKF)
      mCurrentFrame.mpReferenceKF = mpReferenceKF;

    mLastFrame = Frame(mCurrentFrame);
  }

  if (mState == OK || mState == RECENTLY_LOST) {
    // Store frame pose information to retrieve the complete camera trajectory
    // afterwards.
    if (mCurrentFrame.isSet()) {
      Sophus::SE3f Tcr_ = mCurrentFrame.GetPose() *
                          mCurrentFrame.mpReferenceKF->GetPoseInverse();
      mlRelativeFramePoses.push_back(Tcr_);
      mlpReferences.push_back(mCurrentFrame.mpReferenceKF);
      mlFrameTimes.push_back(mCurrentFrame.mTimeStamp);
      mlbLost.push_back(mState == LOST);
    } else {
      // This can happen if tracking is lost
      mlRelativeFramePoses.push_back(mlRelativeFramePoses.back());
      mlpReferences.push_back(mlpReferences.back());
      mlFrameTimes.push_back(mlFrameTimes.back());
      mlbLost.push_back(mState == LOST);
    }
  }

#ifdef REGISTER_LOOP
  if (Stop()) {

    // Safe area to stop
    while (isStopped()) {
      usleep(3000);
    }
  }
#endif
}

void Tracking::InitializeStereo() {
  if (mCurrentFrame.N > 500) {
    if (sensor_type == SensorType::IMU_STEREO ||
        sensor_type == SensorType::IMU_RGB_D) {
      if (!mCurrentFrame.mpImuPreintegrated || !mLastFrame.mpImuPreintegrated) {
        cerr << "not IMU meas" << endl;
        return;
      }
      if (!mFastInit && (mCurrentFrame.mpImuPreintegratedFrame->avgA -
                         mLastFrame.mpImuPreintegratedFrame->avgA)
                                .norm() < 0.5) {
        cerr << "not enough acceleration" << endl;
        return;
      }
      if (mpImuPreintegratedFromLastKF)
        delete mpImuPreintegratedFromLastKF;
      mpImuPreintegratedFromLastKF =
          new IMU::Preintegrated(IMU::Bias(), *mpImuCalib);
      mCurrentFrame.mpImuPreintegrated = mpImuPreintegratedFromLastKF;
      Eigen::Matrix3f Rwb0 = mCurrentFrame.mImuCalib.mTcb.rotationMatrix();
      Eigen::Vector3f twb0 = mCurrentFrame.mImuCalib.mTcb.translation();
      Eigen::Vector3f Vwb0;
      Vwb0.setZero();
      mCurrentFrame.SetImuPoseVelocity(Rwb0, twb0, Vwb0);
    } else {
      mCurrentFrame.SetPose(Sophus::SE3f());
    }

    // Create KeyFrame
    KeyFrame *pKFini =
        new KeyFrame(mCurrentFrame, mpAtlas->GetCurrentMap(), mpKeyFrameDB);

    // Insert KeyFrame in the map
    mpAtlas->AddKeyFrame(pKFini);

    // Create MapPoints and asscoiate to KeyFrame
    if (!mpCamera2) {
      for (int i = 0; i < mCurrentFrame.N; i++) {
        float z = mCurrentFrame.mvDepth[i];
        if (z > 0) {
          Eigen::Vector3f x3D;
          mCurrentFrame.UnprojectStereo(i, x3D);
          MapPoint *pNewMP =
              new MapPoint(x3D, pKFini, mpAtlas->GetCurrentMap());
          pNewMP->AddObservation(pKFini, i);
          pKFini->AddMapPoint(pNewMP, i);
          pNewMP->ComputeDistinctiveDescriptors();
          pNewMP->UpdateNormalAndDepth();
          mpAtlas->AddMapPoint(pNewMP);

          mCurrentFrame.mvpMapPoints[i] = pNewMP;
        }
      }
    } else {
      for (int i = 0; i < mCurrentFrame.Nleft; i++) {
        int rightIndex = mCurrentFrame.mvLeftToRightMatch[i];
        if (rightIndex != -1) {
          Eigen::Vector3f x3D = mCurrentFrame.mvStereo3Dpoints[i];

          MapPoint *pNewMP =
              new MapPoint(x3D, pKFini, mpAtlas->GetCurrentMap());

          pNewMP->AddObservation(pKFini, i);
          pNewMP->AddObservation(pKFini, rightIndex + mCurrentFrame.Nleft);

          pKFini->AddMapPoint(pNewMP, i);
          pKFini->AddMapPoint(pNewMP, rightIndex + mCurrentFrame.Nleft);

          pNewMP->ComputeDistinctiveDescriptors();
          pNewMP->UpdateNormalAndDepth();
          mpAtlas->AddMapPoint(pNewMP);

          mCurrentFrame.mvpMapPoints[i] = pNewMP;
          mCurrentFrame.mvpMapPoints[rightIndex + mCurrentFrame.Nleft] = pNewMP;
        }
      }
    }

    Verbose::Log("New Map created with " +
                     to_string(mpAtlas->MapPointsInMap()) + " points",
                 Verbose::VERBOSITY_QUIET);

    // cerr << "Active map: " << mpAtlas->GetCurrentMap()->GetId() << endl;

    mpLocalMapper->InsertKeyFrame(pKFini);

    mLastFrame = Frame(mCurrentFrame);
    mnLastKeyFrameId = mCurrentFrame.mnId;
    mpLastKeyFrame = pKFini;
    // mnLastRelocFrameId = mCurrentFrame.mnId;

    mvpLocalKeyFrames.push_back(pKFini);
    mvpLocalMapPoints = mpAtlas->GetAllMapPoints();
    mpReferenceKF = pKFini;
    mCurrentFrame.mpReferenceKF = pKFini;

    mpAtlas->SetReferenceMapPoints(mvpLocalMapPoints);

    mpAtlas->GetCurrentMap()->mvpKeyFrameOrigins.push_back(pKFini);

    mpMapDrawer->SetCurrentCameraPose(mCurrentFrame.GetPose());

    mState = OK;
  }
}

void Tracking::InitializeMonocular() {
  ssStateMsg.str(string());
  if (!mbReadyToInitialize) {
    // Set Reference Frame
    if (mCurrentFrame.mvKeys.size() > 100) {
      mInitialFrame = Frame(mCurrentFrame);
      mLastFrame = Frame(mCurrentFrame);
      mvbPrevMatched.resize(mCurrentFrame.mvKeysUn.size());
      for (size_t i = 0; i < mCurrentFrame.mvKeysUn.size(); i++)
        mvbPrevMatched[i] = mCurrentFrame.mvKeysUn[i].pt;

      fill(mvIniMatches.begin(), mvIniMatches.end(), -1);

      if (sensor_type == SensorType::IMU_MONOCULAR) {
        if (mpImuPreintegratedFromLastKF) {
          delete mpImuPreintegratedFromLastKF;
        }
        mpImuPreintegratedFromLastKF =
            new IMU::Preintegrated(IMU::Bias(), *mpImuCalib);
        mCurrentFrame.mpImuPreintegrated = mpImuPreintegratedFromLastKF;
      }
      mbReadyToInitialize = true;
      return;
    } else {
      ssStateMsg << "Too few features (A=" << mCurrentFrame.mvKeys.size()
                 << ")";
    }
  } else {
    if (((int)mCurrentFrame.mvKeys.size() <= 100) ||
        ((sensor_type == SensorType::IMU_MONOCULAR) &&
         (mLastFrame.mTimeStamp - mInitialFrame.mTimeStamp > 1.0))) {
      mbReadyToInitialize = false;
      ssStateMsg << "Too few features (B=" << mCurrentFrame.mvKeys.size()
                 << ")";
      return;
    }

    // Find correspondences
    ORBmatcher matcher(0.9, true);
    int nmatches = matcher.SearchForInitialization(
        mInitialFrame, mCurrentFrame, mvbPrevMatched, mvIniMatches, 100);

    // Check if there are enough correspondences
    if (nmatches < 100) {
      mbReadyToInitialize = false;
      ssStateMsg << "Too few matches (" << nmatches << ")";
      return;
    }

    Sophus::SE3f Tcw;
    vector<bool> vbTriangulated; // Triangulated Correspondences (mvIniMatches)

    if (mpCamera->ReconstructWithTwoViews(mInitialFrame.mvKeysUn,
                                          mCurrentFrame.mvKeysUn, mvIniMatches,
                                          Tcw, mvIniP3D, vbTriangulated)) {
      for (size_t i = 0, iend = mvIniMatches.size(); i < iend; i++) {
        if (mvIniMatches[i] >= 0 && !vbTriangulated[i]) {
          mvIniMatches[i] = -1;
          nmatches--;
        }
      }

      // Set Frame Poses
      mInitialFrame.SetPose(Sophus::SE3f());
      mCurrentFrame.SetPose(Tcw);

      CreateInitialMapMonocular();
    } else {
      ssStateMsg << "ReconstructWithTwoViews failed";
    }
  }
}

void Tracking::CreateInitialMapMonocular() {
  // Create KeyFrames
  KeyFrame *pKFini =
      new KeyFrame(mInitialFrame, mpAtlas->GetCurrentMap(), mpKeyFrameDB);
  KeyFrame *pKFcur =
      new KeyFrame(mCurrentFrame, mpAtlas->GetCurrentMap(), mpKeyFrameDB);

  if (sensor_type == SensorType::IMU_MONOCULAR)
    pKFini->mpImuPreintegrated = (IMU::Preintegrated *)(NULL);

  pKFini->ComputeBoW();
  pKFcur->ComputeBoW();

  // Insert KFs in the map
  mpAtlas->AddKeyFrame(pKFini);
  mpAtlas->AddKeyFrame(pKFcur);

  for (size_t i = 0; i < mvIniMatches.size(); i++) {
    if (mvIniMatches[i] < 0)
      continue;

    // Create MapPoint.
    Eigen::Vector3f worldPos;
    worldPos << mvIniP3D[i].x, mvIniP3D[i].y, mvIniP3D[i].z;
    auto pMP = new MapPoint(worldPos, pKFcur, mpAtlas->GetCurrentMap());

    pKFini->AddMapPoint(pMP, i);
    pKFcur->AddMapPoint(pMP, mvIniMatches[i]);

    pMP->AddObservation(pKFini, i);
    pMP->AddObservation(pKFcur, mvIniMatches[i]);

    pMP->ComputeDistinctiveDescriptors();
    pMP->UpdateNormalAndDepth();

    // Fill Current Frame structure
    mCurrentFrame.mvpMapPoints[mvIniMatches[i]] = pMP;
    mCurrentFrame.mvbOutlier[mvIniMatches[i]] = false;

    // Add to Map
    mpAtlas->AddMapPoint(pMP);
  }

  // Update Connections
  pKFini->UpdateConnections();
  pKFcur->UpdateConnections();

  set<MapPoint *> sMPs;
  sMPs = pKFini->GetMapPoints();

  // Bundle Adjustment
  Verbose::Log("New Map created with " + to_string(mpAtlas->MapPointsInMap()) +
                   " points",
               Verbose::VERBOSITY_QUIET);

  Optimizer::GlobalBundleAdjustment(mpAtlas->GetCurrentMap(), 20);
  Verbose::Log("Optimization complete", Verbose::VERBOSITY_QUIET);

  float medianDepth = pKFini->ComputeSceneMedianDepth(2);
  float invMedianDepth;
  if (sensor_type == SensorType::IMU_MONOCULAR)
    invMedianDepth = 4.0f / medianDepth; // 4.0f
  else
    invMedianDepth = 1.0f / medianDepth;

  if (medianDepth < 0 ||
      pKFcur->TrackedMapPoints(1) < 50) // TODO Check, originally 100 tracks
  {
    Verbose::Log("Wrong initialization, reseting...", Verbose::VERBOSITY_QUIET);
    mpSystem->ResetActiveMap();
    return;
  }

  // Scale initial baseline
  Sophus::SE3f Tc2w = pKFcur->GetPose();
  Tc2w.translation() *= invMedianDepth;
  pKFcur->SetPose(Tc2w);

  // Scale points
  vector<MapPoint *> vpAllMapPoints = pKFini->GetMapPointMatches();
  for (size_t iMP = 0; iMP < vpAllMapPoints.size(); iMP++) {
    if (vpAllMapPoints[iMP]) {
      MapPoint *pMP = vpAllMapPoints[iMP];
      pMP->SetWorldPos(pMP->GetWorldPos() * invMedianDepth);
      pMP->UpdateNormalAndDepth();
    }
  }

  if (sensor_type == SensorType::IMU_MONOCULAR) {
    pKFcur->mPrevKF = pKFini;
    pKFini->mNextKF = pKFcur;
    pKFcur->mpImuPreintegrated = mpImuPreintegratedFromLastKF;

    mpImuPreintegratedFromLastKF = new IMU::Preintegrated(
        pKFcur->mpImuPreintegrated->GetUpdatedBias(), pKFcur->mImuCalib);
  }

  mpLocalMapper->InsertKeyFrame(pKFini);
  mpLocalMapper->InsertKeyFrame(pKFcur);
  mpLocalMapper->mFirstTs = pKFcur->mTimeStamp;

  mCurrentFrame.SetPose(pKFcur->GetPose());
  mnLastKeyFrameId = mCurrentFrame.mnId;
  mpLastKeyFrame = pKFcur;
  // mnLastRelocFrameId = mInitialFrame.mnId;

  mvpLocalKeyFrames.push_back(pKFcur);
  mvpLocalKeyFrames.push_back(pKFini);
  mvpLocalMapPoints = mpAtlas->GetAllMapPoints();
  mpReferenceKF = pKFcur;
  mCurrentFrame.mpReferenceKF = pKFcur;

  // Compute here initial velocity
  vector<KeyFrame *> vKFs = mpAtlas->GetAllKeyFrames();

  Sophus::SE3f deltaT = vKFs.back()->GetPose() * vKFs.front()->GetPoseInverse();
  mbVelocity = false;
  Eigen::Vector3f phi = deltaT.so3().log();

  double aux = (mCurrentFrame.mTimeStamp - mLastFrame.mTimeStamp) /
               (mCurrentFrame.mTimeStamp - mInitialFrame.mTimeStamp);
  phi *= aux;
  // DEBUG_MSG("mLastFrame = Frame(mCurrentFrame);");
  mLastFrame = Frame(mCurrentFrame);
  // DEBUG_MSG("mpAtlas->SetReferenceMapPoints(mvpLocalMapPoints);");
  mpAtlas->SetReferenceMapPoints(mvpLocalMapPoints);
  // DEBUG_MSG("mpMapDrawer->SetCurrentCameraPose(pKFcur->GetPose());");
  mpMapDrawer->SetCurrentCameraPose(pKFcur->GetPose());
  // DEBUG_MSG("mpAtlas->GetCurrentMap()->mvpKeyFrameOrigins.push_back(pKFini);");
  mpAtlas->GetCurrentMap()->mvpKeyFrameOrigins.push_back(pKFini);

  mState = OK;

  initID = pKFcur->mnId;
}

void Tracking::CreateMapInAtlas() {
  mnLastInitFrameId = mCurrentFrame.mnId;
  mpAtlas->CreateNewMap();
  if (sensor_type & SensorType::USE_IMU)
    mpAtlas->SetInertialSensor();
  mbSetInit = false;

  mnInitialFrameId = mCurrentFrame.mnId + 1;
  mState = NO_IMAGES_YET;

  // Restart the variable with information about the last KF
  mbVelocity = false;
  // mnLastRelocFrameId = mnLastInitFrameId; // The last relocation KF_id is the
  // current id, because it is the new starting point for new map
  Verbose::Log("First frame id in map: " + to_string(mnLastInitFrameId + 1),
               Verbose::VERBOSITY_NORMAL);
  mbVO =
      false; // Init value for know if there are enough MapPoints in the last KF
  if ((sensor_type & SensorType::CAMERA_MASK) == SensorType::MONOCULAR) {
    mbReadyToInitialize = false;
  }

  if ((sensor_type & SensorType::USE_IMU) && mpImuPreintegratedFromLastKF) {
    delete mpImuPreintegratedFromLastKF;
    mpImuPreintegratedFromLastKF =
        new IMU::Preintegrated(IMU::Bias(), *mpImuCalib);
  }

  if (mpLastKeyFrame)
    mpLastKeyFrame = static_cast<KeyFrame *>(NULL);

  if (mpReferenceKF)
    mpReferenceKF = static_cast<KeyFrame *>(NULL);

  mLastFrame = Frame();
  mCurrentFrame = Frame();
  mvIniMatches.clear();

  mbCreatedMap = true;
}

void Tracking::CheckReplacedInLastFrame() {
  for (int i = 0; i < mLastFrame.N; i++) {
    MapPoint *pMP = mLastFrame.mvpMapPoints[i];

    if (pMP) {
      MapPoint *pRep = pMP->GetReplaced();
      if (pRep) {
        mLastFrame.mvpMapPoints[i] = pRep;
      }
    }
  }
}

bool Tracking::TrackReferenceKeyFrame() {
  // Compute Bag of Words vector
  mCurrentFrame.ComputeBoW();

  // We perform first an ORB matching with the reference keyframe
  // If enough matches are found we setup a PnP solver
  ORBmatcher matcher(0.7, true);
  vector<MapPoint *> vpMapPointMatches;

  int nmatches =
      matcher.SearchByBoW(mpReferenceKF, mCurrentFrame, vpMapPointMatches);

  if (nmatches < 15) {
    cerr << "TRACK_REF_KF: Less than 15 matches!!\n";
    return false;
  }

  mCurrentFrame.mvpMapPoints = vpMapPointMatches;
  mCurrentFrame.SetPose(mLastFrame.GetPose());

  // mCurrentFrame.PrintPointDistribution();

  // cerr << " TrackReferenceKeyFrame mLastFrame.mTcw:  " << mLastFrame.mTcw <<
  // endl;
  Optimizer::PoseOptimization(&mCurrentFrame);

  // Discard outliers
  int nmatchesMap = 0;
  for (int i = 0; i < mCurrentFrame.N; i++) {
    // if(i >= mCurrentFrame.Nleft) break;
    if (mCurrentFrame.mvpMapPoints[i]) {
      if (mCurrentFrame.mvbOutlier[i]) {
        MapPoint *pMP = mCurrentFrame.mvpMapPoints[i];

        mCurrentFrame.mvpMapPoints[i] = static_cast<MapPoint *>(NULL);
        mCurrentFrame.mvbOutlier[i] = false;
        if (i < mCurrentFrame.Nleft) {
          pMP->mbTrackInView = false;
        } else {
          pMP->mbTrackInViewR = false;
        }
        pMP->mbTrackInView = false;
        pMP->mnLastFrameSeen = mCurrentFrame.mnId;
        nmatches--;
      } else if (mCurrentFrame.mvpMapPoints[i]->Observations() > 0)
        nmatchesMap++;
    }
  }

  if (sensor_type & SensorType::USE_IMU)
    return true;
  else
    return nmatchesMap >= 10;
}

void Tracking::UpdateLastFrame() {
  // Update pose according to reference keyframe
  KeyFrame *pRef = mLastFrame.mpReferenceKF;
  Sophus::SE3f Tlr = mlRelativeFramePoses.back();
  mLastFrame.SetPose(Tlr * pRef->GetPose());

  if (mnLastKeyFrameId == mLastFrame.mnId ||
      (sensor_type & SensorType::CAMERA_MASK) == SensorType::MONOCULAR ||
      !mbOnlyTracking)
    return;

  // Create "visual odometry" MapPoints
  // We sort points according to their measured depth by the stereo/RGB-D sensor
  vector<pair<float, int>> vDepthIdx;
  const int Nfeat = mLastFrame.Nleft == -1 ? mLastFrame.N : mLastFrame.Nleft;
  vDepthIdx.reserve(Nfeat);
  for (int i = 0; i < Nfeat; i++) {
    float z = mLastFrame.mvDepth[i];
    if (z > 0) {
      vDepthIdx.push_back(make_pair(z, i));
    }
  }

  if (vDepthIdx.empty())
    return;

  sort(vDepthIdx.begin(), vDepthIdx.end());

  // We insert all close points (depth<mThDepth)
  // If less than 100 close points, we insert the 100 closest ones.
  int nPoints = 0;
  for (size_t j = 0; j < vDepthIdx.size(); j++) {
    int i = vDepthIdx[j].second;

    bool bCreateNew = false;

    MapPoint *pMP = mLastFrame.mvpMapPoints[i];

    if (!pMP)
      bCreateNew = true;
    else if (pMP->Observations() < 1)
      bCreateNew = true;

    if (bCreateNew) {
      Eigen::Vector3f x3D;

      if (mLastFrame.Nleft == -1) {
        mLastFrame.UnprojectStereo(i, x3D);
      } else {
        x3D = mLastFrame.UnprojectStereoFishEye(i);
      }

      MapPoint *pNewMP =
          new MapPoint(x3D, mpAtlas->GetCurrentMap(), &mLastFrame, i);
      mLastFrame.mvpMapPoints[i] = pNewMP;

      mlpTemporalPoints.push_back(pNewMP);
      nPoints++;
    } else {
      nPoints++;
    }

    if (vDepthIdx[j].first > mThDepth && nPoints > 100)
      break;
  }
}

bool Tracking::TrackWithMotionModel() {
  ORBmatcher matcher(0.9, true);

  // Update last frame pose according to its reference keyframe
  // Create "visual odometry" points if in Localization Mode
  UpdateLastFrame();

  if (mpAtlas->isImuInitialized() &&
      (mCurrentFrame.mnId > mnLastRelocFrameId + mnFramesToResetIMU)) {
    // Predict state with IMU if it is initialized and it doesnt need reset
    PredictStateIMU();
    return true;
  } else {
    mCurrentFrame.SetPose(mVelocity * mLastFrame.GetPose());
  }

  fill(mCurrentFrame.mvpMapPoints.begin(), mCurrentFrame.mvpMapPoints.end(),
       static_cast<MapPoint *>(NULL));

  // Project points seen in previous frame
  int th;

  if (sensor_type == SensorType::STEREO)
    th = 7;
  else
    th = 15;

  int nmatches = matcher.SearchByProjection(
      mCurrentFrame, mLastFrame, th,
      (sensor_type & SensorType::CAMERA_MASK) == SensorType::MONOCULAR);

  // If few matches, uses a wider window search
  if (nmatches < 20) {
    Verbose::Log("Not enough matches, wider window search!!",
                 Verbose::VERBOSITY_NORMAL);
    fill(mCurrentFrame.mvpMapPoints.begin(), mCurrentFrame.mvpMapPoints.end(),
         static_cast<MapPoint *>(NULL));

    nmatches = matcher.SearchByProjection(
        mCurrentFrame, mLastFrame, 2 * th,
        (sensor_type & SensorType::CAMERA_MASK) == SensorType::MONOCULAR);
    Verbose::Log("Matches with wider search: " + to_string(nmatches),
                 Verbose::VERBOSITY_NORMAL);
  }

  if (nmatches < 20) {
    Verbose::Log("Not enough matches!!", Verbose::VERBOSITY_NORMAL);
    if (sensor_type & SensorType::USE_IMU)
      return true;
    else
      return false;
  }

  // Optimize frame pose with all matches
  Optimizer::PoseOptimization(&mCurrentFrame);

  // Discard outliers
  int nmatchesMap = 0;
  for (int i = 0; i < mCurrentFrame.N; i++) {
    if (mCurrentFrame.mvpMapPoints[i]) {
      if (mCurrentFrame.mvbOutlier[i]) {
        MapPoint *pMP = mCurrentFrame.mvpMapPoints[i];

        mCurrentFrame.mvpMapPoints[i] = static_cast<MapPoint *>(NULL);
        mCurrentFrame.mvbOutlier[i] = false;
        if (i < mCurrentFrame.Nleft) {
          pMP->mbTrackInView = false;
        } else {
          pMP->mbTrackInViewR = false;
        }
        pMP->mnLastFrameSeen = mCurrentFrame.mnId;
        nmatches--;
      } else if (mCurrentFrame.mvpMapPoints[i]->Observations() > 0)
        nmatchesMap++;
    }
  }

  if (mbOnlyTracking) {
    mbVO = nmatchesMap < 10;
    return nmatches > 20;
  }

  if (sensor_type & SensorType::USE_IMU)
    return true;
  else
    return nmatchesMap >= 10;
}

bool Tracking::TrackLocalMap() {

  // We have an estimation of the camera pose and some map points tracked in the
  // frame. We retrieve the local map and try to find matches to points in the
  // local map.
  mTrackedFr++;

  UpdateLocalMap();
  SearchLocalPoints();

  // TOO check outliers before PO
  int aux1 = 0, aux2 = 0;
  for (int i = 0; i < mCurrentFrame.N; i++)
    if (mCurrentFrame.mvpMapPoints[i]) {
      aux1++;
      if (mCurrentFrame.mvbOutlier[i])
        aux2++;
    }

  int inliers;
  if (!mpAtlas->isImuInitialized())
    Optimizer::PoseOptimization(&mCurrentFrame);
  else {
    if (mCurrentFrame.mnId <= mnLastRelocFrameId + mnFramesToResetIMU) {
      Verbose::Log("TLM: PoseOptimization ", Verbose::VERBOSITY_DEBUG);
      Optimizer::PoseOptimization(&mCurrentFrame);
    } else {
      // if(!mbMapUpdated && mState == OK) //  && (mnMatchesInliers>30))
      if (!mbMapUpdated) //  && (mnMatchesInliers>30))
      {
        Verbose::Log("TLM: PoseInertialOptimizationLastFrame ",
                     Verbose::VERBOSITY_DEBUG);
        inliers = Optimizer::PoseInertialOptimizationLastFrame(
            &mCurrentFrame); // , !mpLastKeyFrame->GetMap()->GetIniertialBA1());
      } else {
        Verbose::Log("TLM: PoseInertialOptimizationLastKeyFrame ",
                     Verbose::VERBOSITY_DEBUG);
        inliers = Optimizer::PoseInertialOptimizationLastKeyFrame(
            &mCurrentFrame); // , !mpLastKeyFrame->GetMap()->GetIniertialBA1());
      }
    }
  }

  aux1 = 0, aux2 = 0;
  for (int i = 0; i < mCurrentFrame.N; i++)
    if (mCurrentFrame.mvpMapPoints[i]) {
      aux1++;
      if (mCurrentFrame.mvbOutlier[i])
        aux2++;
    }

  mnMatchesInliers = 0;

  // Update MapPoints Statistics
  for (int i = 0; i < mCurrentFrame.N; i++) {
    if (mCurrentFrame.mvpMapPoints[i]) {
      if (!mCurrentFrame.mvbOutlier[i]) {
        mCurrentFrame.mvpMapPoints[i]->IncreaseFound();
        if (!mbOnlyTracking) {
          if (mCurrentFrame.mvpMapPoints[i]->Observations() > 0)
            mnMatchesInliers++;
        } else
          mnMatchesInliers++;
      } else if (sensor_type == SensorType::STEREO)
        mCurrentFrame.mvpMapPoints[i] = static_cast<MapPoint *>(NULL);
    }
  }

  // Decide if the tracking was succesful
  // More restrictive if there was a relocalization recently
  mpLocalMapper->mnMatchesInliers = mnMatchesInliers;
  if (mCurrentFrame.mnId < mnLastRelocFrameId + mMaxFrames &&
      mnMatchesInliers < 50)
    return false;

  if ((mnMatchesInliers > 10) && (mState == RECENTLY_LOST))
    return true;

  if (sensor_type == SensorType::IMU_MONOCULAR) {
    if ((mnMatchesInliers < 15 && mpAtlas->isImuInitialized()) ||
        (mnMatchesInliers < 50 && !mpAtlas->isImuInitialized())) {
      return false;
    } else
      return true;
  } else if (sensor_type == SensorType::IMU_STEREO ||
             sensor_type == SensorType::IMU_RGB_D) {
    if (mnMatchesInliers < 15) {
      return false;
    } else
      return true;
  } else {
    if (mnMatchesInliers < 30)
      return false;
    else
      return true;
  }
}

bool Tracking::NeedNewKeyFrame() {
  if ((sensor_type & SensorType::USE_IMU) &&
      !mpAtlas->GetCurrentMap()->isImuInitialized()) {
    if (sensor_type == SensorType::IMU_MONOCULAR &&
        (mCurrentFrame.mTimeStamp - mpLastKeyFrame->mTimeStamp) >= 0.25)
      return true;
    else if ((sensor_type == SensorType::IMU_STEREO ||
              sensor_type == SensorType::IMU_RGB_D) &&
             (mCurrentFrame.mTimeStamp - mpLastKeyFrame->mTimeStamp) >= 0.25)
      return true;
    else
      return false;
  }

  if (mbOnlyTracking)
    return false;

  // If Local Mapping is freezed by a Loop Closure do not insert keyframes
  if (mpLocalMapper->isStopped() || mpLocalMapper->stopRequested()) {
    /*if(sensor_type == SensorType::MONOCULAR)
    {
        cerr << "NeedNewKeyFrame: localmap stopped" << endl;
    }*/
    return false;
  }

  const int nKFs = mpAtlas->KeyFramesInMap();

  // Do not insert keyframes if not enough frames have passed from last
  // relocalisation
  if (mCurrentFrame.mnId < mnLastRelocFrameId + mMaxFrames &&
      nKFs > mMaxFrames) {
    return false;
  }

  // Tracked MapPoints in the reference keyframe
  int nMinObs = 3;
  if (nKFs <= 2)
    nMinObs = 2;
  int nRefMatches = mpReferenceKF->TrackedMapPoints(nMinObs);

  // Local Mapping accept keyframes?
  bool bLocalMappingIdle = mpLocalMapper->AcceptKeyFrames();

  // Check how many "close" points are being tracked and how many could be
  // potentially created.
  int nNonTrackedClose = 0;
  int nTrackedClose = 0;

  if ((sensor_type & SensorType::CAMERA_MASK) != SensorType::MONOCULAR) {
    int N = (mCurrentFrame.Nleft == -1) ? mCurrentFrame.N : mCurrentFrame.Nleft;
    for (int i = 0; i < N; i++) {
      if (mCurrentFrame.mvDepth[i] > 0 && mCurrentFrame.mvDepth[i] < mThDepth) {
        if (mCurrentFrame.mvpMapPoints[i] && !mCurrentFrame.mvbOutlier[i])
          nTrackedClose++;
        else
          nNonTrackedClose++;
      }
    }
    // Verbose::Log("[NEEDNEWKF]-> closed points: " +
    // to_string(nTrackedClose) + "; non tracked closed points: " +
    // to_string(nNonTrackedClose), Verbose::VERBOSITY_NORMAL);//
    // Verbose::VERBOSITY_DEBUG);
  }

  bool bNeedToInsertClose;
  bNeedToInsertClose = (nTrackedClose < 100) && (nNonTrackedClose > 70);

  // Thresholds
  float thRefRatio = 0.75f;
  if (nKFs < 2)
    thRefRatio = 0.4f;

  /*int nClosedPoints = nTrackedClose + nNonTrackedClose;
  const int thStereoClosedPoints = 15;
  if(nClosedPoints < thStereoClosedPoints && (sensor_type==SensorType::STEREO ||
  sensor_type==SensorType::IMU_STEREO))
  {
      //Pseudo-monocular, there are not enough close points to be confident
  about the stereo observations. thRefRatio = 0.9f;
  }*/

  if (sensor_type == SensorType::MONOCULAR)
    thRefRatio = 0.9f;

  if (mpCamera2)
    thRefRatio = 0.75f;

  if (sensor_type == SensorType::IMU_MONOCULAR) {
    if (mnMatchesInliers > 350) // Points tracked from the local map
      thRefRatio = 0.75f;
    else
      thRefRatio = 0.90f;
  }

  // Condition 1a: More than "MaxFrames" have passed from last keyframe
  // insertion
  const bool c1a = mCurrentFrame.mnId >= mnLastKeyFrameId + mMaxFrames;
  // Condition 1b: More than "MinFrames" have passed and Local Mapping is idle
  const bool c1b =
      ((mCurrentFrame.mnId >= mnLastKeyFrameId + mMinFrames) &&
       bLocalMappingIdle); // mpLocalMapper->KeyframesInQueue() < 2);
  // Condition 1c: tracking is weak
  const bool c1c =
      sensor_type != SensorType::MONOCULAR &&
      !(sensor_type & SensorType::USE_IMU) &&
      (mnMatchesInliers < nRefMatches * 0.25 || bNeedToInsertClose);
  // Condition 2: Few tracked points compared to reference keyframe. Lots of
  // visual odometry compared to map matches.
  const bool c2 =
      (((mnMatchesInliers < nRefMatches * thRefRatio || bNeedToInsertClose)) &&
       mnMatchesInliers > 15);

  // cerr << "NeedNewKF: c1a=" << c1a << "; c1b=" << c1b << "; c1c=" << c1c <<
  // "; c2=" << c2 << endl;
  //  Temporal condition for Inertial cases
  bool c3 = false;
  if (mpLastKeyFrame) {
    if (sensor_type == SensorType::IMU_MONOCULAR) {
      if ((mCurrentFrame.mTimeStamp - mpLastKeyFrame->mTimeStamp) >= 0.5)
        c3 = true;
    } else if (sensor_type == SensorType::IMU_STEREO ||
               sensor_type == SensorType::IMU_RGB_D) {
      if ((mCurrentFrame.mTimeStamp - mpLastKeyFrame->mTimeStamp) >= 0.5)
        c3 = true;
    }
  }

  bool c4 = false;
  if ((((mnMatchesInliers < 75) && (mnMatchesInliers > 15)) ||
       mState == RECENTLY_LOST) &&
      (sensor_type ==
       SensorType::IMU_MONOCULAR)) // MODIFICATION_2, originally
                                   // ((((mnMatchesInliers<75) &&
                                   // (mnMatchesInliers>15)) ||
                                   // mState==RECENTLY_LOST) && ((sensor_type
                                   // == SensorType::IMU_MONOCULAR)))
    c4 = true;
  else
    c4 = false;

  if (((c1a || c1b || c1c) && c2) || c3 || c4) {
    // If the mapping accepts keyframes, insert keyframe.
    // Otherwise send a signal to interrupt BA
    if (bLocalMappingIdle || mpLocalMapper->IsInitializing()) {
      return true;
    } else {
      mpLocalMapper->InterruptBA();
      if ((sensor_type & SensorType::CAMERA_MASK) == SensorType::MONOCULAR) {
        if (mpLocalMapper->KeyframesInQueue() < 3)
          return true;
        else
          return false;
      } else {
        // cerr << "NeedNewKeyFrame: localmap is busy" << endl;
        return false;
      }
    }
  } else
    return false;
}

void Tracking::CreateNewKeyFrame() {
  if (mpLocalMapper->IsInitializing() && !mpAtlas->isImuInitialized())
    return;

  if (!mpLocalMapper->SetNotStop(true))
    return;

  KeyFrame *pKF =
      new KeyFrame(mCurrentFrame, mpAtlas->GetCurrentMap(), mpKeyFrameDB);

  if (mpAtlas->isImuInitialized()) //  || mpLocalMapper->IsInitializing())
    pKF->bImu = true;

  pKF->SetNewBias(mCurrentFrame.mImuBias);
  mpReferenceKF = pKF;
  mCurrentFrame.mpReferenceKF = pKF;

  if (mpLastKeyFrame) {
    pKF->mPrevKF = mpLastKeyFrame;
    mpLastKeyFrame->mNextKF = pKF;
  } else
    Verbose::Log("No last KF in KF creation!!", Verbose::VERBOSITY_NORMAL);

  // Reset preintegration from last KF (Create new object)
  if (sensor_type & SensorType::USE_IMU) {
    mpImuPreintegratedFromLastKF =
        new IMU::Preintegrated(pKF->GetImuBias(), pKF->mImuCalib);
  }

  if (sensor_type != SensorType::MONOCULAR &&
      sensor_type !=
          SensorType::IMU_MONOCULAR) // TODO check if incluide imu_stereo
  {
    mCurrentFrame.UpdatePoseMatrices();
    // cerr << "create new MPs" << endl;
    // We sort points by the measured depth by the stereo/RGBD sensor.
    // We create all those MapPoints whose depth < mThDepth.
    // If there are less than 100 close points we create the 100 closest.
    int maxPoint = 100;
    if (sensor_type == SensorType::IMU_STEREO ||
        sensor_type == SensorType::IMU_RGB_D)
      maxPoint = 100;

    vector<pair<float, int>> vDepthIdx;
    int N = (mCurrentFrame.Nleft != -1) ? mCurrentFrame.Nleft : mCurrentFrame.N;
    vDepthIdx.reserve(mCurrentFrame.N);
    for (int i = 0; i < N; i++) {
      float z = mCurrentFrame.mvDepth[i];
      if (z > 0) {
        vDepthIdx.push_back(make_pair(z, i));
      }
    }

    if (!vDepthIdx.empty()) {
      sort(vDepthIdx.begin(), vDepthIdx.end());

      int nPoints = 0;
      for (size_t j = 0; j < vDepthIdx.size(); j++) {
        int i = vDepthIdx[j].second;

        bool bCreateNew = false;

        MapPoint *pMP = mCurrentFrame.mvpMapPoints[i];
        if (!pMP)
          bCreateNew = true;
        else if (pMP->Observations() < 1) {
          bCreateNew = true;
          mCurrentFrame.mvpMapPoints[i] = static_cast<MapPoint *>(NULL);
        }

        if (bCreateNew) {
          Eigen::Vector3f x3D;

          if (mCurrentFrame.Nleft == -1) {
            mCurrentFrame.UnprojectStereo(i, x3D);
          } else {
            x3D = mCurrentFrame.UnprojectStereoFishEye(i);
          }

          MapPoint *pNewMP = new MapPoint(x3D, pKF, mpAtlas->GetCurrentMap());
          pNewMP->AddObservation(pKF, i);

          // Check if it is a stereo observation in order to not
          // duplicate mappoints
          if (mCurrentFrame.Nleft != -1 &&
              mCurrentFrame.mvLeftToRightMatch[i] >= 0) {
            mCurrentFrame.mvpMapPoints[mCurrentFrame.Nleft +
                                       mCurrentFrame.mvLeftToRightMatch[i]] =
                pNewMP;
            pNewMP->AddObservation(
                pKF, mCurrentFrame.Nleft + mCurrentFrame.mvLeftToRightMatch[i]);
            pKF->AddMapPoint(pNewMP, mCurrentFrame.Nleft +
                                         mCurrentFrame.mvLeftToRightMatch[i]);
          }

          pKF->AddMapPoint(pNewMP, i);
          pNewMP->ComputeDistinctiveDescriptors();
          pNewMP->UpdateNormalAndDepth();
          mpAtlas->AddMapPoint(pNewMP);

          mCurrentFrame.mvpMapPoints[i] = pNewMP;
          nPoints++;
        } else {
          nPoints++;
        }

        if (vDepthIdx[j].first > mThDepth && nPoints > maxPoint) {
          break;
        }
      }
      // Verbose::Log("new mps for stereo KF: " + to_string(nPoints),
      // Verbose::VERBOSITY_NORMAL);
    }
  }

  mpLocalMapper->InsertKeyFrame(pKF);

  mpLocalMapper->SetNotStop(false);

  mnLastKeyFrameId = mCurrentFrame.mnId;
  mpLastKeyFrame = pKF;
}

void Tracking::SearchLocalPoints() {
  // Do not search map points already matched
  for (vector<MapPoint *>::iterator vit = mCurrentFrame.mvpMapPoints.begin(),
                                    vend = mCurrentFrame.mvpMapPoints.end();
       vit != vend; vit++) {
    MapPoint *pMP = *vit;
    if (pMP) {
      if (pMP->isBad()) {
        *vit = static_cast<MapPoint *>(NULL);
      } else {
        pMP->IncreaseVisible();
        pMP->mnLastFrameSeen = mCurrentFrame.mnId;
        pMP->mbTrackInView = false;
        pMP->mbTrackInViewR = false;
      }
    }
  }

  int nToMatch = 0;

  // Project points in frame and check its visibility
  for (vector<MapPoint *>::iterator vit = mvpLocalMapPoints.begin(),
                                    vend = mvpLocalMapPoints.end();
       vit != vend; vit++) {
    MapPoint *pMP = *vit;

    if (pMP->mnLastFrameSeen == mCurrentFrame.mnId)
      continue;
    if (pMP->isBad())
      continue;
    // Project (this fills MapPoint variables for matching)
    if (mCurrentFrame.isInFrustum(pMP, 0.5)) {
      pMP->IncreaseVisible();
      nToMatch++;
    }
    if (pMP->mbTrackInView) {
      mCurrentFrame.mmProjectPoints[pMP->mnId] =
          cv::Point2f(pMP->mTrackProjX, pMP->mTrackProjY);
    }
  }

  if (nToMatch > 0) {
    ORBmatcher matcher(0.8);
    int th = 1;
    if (sensor_type == SensorType::RGB_D ||
        sensor_type == SensorType::IMU_RGB_D)
      th = 3;
    if (mpAtlas->isImuInitialized()) {
      if (mpAtlas->GetCurrentMap()->GetIniertialBA2())
        th = 2;
      else
        th = 6;
    } else if (!mpAtlas->isImuInitialized() &&
               (sensor_type & SensorType::USE_IMU)) {
      th = 10;
    }

    // If the camera has been relocalised recently, perform a coarser search
    if (mCurrentFrame.mnId < mnLastRelocFrameId + 2)
      th = 5;

    if (mState == LOST ||
        mState == RECENTLY_LOST) // Lost for less than 1 second
      th = 15;                   // 15

    int matches = matcher.SearchByProjection(mCurrentFrame, mvpLocalMapPoints,
                                             th, mpLocalMapper->mbFarPoints,
                                             mpLocalMapper->mThFarPoints);
  }
}

void Tracking::UpdateLocalMap() {
  // This is for visualization
  mpAtlas->SetReferenceMapPoints(mvpLocalMapPoints);

  // Update
  UpdateLocalKeyFrames();
  UpdateLocalPoints();
}

void Tracking::UpdateLocalPoints() {
  mvpLocalMapPoints.clear();

  int count_pts = 0;

  for (vector<KeyFrame *>::const_reverse_iterator
           itKF = mvpLocalKeyFrames.rbegin(),
           itEndKF = mvpLocalKeyFrames.rend();
       itKF != itEndKF; ++itKF) {
    KeyFrame *pKF = *itKF;
    const vector<MapPoint *> vpMPs = pKF->GetMapPointMatches();

    for (vector<MapPoint *>::const_iterator itMP = vpMPs.begin(),
                                            itEndMP = vpMPs.end();
         itMP != itEndMP; itMP++) {

      MapPoint *pMP = *itMP;
      if (!pMP)
        continue;
      if (pMP->mnTrackReferenceForFrame == mCurrentFrame.mnId)
        continue;
      if (!pMP->isBad()) {
        count_pts++;
        mvpLocalMapPoints.push_back(pMP);
        pMP->mnTrackReferenceForFrame = mCurrentFrame.mnId;
      }
    }
  }
}

#define SKIP_NULL(ptr)                                                         \
  if ((ptr) == nullptr)                                                        \
    continue;

void Tracking::UpdateLocalKeyFrames() {
  // Each map point vote for the keyframes in which it has been observed
  Frame *frame = nullptr;
  map<KeyFrame *, int> key_frame_counter;
  if (!mpAtlas->isImuInitialized() ||
      (mCurrentFrame.mnId < mnLastRelocFrameId + 2))
    frame = &mCurrentFrame;
  else
    frame = &mLastFrame;

  for (auto &map_point : frame->mvpMapPoints) {
    if (map_point != nullptr && !map_point->isBad()) {
      const auto observations = map_point->GetObservations();
      for (const auto ob : observations)
        key_frame_counter[ob.first]++;
    } else {
      map_point = nullptr;
    }
  }

  mvpLocalKeyFrames.clear();
  mvpLocalKeyFrames.reserve(3 * key_frame_counter.size());

  // All keyframes that observe a map point are included in the local map.
  // Also check which keyframe shares most points (best keyframe).
  KeyFrame *best_key_frame = static_cast<KeyFrame *>(nullptr);
  for (const auto &it : key_frame_counter) {
    auto &key_frame = *it.first;
    auto &count = it.second;
    SKIP_NULL(&key_frame);
    if (best_key_frame == nullptr || count > key_frame_counter[best_key_frame])
      best_key_frame = &key_frame;
    mvpLocalKeyFrames.push_back(&key_frame);
    key_frame.mnTrackReferenceForFrame = mCurrentFrame.mnId;
  }

  // Include also some not-already-included keyframes that are neighbors to
  // already-included keyframes
  for (auto &local_kf_ptr : mvpLocalKeyFrames) {
    // Limit the number of keyframes
    if (mvpLocalKeyFrames.size() > 80) // 80
      break;
    if (local_kf_ptr == nullptr || local_kf_ptr->isBad())
      continue;
    auto &local_key_frame = *local_kf_ptr;

    for (auto covis_kf_ptr : local_key_frame.GetBestCovisibilityKeyFrames(10)) {
      SKIP_NULL(covis_kf_ptr);
      auto &covis_key_frame = *covis_kf_ptr;
      if (covis_key_frame.mnTrackReferenceForFrame != mCurrentFrame.mnId) {
        mvpLocalKeyFrames.push_back(&covis_key_frame);
        covis_key_frame.mnTrackReferenceForFrame = mCurrentFrame.mnId;
        break;
      }
    }

    for (auto &child_kf_ptr : local_key_frame.GetChilds()) {
      SKIP_NULL(child_kf_ptr);
      auto &child = *child_kf_ptr;
      if (child.mnTrackReferenceForFrame != mCurrentFrame.mnId) {
        mvpLocalKeyFrames.push_back(&child);
        child.mnTrackReferenceForFrame = mCurrentFrame.mnId;
        break;
      }
    }

    auto &parent_frame = *local_key_frame.GetParent();
    SKIP_NULL(&parent_frame);
    if (parent_frame.mnTrackReferenceForFrame != mCurrentFrame.mnId) {
      mvpLocalKeyFrames.push_back(&parent_frame);
      parent_frame.mnTrackReferenceForFrame = mCurrentFrame.mnId;
      break;
    }
  }

  // Add 10 last temporal KFs (mainly for IMU)
  if ((sensor_type & SensorType::USE_IMU) && mvpLocalKeyFrames.size() < 80) {
    auto temporal_keyframe = mCurrentFrame.mpLastKeyFrame;

    const int Nd = 20;
    for (int i = 0; i < Nd; i++) {
      if (temporal_keyframe == nullptr || temporal_keyframe->isBad())
        break;
      if (temporal_keyframe->mnTrackReferenceForFrame != mCurrentFrame.mnId) {
        mvpLocalKeyFrames.push_back(temporal_keyframe);
        temporal_keyframe->mnTrackReferenceForFrame = mCurrentFrame.mnId;
        temporal_keyframe = temporal_keyframe->mPrevKF;
      }
    }
  }

  if (best_key_frame) {
    mpReferenceKF = best_key_frame;
    mCurrentFrame.mpReferenceKF = mpReferenceKF;
  }
}

bool Tracking::Relocalization() {
  Verbose::Log("Starting relocalization", Verbose::VERBOSITY_NORMAL);
  // Compute Bag of Words Vector
  mCurrentFrame.ComputeBoW();

  // Relocalization is performed when tracking is lost
  // Track Lost: Query KeyFrame Database for keyframe candidates for
  // relocalisation
  vector<KeyFrame *> vpCandidateKFs =
      mpKeyFrameDB->DetectRelocalizationCandidates(&mCurrentFrame,
                                                   mpAtlas->GetCurrentMap());

  if (vpCandidateKFs.empty()) {
    Verbose::Log("There are not candidates", Verbose::VERBOSITY_NORMAL);
    return false;
  }

  const int nKFs = vpCandidateKFs.size();

  // We perform first an ORB matching with each candidate
  // If enough matches are found we setup a PnP solver
  ORBmatcher matcher(0.75, true);

  vector<MLPnPsolver *> vpMLPnPsolvers;
  vpMLPnPsolvers.resize(nKFs);

  vector<vector<MapPoint *>> vvpMapPointMatches;
  vvpMapPointMatches.resize(nKFs);

  vector<bool> vbDiscarded;
  vbDiscarded.resize(nKFs);

  int nCandidates = 0;

  for (int i = 0; i < nKFs; i++) {
    KeyFrame *pKF = vpCandidateKFs[i];
    if (pKF->isBad())
      vbDiscarded[i] = true;
    else {
      int nmatches =
          matcher.SearchByBoW(pKF, mCurrentFrame, vvpMapPointMatches[i]);
      if (nmatches < 15) {
        vbDiscarded[i] = true;
        continue;
      } else {
        MLPnPsolver *pSolver =
            new MLPnPsolver(mCurrentFrame, vvpMapPointMatches[i]);
        pSolver->SetRansacParameters(
            0.99, 10, 300, 6, 0.5,
            5.991); // This solver needs at least 6 points
        vpMLPnPsolvers[i] = pSolver;
        nCandidates++;
      }
    }
  }

  // Alternatively perform some iterations of P4P RANSAC
  // Until we found a camera pose supported by enough inliers
  bool bMatch = false;
  ORBmatcher matcher2(0.9, true);

  while (nCandidates > 0 && !bMatch) {
    for (int i = 0; i < nKFs; i++) {
      if (vbDiscarded[i])
        continue;

      // Perform 5 Ransac Iterations
      vector<bool> vbInliers;
      int nInliers;
      bool bNoMore;

      MLPnPsolver *pSolver = vpMLPnPsolvers[i];
      Eigen::Matrix4f eigTcw;
      bool bTcw = pSolver->iterate(5, bNoMore, vbInliers, nInliers, eigTcw);

      // If Ransac reachs max. iterations discard keyframe
      if (bNoMore) {
        vbDiscarded[i] = true;
        nCandidates--;
      }

      // If a Camera Pose is computed, optimize
      if (bTcw) {
        Sophus::SE3f Tcw(eigTcw);
        mCurrentFrame.SetPose(Tcw);
        // Tcw.copyTo(mCurrentFrame.mTcw);

        set<MapPoint *> sFound;

        const int np = vbInliers.size();

        for (int j = 0; j < np; j++) {
          if (vbInliers[j]) {
            mCurrentFrame.mvpMapPoints[j] = vvpMapPointMatches[i][j];
            sFound.insert(vvpMapPointMatches[i][j]);
          } else
            mCurrentFrame.mvpMapPoints[j] = NULL;
        }

        int nGood = Optimizer::PoseOptimization(&mCurrentFrame);

        if (nGood < 10)
          continue;

        for (int io = 0; io < mCurrentFrame.N; io++)
          if (mCurrentFrame.mvbOutlier[io])
            mCurrentFrame.mvpMapPoints[io] = static_cast<MapPoint *>(NULL);

        // If few inliers, search by projection in a coarse window and optimize
        // again
        if (nGood < 50) {
          int nadditional = matcher2.SearchByProjection(
              mCurrentFrame, vpCandidateKFs[i], sFound, 10, 100);

          if (nadditional + nGood >= 50) {
            nGood = Optimizer::PoseOptimization(&mCurrentFrame);

            // If many inliers but still not enough, search by projection again
            // in a narrower window the camera has been already optimized with
            // many points
            if (nGood > 30 && nGood < 50) {
              sFound.clear();
              for (int ip = 0; ip < mCurrentFrame.N; ip++)
                if (mCurrentFrame.mvpMapPoints[ip])
                  sFound.insert(mCurrentFrame.mvpMapPoints[ip]);
              nadditional = matcher2.SearchByProjection(
                  mCurrentFrame, vpCandidateKFs[i], sFound, 3, 64);

              // Final optimization
              if (nGood + nadditional >= 50) {
                nGood = Optimizer::PoseOptimization(&mCurrentFrame);

                for (int io = 0; io < mCurrentFrame.N; io++)
                  if (mCurrentFrame.mvbOutlier[io])
                    mCurrentFrame.mvpMapPoints[io] = NULL;
              }
            }
          }
        }

        // If the pose is supported by enough inliers stop ransacs and continue
        if (nGood >= 50) {
          bMatch = true;
          break;
        }
      }
    }
  }

  if (!bMatch) {
    return false;
  } else {
    mnLastRelocFrameId = mCurrentFrame.mnId;
    cerr << "Relocalized!!" << endl;
    return true;
  }
}

void Tracking::Reset(bool bLocMap) {
  Verbose::Log("System Reseting", Verbose::VERBOSITY_NORMAL);

  if (mpViewer) {
    mpViewer->RequestStop();
    while (!mpViewer->isStopped())
      usleep(3000);
  }

  // Reset Local Mapping
  if (!bLocMap) {
    Verbose::Log("Reseting Local Mapper...", Verbose::VERBOSITY_NORMAL);
    mpLocalMapper->RequestReset();
    Verbose::Log("done", Verbose::VERBOSITY_NORMAL);
  }

  // Reset Loop Closing
  Verbose::Log("Reseting Loop Closing...", Verbose::VERBOSITY_NORMAL);
  mpLoopClosing->RequestReset();
  Verbose::Log("done", Verbose::VERBOSITY_NORMAL);

  // Clear BoW Database
  Verbose::Log("Reseting Database...", Verbose::VERBOSITY_NORMAL);
  mpKeyFrameDB->clear();
  Verbose::Log("done", Verbose::VERBOSITY_NORMAL);

  // Clear Map (this erase MapPoints and KeyFrames)
  mpAtlas->clearAtlas();
  mpAtlas->CreateNewMap();
  if (sensor_type & SensorType::USE_IMU)
    mpAtlas->SetInertialSensor();
  mnInitialFrameId = 0;

  KeyFrame::nNextId = 0;
  Frame::nNextId = 0;
  mState = NO_IMAGES_YET;

  mbReadyToInitialize = false;
  mbSetInit = false;

  mlRelativeFramePoses.clear();
  mlpReferences.clear();
  mlFrameTimes.clear();
  mlbLost.clear();
  mCurrentFrame = Frame();
  mnLastRelocFrameId = 0;
  mLastFrame = Frame();
  mpReferenceKF = static_cast<KeyFrame *>(NULL);
  mpLastKeyFrame = static_cast<KeyFrame *>(NULL);
  mvIniMatches.clear();

  if (mpViewer)
    mpViewer->Release();

  Verbose::Log("   End reseting! ", Verbose::VERBOSITY_NORMAL);
}

void Tracking::ResetActiveMap(bool bLocMap) {
  Verbose::Log("Active map Reseting", Verbose::VERBOSITY_NORMAL);
  if (mpViewer) {
    mpViewer->RequestStop();
    while (!mpViewer->isStopped())
      usleep(3000);
  }

  Map *pMap = mpAtlas->GetCurrentMap();

  if (!bLocMap) {
    Verbose::Log("Reseting Local Mapper...", Verbose::VERBOSITY_VERY_VERBOSE);
    mpLocalMapper->RequestResetActiveMap(pMap);
    Verbose::Log("done", Verbose::VERBOSITY_VERY_VERBOSE);
  }

  // Reset Loop Closing
  Verbose::Log("Reseting Loop Closing...", Verbose::VERBOSITY_NORMAL);
  mpLoopClosing->RequestResetActiveMap(pMap);
  Verbose::Log("done", Verbose::VERBOSITY_NORMAL);

  // Clear BoW Database
  Verbose::Log("Reseting Database", Verbose::VERBOSITY_NORMAL);
  mpKeyFrameDB->clearMap(pMap); // Only clear the active map references
  Verbose::Log("done", Verbose::VERBOSITY_NORMAL);

  // Clear Map (this erase MapPoints and KeyFrames)
  mpAtlas->clearMap();

  // KeyFrame::nNextId = mpAtlas->GetLastInitKFid();
  // Frame::nNextId = mnLastInitFrameId;
  mnLastInitFrameId = Frame::nNextId;
  // mnLastRelocFrameId = mnLastInitFrameId;
  mState = NO_IMAGES_YET; // NOT_INITIALIZED;

  mbReadyToInitialize = false;

  list<bool> lbLost;
  // lbLost.reserve(mlbLost.size());
  unsigned int index = mnFirstFrameId;
  cerr << "mnFirstFrameId = " << mnFirstFrameId << endl;
  for (Map *pMap : mpAtlas->GetAllMaps()) {
    if (pMap->GetAllKeyFrames().size() > 0) {
      if (index > pMap->GetLowerKFID())
        index = pMap->GetLowerKFID();
    }
  }

  // cerr << "First Frame id: " << index << endl;
  int num_lost = 0;
  cerr << "mnInitialFrameId = " << mnInitialFrameId << endl;

  for (list<bool>::iterator ilbL = mlbLost.begin(); ilbL != mlbLost.end();
       ilbL++) {
    if (index < mnInitialFrameId)
      lbLost.push_back(*ilbL);
    else {
      lbLost.push_back(true);
      num_lost += 1;
    }

    index++;
  }
  cerr << num_lost << " Frames set to lost" << endl;

  mlbLost = lbLost;

  mnInitialFrameId = mCurrentFrame.mnId;
  mnLastRelocFrameId = mCurrentFrame.mnId;

  mCurrentFrame = Frame();
  mLastFrame = Frame();
  mpReferenceKF = static_cast<KeyFrame *>(NULL);
  mpLastKeyFrame = static_cast<KeyFrame *>(NULL);
  mvIniMatches.clear();

  mbVelocity = false;

  if (mpViewer)
    mpViewer->Release();

  Verbose::Log("   End reseting! ", Verbose::VERBOSITY_NORMAL);
}

vector<MapPoint *> Tracking::GetLocalMapMPS() { return mvpLocalMapPoints; }

void Tracking::ChangeCalibration(const string &strSettingPath) {
  cv::FileStorage fSettings(strSettingPath, cv::FileStorage::READ);
  float fx = fSettings["Camera.fx"];
  float fy = fSettings["Camera.fy"];
  float cx = fSettings["Camera.cx"];
  float cy = fSettings["Camera.cy"];

  mK_.setIdentity();
  mK_(0, 0) = fx;
  mK_(1, 1) = fy;
  mK_(0, 2) = cx;
  mK_(1, 2) = cy;

  cv::Mat K = cv::Mat::eye(3, 3, CV_32F);
  K.at<float>(0, 0) = fx;
  K.at<float>(1, 1) = fy;
  K.at<float>(0, 2) = cx;
  K.at<float>(1, 2) = cy;
  K.copyTo(mK);

  cv::Mat DistCoef(4, 1, CV_32F);
  DistCoef.at<float>(0) = fSettings["Camera.k1"];
  DistCoef.at<float>(1) = fSettings["Camera.k2"];
  DistCoef.at<float>(2) = fSettings["Camera.p1"];
  DistCoef.at<float>(3) = fSettings["Camera.p2"];
  const float k3 = fSettings["Camera.k3"];
  if (k3 != 0) {
    DistCoef.resize(5);
    DistCoef.at<float>(4) = k3;
  }
  DistCoef.copyTo(mDistCoef);

  mbf = fSettings["Camera.bf"];

  Frame::mbInitialComputations = true;
}

void Tracking::InformOnlyTracking(const bool &flag) { mbOnlyTracking = flag; }

void Tracking::UpdateFrameIMU(const float s, const IMU::Bias &b,
                              KeyFrame *pCurrentKeyFrame) {
  Map *pMap = pCurrentKeyFrame->GetMap();
  unsigned int index = mnFirstFrameId;
  list<ORB_SLAM3::KeyFrame *>::iterator lRit = mlpReferences.begin();
  list<bool>::iterator lbL = mlbLost.begin();
  for (auto lit = mlRelativeFramePoses.begin(),
            lend = mlRelativeFramePoses.end();
       lit != lend; lit++, lRit++, lbL++) {
    if (*lbL)
      continue;

    KeyFrame *pKF = *lRit;

    while (pKF->isBad()) {
      pKF = pKF->GetParent();
    }

    if (pKF->GetMap() == pMap) {
      (*lit).translation() *= s;
    }
  }

  mLastBias = b;

  mpLastKeyFrame = pCurrentKeyFrame;

  mLastFrame.SetNewBias(mLastBias);
  mCurrentFrame.SetNewBias(mLastBias);

  while (!mCurrentFrame.imuIsPreintegrated()) {
    usleep(500);
  }

  if (mLastFrame.mnId == mLastFrame.mpLastKeyFrame->mnFrameId) {
    mLastFrame.SetImuPoseVelocity(mLastFrame.mpLastKeyFrame->GetImuRotation(),
                                  mLastFrame.mpLastKeyFrame->GetImuPosition(),
                                  mLastFrame.mpLastKeyFrame->GetVelocity());
  } else {
    const Eigen::Vector3f Gz(0, 0, -IMU::GRAVITY_VALUE);
    const Eigen::Vector3f twb1 = mLastFrame.mpLastKeyFrame->GetImuPosition();
    const Eigen::Matrix3f Rwb1 = mLastFrame.mpLastKeyFrame->GetImuRotation();
    const Eigen::Vector3f Vwb1 = mLastFrame.mpLastKeyFrame->GetVelocity();
    float t12 = mLastFrame.mpImuPreintegrated->dT;

    mLastFrame.SetImuPoseVelocity(
        IMU::NormalizeRotation(
            Rwb1 * mLastFrame.mpImuPreintegrated->GetUpdatedDeltaRotation()),
        twb1 + Vwb1 * t12 + 0.5f * t12 * t12 * Gz +
            Rwb1 * mLastFrame.mpImuPreintegrated->GetUpdatedDeltaPosition(),
        Vwb1 + Gz * t12 +
            Rwb1 * mLastFrame.mpImuPreintegrated->GetUpdatedDeltaVelocity());
  }

  if (mCurrentFrame.mpImuPreintegrated) {
    const Eigen::Vector3f Gz(0, 0, -IMU::GRAVITY_VALUE);

    const Eigen::Vector3f twb1 = mCurrentFrame.mpLastKeyFrame->GetImuPosition();
    const Eigen::Matrix3f Rwb1 = mCurrentFrame.mpLastKeyFrame->GetImuRotation();
    const Eigen::Vector3f Vwb1 = mCurrentFrame.mpLastKeyFrame->GetVelocity();
    float t12 = mCurrentFrame.mpImuPreintegrated->dT;

    mCurrentFrame.SetImuPoseVelocity(
        IMU::NormalizeRotation(
            Rwb1 * mCurrentFrame.mpImuPreintegrated->GetUpdatedDeltaRotation()),
        twb1 + Vwb1 * t12 + 0.5f * t12 * t12 * Gz +
            Rwb1 * mCurrentFrame.mpImuPreintegrated->GetUpdatedDeltaPosition(),
        Vwb1 + Gz * t12 +
            Rwb1 * mCurrentFrame.mpImuPreintegrated->GetUpdatedDeltaVelocity());
  }

  mnFirstImuFrameId = mCurrentFrame.mnId;
}

void Tracking::NewDataset() { mnNumDataset++; }

int Tracking::GetNumberDataset() { return mnNumDataset; }

int Tracking::GetMatchesInliers() { return mnMatchesInliers; }

void Tracking::SaveSubTrajectory(string strNameFile_frames,
                                 string strNameFile_kf, string strFolder) {
  mpSystem->SaveTrajectoryEuRoC(strFolder + strNameFile_frames);
  // mpSystem->SaveKeyFrameTrajectoryEuRoC(strFolder + strNameFile_kf);
}

void Tracking::SaveSubTrajectory(string strNameFile_frames,
                                 string strNameFile_kf, Map *pMap) {
  mpSystem->SaveTrajectoryEuRoC(strNameFile_frames, pMap);
  if (!strNameFile_kf.empty())
    mpSystem->SaveKeyFrameTrajectoryEuRoC(strNameFile_kf, pMap);
}

float Tracking::GetImageScale() { return mImageScale; }

Sophus::SE3f Tracking::GetCamTwc() {
  return (mCurrentFrame.GetPose()).inverse();
}

Sophus::SE3f Tracking::GetImuTwb() { return mCurrentFrame.GetImuPose(); }

Eigen::Vector3f Tracking::GetImuVwb() { return mCurrentFrame.GetVelocity(); }

bool Tracking::isImuPreintegrated() { return mCurrentFrame.mpImuPreintegrated; }

#ifdef REGISTER_LOOP
void Tracking::RequestStop() {
  unique_lock<mutex> lock(mMutexStop);
  mbStopRequested = true;
}

bool Tracking::Stop() {
  unique_lock<mutex> lock(mMutexStop);
  if (mbStopRequested && !mbNotStop) {
    mbStopped = true;
    cerr << "Tracking STOP" << endl;
    return true;
  }

  return false;
}

bool Tracking::stopRequested() {
  unique_lock<mutex> lock(mMutexStop);
  return mbStopRequested;
}

bool Tracking::isStopped() {
  unique_lock<mutex> lock(mMutexStop);
  return mbStopped;
}

void Tracking::Release() {
  unique_lock<mutex> lock(mMutexStop);
  mbStopped = false;
  mbStopRequested = false;
}
#endif

} // namespace ORB_SLAM3
