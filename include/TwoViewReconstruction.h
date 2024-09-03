/**
* This file is part of ORB-SLAM3
*
* Copyright (C) 2017-2021 Carlos Campos, Richard Elvira, Juan J. Gómez Rodríguez, José M.M. Montiel and Juan D. Tardós, University of Zaragoza.
* Copyright (C) 2014-2016 Raúl Mur-Artal, José M.M. Montiel and Juan D. Tardós, University of Zaragoza.
*
* ORB-SLAM3 is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
* License as published by the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* ORB-SLAM3 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
* the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with ORB-SLAM3.
* If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef TwoViewReconstruction_H
#define TwoViewReconstruction_H

using namespace std;

#include <opencv2/opencv.hpp>
#include <Eigen/Core>
#include <unordered_set>

#include <sophus/se3.hpp>

namespace ORB_SLAM3
{

    class TwoViewReconstruction
    {
        typedef pair<int,int> Match;

    public:
        EIGEN_MAKE_ALIGNED_OPERATOR_NEW
        // Fix the reference frame
        TwoViewReconstruction(const Eigen::Matrix3f& k, float sigma = 1.0, int iterations = 200);

        // Computes in parallel a fundamental matrix and a homography
        // Selects a model and tries to recover the motion and the structure from motion
        bool Reconstruct(const vector<cv::KeyPoint>& vKeys1, const vector<cv::KeyPoint>& vKeys2, const vector<int> &vMatches12,
                          Sophus::SE3f &T21, vector<cv::Point3f> &vP3D, vector<bool> &vbTriangulated);

    private:

        void FindHomography(vector<bool> &vbMatchesInliers, float &score, Eigen::Matrix3f &H21);
        void FindFundamental(vector<bool> &vbInliers, float &score, Eigen::Matrix3f &F21);

        Eigen::Matrix3f ComputeH21(const vector<cv::Point2f> &vP1, const vector<cv::Point2f> &vP2);
        Eigen::Matrix3f ComputeF21(const vector<cv::Point2f> &vP1, const vector<cv::Point2f> &vP2);

        float CheckHomography(const Eigen::Matrix3f &H21, const Eigen::Matrix3f &H12, vector<bool> &vbMatchesInliers, float sigma);

        float CheckFundamental(const Eigen::Matrix3f &F21, vector<bool> &vbMatchesInliers, float sigma);

        bool ReconstructF(vector<bool> &vbMatchesInliers, Eigen::Matrix3f &F21, Eigen::Matrix3f &K,
                          Sophus::SE3f &T21, vector<cv::Point3f> &vP3D, vector<bool> &vbTriangulated, float minParallax, int minTriangulated);

        bool ReconstructH(vector<bool> &vbMatchesInliers, Eigen::Matrix3f &H21, Eigen::Matrix3f &K,
                          Sophus::SE3f &T21, vector<cv::Point3f> &vP3D, vector<bool> &vbTriangulated, float minParallax, int minTriangulated);

        void Normalize(const vector<cv::KeyPoint> &vKeys, vector<cv::Point2f> &vNormalizedPoints, Eigen::Matrix3f &T);


        int CheckRT(const Eigen::Matrix3f &R, const Eigen::Vector3f &t, const vector<cv::KeyPoint> &vKeys1, const vector<cv::KeyPoint> &vKeys2,
                    const vector<Match> &vMatches12, vector<bool> &vbMatchesInliers,
                    const Eigen::Matrix3f &K, vector<cv::Point3f> &vP3D, float th2, vector<bool> &vbGood, float &parallax);

        void DecomposeE(const Eigen::Matrix3f &E, Eigen::Matrix3f &R1, Eigen::Matrix3f &R2, Eigen::Vector3f &t);


        // Keypoints from Reference Frame (Frame 1)
        vector<cv::KeyPoint> mvKeys1;

        // Keypoints from Current Frame (Frame 2)
        vector<cv::KeyPoint> mvKeys2;

        // Current Matches from Reference to Current
        vector<Match> mvMatches12;
        vector<bool> mvbMatched1;

        // Calibration
        Eigen::Matrix3f mK;

        // Standard Deviation and Variance
        float mSigma, mSigma2;

        // Ransac max iterations
        int mMaxIterations;

        // Ransac sets
        vector<vector<size_t> > mvSets;

    };

} //namespace ORB_SLAM

#endif // TwoViewReconstruction_H
