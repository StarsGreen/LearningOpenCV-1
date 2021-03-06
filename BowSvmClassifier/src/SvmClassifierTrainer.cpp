/*
 * SvmClassifierTrainer.cpp
 *
 *  Created on: Jul 14, 2017
 *      Author: renwei
 */

#include "Utility.h"
#include "SvmClassifierTrainer.h"

using namespace std;
using namespace cv;
using namespace cv::xfeatures2d;
using namespace cv::ml;

typedef std::chrono::high_resolution_clock Clock;

SvmClassifierTrainer::SvmClassifierTrainer()
{
}

SvmClassifierTrainer::SvmClassifierTrainer(
    const string& vocabularyFile,
    const string& descriptorsFile,
    const std::string& imgBasePath,
    const std::string& matcherDescriptorsFile,
    const string& classifierFilePrefix) :
    m_vocabularyFile(vocabularyFile),
    m_descriptorsFile(descriptorsFile),
    m_imgBasePath(imgBasePath),
    m_matcherDescriptorsFile(matcherDescriptorsFile),
    m_classifierFilePrefix(classifierFilePrefix),
    m_surfMinHessian(400)   // TODO: Expose m_cntBowClusters and m_surfMinHessian as configurable parameters.
{

}

SvmClassifierTrainer::~SvmClassifierTrainer()
{
}

void SvmClassifierTrainer::Reset(
    const std::string& vocabularyFile,
    const std::string& descriptorsFile,
    const std::string& imgBasePath,
    const std::string& matcherDescriptorsFile,
    const std::string& classifierFilePrefix)
{
    m_vocabularyFile = vocabularyFile;
    m_descriptorsFile = descriptorsFile;
    m_imgBasePath = imgBasePath;
    m_matcherDescriptorsFile = matcherDescriptorsFile;
    m_classifierFilePrefix = classifierFilePrefix;

    m_img2DescriptorsMap.clear();
    m_label2BowDescriptorsMap.clear();
}

bool SvmClassifierTrainer::ComputeBowDescriptors()
{
    // Load the vocabulary from the vocabulary file.
    Mat vocabulary;
    FileStorage fsVocabulary(m_vocabularyFile, FileStorage::READ);
    fsVocabulary["vocabulary"] >> vocabulary;
    cout << "[INFO]: Read the vocabulary with " << vocabulary.rows << " clusters from " << m_vocabularyFile
        << "." << endl;
    fsVocabulary.release();

    // Load the filenames of all the training images.
    FileStorage fsDescriptors(m_descriptorsFile, FileStorage::READ);
    FileNode img2LabelListNode = fsDescriptors["image_label_list"];
    if (img2LabelListNode.type() != FileNode::SEQ)
    {
        cerr << "[ERROR]: The list of image filenames with labels is not a sequence in " << m_descriptorsFile
            << "." << endl << endl;
        return false;
    }

    vector<pair<string, string> > imgFullFilename2LabelList;
    for (FileNodeIterator itNode = img2LabelListNode.begin(); itNode != img2LabelListNode.end(); ++itNode)
    {
        string imgFullFilename = (string)(*itNode++);
        string imgLabel = (string)(*itNode);
        imgFullFilename2LabelList.push_back(make_pair(imgFullFilename, imgLabel));
    }

    cout << "[INFO]: Read the filenames of " << imgFullFilename2LabelList.size() << " images with their labels from "
        << m_descriptorsFile << "." << endl;

    // Load the descriptors with the labels of all the training images.
    int imgIndex = 0;
    for (const auto& imgFullFilename2Label : imgFullFilename2LabelList)
    {
        string imgFullFilename = imgFullFilename2Label.first;
        string imgLabel = imgFullFilename2Label.second;

        string descriptorsKey = "descriptors_" + to_string(imgIndex++);

        Mat descriptors;
        fsDescriptors[descriptorsKey] >> descriptors;

        //cout << "[DEBUG]: image " << imgFullFilename << ": Read the label " << descInfo.label << " and "
        //    << descInfo.descriptors.rows << " descriptors from " << m_descriptorsFile << "." << endl;

        // Since two images with different labels may share the same name, we prefix the key with the image label.
        m_img2DescriptorsMap.insert(make_pair(imgLabel + "_" + imgFullFilename, descriptors));
    }

    cout << "[INFO]: Read the labels and descriptors of " << imgFullFilename2LabelList.size() << " images from "
        << m_descriptorsFile << "." << endl;

    fsDescriptors.release();

    // Create the DescriptorMatcher which is required for creating the BOWImgDescriptorMatcher.
    Ptr<DescriptorMatcher> descMatcher = DescriptorMatcher::create("BruteForce");

    // Create the BOWImgDescriptorMatcher.
    BOWImgDescriptorExtractor bowExtractor(descMatcher);

    // Set the vocabulary of the BOWImgDescriptorMatcher.
    bowExtractor.setVocabulary(vocabulary);

    auto tStart = Clock::now();

    // Compute the Bag-of-Words (BOW) descriptors from the original image descriptors.
    // A BOW descriptor, a.k.a. a presence vector, is a normalized histogram of vocabulary words
    // encountered in the image. Note that the BOW descriptors are stored in a map with
    // the image label as the key.
    for (const auto& imgFullFilename2Label : imgFullFilename2LabelList)
    {
        string imgFullFilename = imgFullFilename2Label.first;
        string imgLabel = imgFullFilename2Label.second;
        string imgMapKey = imgLabel + "_" + imgFullFilename;

        Mat bowDescriptor;
        bowExtractor.compute(m_img2DescriptorsMap[imgMapKey], bowDescriptor);

        //cout << "[DEBUG]: image "  << imgFullFilename << ": Compute the BOW descriptor with "
        //    << bowDescriptor.rows << " rows and " << bowDescriptor.cols << " columns." << endl;

        auto itMap = m_label2BowDescriptorsMap.find(imgLabel);
        if (itMap == m_label2BowDescriptorsMap.end())
        {
            m_label2BowDescriptorsMap.insert(make_pair(imgLabel, bowDescriptor));
        }
        else
        {
            itMap->second.push_back(bowDescriptor);
        }

        //cout << "[DEBUG]: image "  << imgFullFilename << ": Add the BOW descriptor to class " << imgLabel
        //    << "." << endl;
    }

    auto tEnd = Clock::now();

    cout << "[INFO]: Compute the BOW descriptors of " << imgFullFilename2LabelList.size() << " images in "
        << chrono::duration_cast<chrono::milliseconds>(tEnd - tStart).count()
        << " ms." << endl;

    return true;
}

// Train and save the 1-vs-all SVMs for each class. Note that one class corresponds to one image label.
void SvmClassifierTrainer::TrainAndSaveSvms()
{
    auto tStart = Clock::now();

    for (const auto& labelledBowDescriptors : m_label2BowDescriptorsMap)
    {
        string className = labelledBowDescriptors.first;
        Mat classBowDescriptors = labelledBowDescriptors.second;

        // First we add the BOW descriptors of the current class to the training BOW descriptors and
        // an all-one vector to the training label vector.
        Mat trainingBowDescriptors = classBowDescriptors;
        Mat trainingLabels = Mat::ones(classBowDescriptors.rows, 1, CV_32S);

        // Then we add the BOW descriptors of the remaining classes to the training BOW descriptors
        // and an all-zero vector to the training label vector.
        for (const auto& nonClassLabelledBowDescriptors : m_label2BowDescriptorsMap)
        {
            if (nonClassLabelledBowDescriptors.first != className)
            {
                Mat nonClassBowDescriptors = nonClassLabelledBowDescriptors.second;
                trainingBowDescriptors.push_back(nonClassBowDescriptors);
                trainingLabels.push_back(Mat::zeros(nonClassBowDescriptors.rows, 1, CV_32S));
            }
        }

        if (trainingBowDescriptors.rows == 0)
        {
            cout << "[WARNING]: Class " << className << " has ZERO BOW descriptor!" << endl << endl;
            continue;
        }

        //cout << "[DEBUG]: BOW descriptor element type of class " << className << " = "
        //    << Utility::CvType2Str(trainingBowDescriptors.type()) << "." << endl;

        trainingBowDescriptors.convertTo(trainingBowDescriptors, CV_32F);
        //cout << "[DEBUG]: Covert the element type of the BOW descriptor of class " << className
        //    << " to CV_32F in case that it is not." << endl;

        // Create the SVM classifier and train it using trainingBowDescriptors.
        Ptr<SVM> classifier = SVM::create();
        classifier->train(trainingBowDescriptors, ROW_SAMPLE, trainingLabels);
        cout << "[INFO]: Train the classifier of class " << className << "." << endl;

        // Save the SVM classifier to a file.
        string classifierFilename = m_classifierFilePrefix + "_" + className + ".yml";
        classifier->save(classifierFilename);
    }

    auto tEnd = Clock::now();

    cout << "[INFO]: Train the 1-vs-all SVM classifiers of " << m_label2BowDescriptorsMap.size() << " classes in "
        << chrono::duration_cast<chrono::milliseconds>(tEnd - tStart).count()
        << " ms." << endl;
}

void SvmClassifierTrainer::ComputeAndSaveMatcherDescriptors()
{
    // We assume that each label has only one image for the FLANN-based matcher.
    vector<pair<string, string> > matcherImgWithLabels;
    Utility::GetImagesWithLabels(m_imgBasePath, matcherImgWithLabels);

    FileStorage fs(m_matcherDescriptorsFile, FileStorage::WRITE);

    fs << "image_label_list" << "[";
    for (const auto& labelledImg : matcherImgWithLabels)
    {
        // We write the image filename first and then its label.
        fs << labelledImg.second << labelledImg.first;
    }
    fs << "]";  // End of image_label_list
    cout << "[INFO]: Write the filenames of " << matcherImgWithLabels.size() << " images with their labels to file "
        << m_matcherDescriptorsFile << " for the FLANN-based matcher." << endl;

    Ptr<SurfFeatureDetector> detector = SURF::create(m_surfMinHessian);

    int imgIndex = 0;
    for (const auto& labelledImg : matcherImgWithLabels)
    {
        string imgLabel = labelledImg.first;
        string imgFile = labelledImg.second;
        string imgFullPath = m_imgBasePath + "/" + imgLabel + "/" + imgFile;

        Mat img = imread(imgFullPath);
        vector<KeyPoint> oneImgKeypoints;
        Mat oneImgDescriptors;

        auto tStart = Clock::now();
        detector->detectAndCompute(img, noArray(), oneImgKeypoints, oneImgDescriptors);
        auto tEnd = Clock::now();

        cout << "[INFO]: Computed the SURF descriptors of " << imgLabel << " for the FLANN-based matcher in "
            << chrono::duration_cast<chrono::milliseconds>(tEnd - tStart).count() << " ms." << endl;

        // Key names must start with a letter or '_'. Since the image filename may start with a non-letter,
        // e.g., a digit, we don't use the image filename as the key name.
        fs << "descriptors_" + to_string(imgIndex++) << oneImgDescriptors;
        cout << "[INFO]: Write " << oneImgDescriptors.rows << " descriptors of image " << imgFile
            << " with label " << imgLabel << " to file " << m_matcherDescriptorsFile << "." << endl;
    }

    fs.release();
}

void SvmClassifierTrainer::Train()
{
    if(ComputeBowDescriptors())
    {
        TrainAndSaveSvms();
        ComputeAndSaveMatcherDescriptors();
    }
}
