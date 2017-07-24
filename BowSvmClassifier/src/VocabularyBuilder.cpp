/*
 * VocabularyBuilder.cpp
 *
 *  Created on: Jul 10, 2017
 *      Author: renwei
 */

#include "Utility.h"
#include "VocabularyBuilder.h"

using namespace std;
using namespace cv;
using namespace cv::xfeatures2d;

typedef std::chrono::high_resolution_clock Clock;

VocabularyBuilder::VocabularyBuilder() :
    m_cntBowClusters(0),
    m_surfMinHessian(0)
{
}

VocabularyBuilder::VocabularyBuilder(
    const string& imgBasePath,
    const std::string& descriptorsFile,
    const string& vocabularyFile) :
    m_imgBasePath(imgBasePath),
    m_descriptorsFile(descriptorsFile),
    m_vocabularyFile(vocabularyFile),
    m_cntBowClusters(1000), // TODO: Expose m_cntBowClusters and m_surfMinHessian as configurable parameters.
    m_surfMinHessian(400)
{

}

VocabularyBuilder::~VocabularyBuilder()
{
}

void VocabularyBuilder::Reset(
    const string& imgBasePath,
    const std::string& descriptorsFile,
    const string& vocabularyFile)
{
    m_imgBasePath = imgBasePath;
    m_descriptorsFile = descriptorsFile;
    m_vocabularyFile = vocabularyFile;

    m_descriptors.release();
    m_vocabulary.release();
}

void VocabularyBuilder::ComputeDescriptors(OutputArray descriptors)
{
    vector<pair<string, string> > imgWithLabels;
    Utility::GetImagesWithLabels(m_imgBasePath, imgWithLabels);
    cout << "[INFO]: Computing the SURF descriptors of " << imgWithLabels.size() << " images." << endl;

    Ptr<SurfFeatureDetector> detector = SURF::create(m_surfMinHessian);
    vector<KeyPoint> imgKeypoints;
    Mat imgDescriptors;
    FileStorage fs(m_descriptorsFile, FileStorage::WRITE);

    auto tStart = Clock::now();

    vector<string> allImgs;
    for (auto& labelledImg : imgWithLabels)
    {
        string label = labelledImg.first;
        string imgFile = labelledImg.second;
        string imgFullPath = m_imgBasePath + "/" + label + "/" + imgFile;

        allImgs.push_back(imgFile);

        Mat img = imread(imgFullPath);
        detector->detectAndCompute(img, noArray(), imgKeypoints, imgDescriptors);

        // For OpenCV FileStorage, key names may only contain alphanumeric characters [a-zA-Z0-9],
        // '-', '_' and ' '. Unfortunately key names may not contain '.'.
        size_t dotPos = imgFile.find_last_of('.');
        string imgFilename = imgFile.substr(0, dotPos);
        string imgFileExtension = imgFile.substr(dotPos + 1);

        // Key names must start with a letter or '_'. Since the image filename may start with a non-letter,
        // e.g., a digit, we have to put it after those prefixes.
        // TODO: Define a struct with the fields name, label, descriptors, and its write function so that
        // each struct corresponding to one image can be directly written into the file via FileStorage.
        fs << "name_" + imgFilename << imgFile;
        fs << string("label_" + imgFilename) << label;
        fs << string("descriptors_" + imgFilename) << imgDescriptors;
        cout << "[INFO]: Write " << imgDescriptors.rows << " descriptors of image " << imgFile
            << " with label " << label << " to file " << m_descriptorsFile << "." << endl;

        m_descriptors.push_back(imgDescriptors);
    }

    auto tEnd = Clock::now();

    cout << "[INFO]: Get " << m_descriptors.rows << " total descriptors in "
        << chrono::duration_cast<chrono::milliseconds>(tEnd - tStart).count()
        << " milliseconds." << endl;

    fs << "imagefile_list" << allImgs;
    cout << "[INFO]: Write the filenames of " << allImgs.size() << " images to file "
        << m_descriptorsFile << "." << endl;

    fs.release();

    if (descriptors.needed())
    {
        m_descriptors.copyTo(descriptors);
    }
}

void VocabularyBuilder::BuildVocabulary(OutputArray vocabulary)
{
    cout << "[INFO]: Building the vocabulary." << endl;

    BOWKMeansTrainer bowTrainer(m_cntBowClusters);

    auto tStart = Clock::now();

    bowTrainer.add(m_descriptors);
    m_vocabulary = bowTrainer.cluster();

    auto tEnd = Clock::now();

    cout << "[INFO]: Build the vocabulary in "
        << chrono::duration_cast<chrono::milliseconds>(tEnd - tStart).count()
        << " milliseconds." << endl;

    FileStorage fs(m_vocabularyFile, FileStorage::WRITE);
    // TODO: Write the count of clusters, the descriptor type (i.e., "SURF"), and the descriptor
    // parameter value (e.g., m_surfMinHessian) to the vocabulary file.
    fs << "vocabulary" << m_vocabulary;

    cout << "[INFO]: Write the vocabulary with " << m_vocabulary.rows << " clusters to file "
        << m_vocabularyFile << "." << endl;

    fs.release();

    if (vocabulary.needed())
    {
        m_vocabulary.copyTo(vocabulary);
    }
}