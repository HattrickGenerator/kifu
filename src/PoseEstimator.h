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
        setSource(points, normals, 1);
    }

    void setSource(std::vector<Vector3f> points, std::vector<Vector3f> normals, unsigned int downsample)
    {
        if (downsample == 1)
        {
            m_source.points = points;
            m_source.normals = normals;

            m_source.normalsValid = std::vector<bool>(normals.size(), true);
            m_source.pointsValid = std::vector<bool>(points.size(), true);
        }
        else
        {
            int nPoints = std::min(points.size(), normals.size()) / downsample;
            m_source.points = std::vector<Vector3f>(nPoints);
            m_source.normals = std::vector<Vector3f>(nPoints);
            for (int i = 0; i < nPoints; ++i)
            {
                m_source.points[i] = points[i*downsample];
                m_source.normals[i] = normals[i*downsample];
            }

            m_source.normalsValid = std::vector<bool>(nPoints, true);
            m_source.pointsValid = std::vector<bool>(nPoints, true);
        }
    }


    void printPoints()
    {
        std::cout << "first 10 points " << std::endl;
        for(int i =0; i<std::min<int>(10, m_target.points.size());++i)
        {
            std::cout << m_target.points[i].transpose() << std::endl;
        }
    }
    virtual Matrix4f estimatePose(Matrix4f = Matrix4f::Identity()) = 0;

    static std::vector<Vector3f> pruneVector(std::vector<Vector3f>& input, std::vector<bool>& validity)
    {
        assert((input.size() == validity.size()));

        std::vector<Vector3f> output;
        for (uint i = 0; i < input.size(); ++i)
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
    PointCloud m_target;
    PointCloud m_source;
    int m_nIter = 10;
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
            for (uint j = 0; j < transformedPoints.size(); j++)
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
