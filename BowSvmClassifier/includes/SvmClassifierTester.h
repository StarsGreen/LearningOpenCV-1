/*
 * SvmClassifierTester.h
 *
 *  Created on: Jul 10, 2017
 *      Author: renwei
 */

#ifndef INCLUDES_SVMCLASSIFIERTESTER_H_
#define INCLUDES_SVMCLASSIFIERTESTER_H_

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <chrono>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/xfeatures2d.hpp>
#include <opencv2/ml.hpp>

struct ClassifierResult
{
    std::string expectedClass;
    std::string evaluatedClass;
    std::map<std::string, float> class2ScoresMap;

    ClassifierResult()
    {
    }

    // Write serialization for this class
    void write(cv::FileStorage& fs) const
    {
        fs << "{" << "expectedClass" << expectedClass;
        fs << "evaluatedClass" << evaluatedClass;

        fs << "class2ScoresMap" << "{";
        for (const auto& classScore : class2ScoresMap)
        {
            fs << classScore.first << classScore.second;
        }

        fs << "}";  // End of class2ScoresMap.
        fs << "}";  // End of ClassifierResult.
    }

    // Read de-serialization for this class
    void read(const cv::FileNode& node)
    {
        expectedClass = (std::string)(node["expectedClass"]);
        evaluatedClass = (std::string)(node["evaluatedClass"]);

        class2ScoresMap.clear();

        cv::FileNode mapNode = node["class2ScoresMap"];
        for (auto itMapNode = mapNode.begin(); itMapNode != mapNode.end(); ++itMapNode)
        {
            cv::FileNode item = *itMapNode;
            std::string className = item.name();
            float classScore = (float)item;
            class2ScoresMap.insert(std::make_pair(className, classScore));
        }
    }
};

class SvmClassifierTester
{
private:

    int m_surfMinHessian;
    std::string m_vocabularyFile;
    std::string m_classifierFilePrefix;
    std::string m_resultFile;

    std::map<std::string, cv::Mat> m_img2BowDescriptorMap;
    std::map<std::string, ClassifierResult> m_img2ClassifierResultMap;
    std::map<std::string, cv::Ptr<cv::ml::SVM> > m_class2SvmMap;

    cv::Ptr<cv::xfeatures2d::SurfFeatureDetector> m_detector;
    cv::Ptr<cv::DescriptorMatcher> m_descMatcher;
    cv::Ptr<cv::BOWImgDescriptorExtractor> m_bowExtractor;

    SvmClassifierTester();

    bool ComputeBowDescriptor(
        const std::string& img2ClassifierResultMapKey,
        const cv::Mat& img);
    bool EvaluateOneImgInternal(
        const std::string& img2ClassifierResultMapKey,
        const cv::Mat& img);

    void WriteResultsToFile();

public:

    SvmClassifierTester(
        const std::string& vocabularyFile,
        const std::string& classifierFilePrefix,
        const std::string& resultFile);
    ~SvmClassifierTester();

    bool InitBowImgDescriptorExtractor();

    bool InitSvmClassifiers();

    void Reset(
        const std::string& vocabularyFile,
        const std::string& classifierFilePrefix,
        const std::string& resultFile);

    void EvaluateOneImg(
        const std::string& imgFullFilename,
        const std::string& expectedClass);
    void EvaluateImgs(const std::string& imgBasePath);
};

#endif /* INCLUDES_SVMCLASSIFIERTESTER_H_ */