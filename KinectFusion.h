#pragma once

#include <string>
#include <assert.h>
#include "Eigen.h"
#include "VirtualSensor.h"
#include "NearestNeighbor.h"

#include <opencv4/opencv2/core.hpp>
#include <opencv4/opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

struct PointCloud
{
    std::vector<Vector3f> points;
    std::vector<bool> pointsValid;
    std::vector<Vector3f> normals;
    std::vector<bool> normalsValid;
};

// compute surface and normal maps
class SurfaceMeasurer
{
public:
    SurfaceMeasurer(Eigen::Matrix3f DepthIntrinsics, unsigned int DepthImageHeight, unsigned int DepthImageWidth)
        : m_DepthIntrinsics(DepthIntrinsics),
          m_DepthImageHeight(DepthImageHeight),
          m_DepthImageWidth(DepthImageWidth)
    {}

    void registerInput(float* depthMap)
    {
        m_rawDepthMap = depthMap;
    }

    void smoothInput()
    {
        cv::Mat rawDepthMap = cv::Mat{static_cast<int>(m_DepthImageHeight), static_cast<int>(m_DepthImageWidth), CV_32F, m_rawDepthMap};
        cv::Mat rawDepthMapCopy = rawDepthMap.clone();
        cv::bilateralFilter(rawDepthMapCopy, rawDepthMap, 5, 10, 10);

    }
    void process()
    {
        computeVertexAndNormalMap();
        std::cout << "size" << m_vertexMap.size() << std::endl;
    }

    void printDepthMap()
    {
        cv::Mat rawDepthMap = cv::Mat{static_cast<int>(m_DepthImageHeight), static_cast<int>(m_DepthImageWidth), CV_32F, m_rawDepthMap};
        std::cout << rawDepthMap;
    }

    void displayDepthMap()
    {
        // TODO
        cv::Mat rawDepthMap = cv::Mat{static_cast<int>(m_DepthImageHeight), static_cast<int>(m_DepthImageWidth), CV_32F, m_rawDepthMap};
        cv::Mat normDepthMap;
        //cv::patchNaNs(rawDepthMap);
        std::cout << cv::checkRange(rawDepthMap);
        cv::normalize(rawDepthMap, normDepthMap, 0, 1, cv::NORM_MINMAX);
        std::cout << cv::checkRange(normDepthMap);
        //std::cout << normDepthMap;
        std::string windowName = "rawDepthMap";
        cv::namedWindow(windowName, cv::WINDOW_AUTOSIZE);

        cv::Mat test = cv::Mat::zeros(100,100,CV_32F);
        cv::imshow(windowName, rawDepthMap);
        cv::waitKey(0);
    }

    PointCloud getPointCloud()
    {
        return PointCloud{m_vertexMap, m_vertexValidityMap, m_normalMap, m_normalValidityMap};
    }


private:
    void computeVertexAndNormalMap()
    {
        m_vertexMap.reserve(m_DepthImageHeight*m_DepthImageWidth);
        m_vertexValidityMap.reserve(m_DepthImageHeight*m_DepthImageWidth);

        m_normalMap = std::vector<Vector3f>(m_DepthImageHeight*m_DepthImageWidth);
        m_normalValidityMap = std::vector<bool>(m_DepthImageHeight*m_DepthImageWidth);

        //#pragma omp parallel for

        float fovX = m_DepthIntrinsics(0, 0);
        float fovY = m_DepthIntrinsics(1, 1);
        float cX = m_DepthIntrinsics(0, 2);
        float cY = m_DepthIntrinsics(1, 2);
        for(int y = 0; y < m_DepthImageHeight; ++y)
        {
            for(int x = 0; x < m_DepthImageWidth; ++x)
            {
                unsigned int idx = y*m_DepthImageWidth + x;
                const float depth = m_rawDepthMap[idx];
                if (depth == MINF || depth == NAN)
                {
                    m_vertexMap.push_back(Vector3f(MINF, MINF, MINF));
                    m_vertexValidityMap.push_back(false);
                }
                else
                {
                    // backproject to camera space
                    m_vertexMap.push_back(Vector3f((x - cX) / fovX * depth, (y - cY) / fovY * depth, depth));
                    m_vertexValidityMap.push_back(true);
                }

            }
        }
        const float maxDistHalve = 0.05f;
        for(int y = 1; y < m_DepthImageHeight-1; ++y)
        {
            for(int x = 1; x < m_DepthImageWidth-1; ++x)
            {
                unsigned int idx = y*m_DepthImageWidth + x;
                const float du = 0.5f * (m_rawDepthMap[idx + 1] - m_rawDepthMap[idx - 1]);
                const float dv = 0.5f * (m_rawDepthMap[idx + m_DepthImageWidth] - m_rawDepthMap[idx - m_DepthImageWidth]);
                if (!std::isfinite(du) || !std::isfinite(dv) || std::abs(du) > maxDistHalve || std::abs(dv) > maxDistHalve)
                {
                    m_normalMap[idx] = Vector3f(MINF, MINF, MINF);
                    m_normalValidityMap[idx] = false;
                }
                else
                {
                    m_normalMap[idx] = Vector3f(du, dv, -1);
                    m_normalMap[idx].normalize();
                    m_normalValidityMap[idx] = true;
                }
            }
        }
        // edge regions
        for (int x = 0; x < m_DepthImageWidth; ++x) {
            m_normalMap[x] = Vector3f(MINF, MINF, MINF);
            m_normalMap[x + (m_DepthImageHeight - 1) * m_DepthImageWidth] = Vector3f(MINF, MINF, MINF);
        }
        for (int y = 0; y < m_DepthImageHeight; ++y) {
            m_normalMap[y * m_DepthImageWidth] = Vector3f(MINF, MINF, MINF);
            m_normalMap[(m_DepthImageWidth - 1) + y * m_DepthImageWidth] = Vector3f(MINF, MINF, MINF);
        }
    }


    Matrix3f m_DepthIntrinsics;
    unsigned int m_DepthImageHeight;
    unsigned int m_DepthImageWidth;
    float* m_rawDepthMap;
    std::vector<Vector3f> m_vertexMap;
    std::vector<bool> m_vertexValidityMap;
    std::vector<Vector3f> m_normalMap;
    std::vector<bool> m_normalValidityMap;

};

class PoseEstimator
{
public:
    PoseEstimator()
    {}

    void setTarget(PointCloud& input)
    {
        m_target = input;
    }
    void setSource(PointCloud& input)
    {
        m_source = input;
    }
    void setTarget(std::vector<Vector3f> points, std::vector<Vector3f> normals)
    {
        m_target.points = points;
        m_target.normals = normals;

        m_target.normalsValid = std::vector<bool>(normals.size(), true);
        m_target.pointsValid = std::vector<bool>(points.size(), true);

    }
    void setSource(std::vector<Vector3f> points, std::vector<Vector3f> normals)
    {
        m_source.points = points;
        m_source.normals = normals;

        m_source.normalsValid = std::vector<bool>(normals.size(), true);
        m_source.pointsValid = std::vector<bool>(points.size(), true);
    }


    void printPoints()
    {
        std::cout << "first 10 points " << std::endl;
        for(int i =0; i<std::min<int>(10, m_target.points.size());++i)
        {
            std::cout << m_target.points[i].transpose() << std::endl;
        }
    }
    virtual Matrix4f estimatePose(Matrix4f) = 0;

    static std::vector<Vector3f> pruneVector(std::vector<Vector3f>& input, std::vector<bool>& validity)
    {
        assert((input.size() == validity.size()));

        std::vector<Vector3f> output;
        for (int i = 0; i < input.size(); ++i)
        {
            if(validity[i])
            {
                output.push_back(input[i]);
            }
        }
        return output;
    }
    static std::vector<Vector3f> transformPoint(const std::vector<Vector3f>& input, const Matrix4f& pose)
    {
        std::vector<Vector3f> output;
        output.reserve(input.size());

        const auto rotation = pose.block(0, 0, 3, 3);
        const auto translation = pose.block(0, 3, 3, 1);

        for (const auto& point : input) {
            output.push_back(rotation * point + translation);
        }

        return output;
    }
    static std::vector<Vector3f> transformNormal(const std::vector<Vector3f>& input, const Matrix4f& pose)
    {
        std::vector<Vector3f> output;
        output.reserve(input.size());

        const auto rotation = pose.block(0, 0, 3, 3);

        for (const auto& normal : input) {
            output.push_back(rotation.inverse().transpose() * normal);
        }

        return output;
    }
protected:
    // missing normals
    PointCloud m_target;
    PointCloud m_source;
    int m_nIter;
};

class NearestNeighborPoseEstimator : public PoseEstimator
{
public:
    NearestNeighborPoseEstimator()
        : m_nearestNeighborSearch{ std::make_unique<NearestNeighborSearchFlann>()}
    {}

    virtual Matrix4f estimatePose(Matrix4f initialPose = Matrix4f::Identity()) override
    {
        // Build the index of the FLANN tree (for fast nearest neighbor lookup).
        m_nearestNeighborSearch->buildIndex(m_target.points);

        // The initial estimate can be given as an argument.
        Matrix4f estimatedPose = initialPose;

        for (int i = 0; i < m_nIter; ++i) {
            auto transformedPoints = transformPoint(m_source.points, estimatedPose);
            auto transformedNormals = transformNormal(m_source.normals, estimatedPose);
            auto matches = m_nearestNeighborSearch->queryMatches(transformedPoints);

            // TODO
            // pruneCorrespondences(transformedNormals, m_target.getNormals(), matches);

            std::vector<Vector3f> sourcePoints;
            std::vector<Vector3f> targetPoints;

            // Add all matches to the sourcePoints and targetPoints vectors,
            // so thath sourcePoints[i] matches targetPoints[i].
            for (int j = 0; j < transformedPoints.size(); j++)
            {
                const auto& match = matches[j];
                if (match.idx >= 0) {
                    sourcePoints.push_back(transformedPoints[j]);
                    targetPoints.push_back(m_target.points[match.idx]);
                }
            }

            estimatedPose = solvePointToPlane(sourcePoints, targetPoints, m_target.normals) * estimatedPose;
        }

        return estimatedPose;

    }

private:
    Matrix4f solvePointToPlane(const std::vector<Vector3f>& sourcePoints, const std::vector<Vector3f>& targetPoints, const std::vector<Vector3f>& targetNormals)
    {
        const unsigned nPoints = sourcePoints.size();

        // Build the system
        MatrixXf A = MatrixXf::Zero(4 * nPoints, 6);
        VectorXf b = VectorXf::Zero(4 * nPoints);

        for (unsigned i = 0; i < nPoints; i++) {
            const auto& s = sourcePoints[i];
            const auto& d = targetPoints[i];
            const auto& n = targetNormals[i];

            // Add the point-to-plane constraints to the system
            b[i] = n.dot(d) - n.dot(s);
            A.block<1,3>(i,0) << n[2]*s[1] - n[1]*s[2],
                                 n[0]*s[2] - n[2]*s[0],
                                 n[1]*s[0] - n[0]*s[1];
            A.block<1,3>(i,3) = n;
            // Add the point-to-point constraints to the system
            // for x-coords
            b[nPoints+i] = d[0] - s[0];
            A.block<1,6>(nPoints+i,0) << 0, s[2], -s[1], 1, 0, 0;

            // for y-coords
            b[2*nPoints+i] = d[1] - s[1];
            A.block<1,6>(2*nPoints+i,0) << -s[2], 0, s[0], 0, 1, 0;

            // for z-coords
            b[3*nPoints+i] = d[2] - s[2];
            A.block<1,6>(3*nPoints+i,0) << s[1], -s[0], 0, 0, 0, 1;
        }

        // Solve the system
        VectorXf x(6);
        JacobiSVD<MatrixXf> svd(A, ComputeThinU | ComputeThinV);

        x = svd.solve(b);

        float alpha = x(0), beta = x(1), gamma = x(2);

        // Build the pose matrix
        Matrix3f rotation = AngleAxisf(alpha, Vector3f::UnitX()).toRotationMatrix() *
                            AngleAxisf(beta, Vector3f::UnitY()).toRotationMatrix() *
                            AngleAxisf(gamma, Vector3f::UnitZ()).toRotationMatrix();

        Vector3f translation = x.tail(3);

        // Build the pose matrix using the rotation and translation matrices
        Matrix4f estimatedPose = Matrix4f::Identity();
        estimatedPose.block<3,3>(0,0) = rotation;
        estimatedPose.block<3,1>(0,3) = translation;

        return estimatedPose;
    }
    std::unique_ptr<NearestNeighborSearch> m_nearestNeighborSearch;
};



class SurfaceReconstructor
{

};

class SurfacePredictor
{

};

typedef VirtualSensor InputType;
//template<class InputType>
class KiFuModel
{
public:
    KiFuModel(InputType& InputHandle)
        : m_InputHandle(&InputHandle)
    {
        m_InputHandle->processNextFrame();
        m_SurfaceMeasurer = new SurfaceMeasurer(m_InputHandle->getDepthIntrinsics(),
                                                m_InputHandle->getDepthImageHeight(),
                                                m_InputHandle->getDepthImageWidth());
        m_SurfaceMeasurer->registerInput(m_InputHandle->getDepth());
        //m_SurfaceMeasurer->smoothInput();
        //m_SurfaceMeasurer->displayDepthMap();
        m_SurfaceMeasurer->process();

        PointCloud Frame0 = m_SurfaceMeasurer->getPointCloud();

        std::vector<bool> pointsAndNormalsValid;
        pointsAndNormalsValid.reserve( Frame0.pointsValid.size() );

        std::transform(Frame0.pointsValid.begin(), Frame0.pointsValid.end(), Frame0.normalsValid.begin(),
            std::back_inserter(pointsAndNormalsValid), std::logical_and<>());


        m_PoseEstimator = new NearestNeighborPoseEstimator();
        m_PoseEstimator->setTarget(PoseEstimator::pruneVector(Frame0.points, pointsAndNormalsValid),
                                   PoseEstimator::pruneVector(Frame0.normals, pointsAndNormalsValid));
        m_PoseEstimator->printPoints();
    }


    bool processNextFrame()
    {
        if(!m_InputHandle->processNextFrame())
        {
            return true;
        }
        m_SurfaceMeasurer->registerInput(m_InputHandle->getDepth());
        m_SurfaceMeasurer->process();

        PointCloud nextFrame = m_SurfaceMeasurer->getPointCloud();

        std::vector<bool> pointsAndNormalsValid;
        pointsAndNormalsValid.reserve( nextFrame.pointsValid.size() );

        std::transform(nextFrame.pointsValid.begin(), nextFrame.pointsValid.end(), nextFrame.normalsValid.begin(),
            std::back_inserter(pointsAndNormalsValid), std::logical_and<>());


        m_PoseEstimator = new NearestNeighborPoseEstimator();
        m_PoseEstimator->setSource(PoseEstimator::pruneVector(nextFrame.points, pointsAndNormalsValid),
                                   PoseEstimator::pruneVector(nextFrame.normals, pointsAndNormalsValid));


        return false;
    }


private:
    SurfaceMeasurer* m_SurfaceMeasurer;
    PoseEstimator* m_PoseEstimator;
    SurfaceReconstructor m_SurfaceReconstructor;
    SurfacePredictor m_SurfacePredictor;

    InputType* m_InputHandle;
    std::string param;
    float* tsdf;

};

