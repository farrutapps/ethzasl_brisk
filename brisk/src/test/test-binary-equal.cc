/*
 Copyright (C) 2013  The Autonomous Systems Lab, ETH Zurich,
 Stefan Leutenegger and Simon Lynen.

 BRISK - Binary Robust Invariant Scalable Keypoints
 Reference implementation of
 [1] Stefan Leutenegger,Margarita Chli and Roland Siegwart, BRISK:
 Binary Robust Invariant Scalable Keypoints, in Proceedings of
 the IEEE International Conference on Computer Vision (ICCV2011).

 This file is part of BRISK.

 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.
 * Neither the name of the <organization> nor the
 names of its contributors may be used to endorse or promote products
 derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>

#include <brisk/brisk.h>
#include <brisk/internal/timer.h>
#if HAVE_GLOG
#include <glog/logging.h>
#else
#include <brisk/glog_replace.h>
#endif
#include <gtest/gtest.h>
#include <opencv2/opencv.hpp>
#include <opencv2/features2d/features2d.hpp>
#include <opencv2/nonfree/features2d.hpp>
#include <opencv2/nonfree/nonfree.hpp>

#include "./bench-ds.h"
#include "./image-io.h"
#include "./test-binary-equal.h"

#ifndef TEST
#define TEST(a, b) int Test_##a##_##b()
#endif

namespace brisk {

enum Parameters {
  doKeypointDetection = true,
  // Before switching this to false, you need to add a function which removes
  // keypoints that are too close to the border.
  doDescriptorComputation = true,
  // Stop on first image for which the verification fails.
  exitfirstfail = false,
  drawKeypoints = false,

  BRISK_absoluteThreshold = 20,
  BRISK_uniformityradius = 30,
  BRISK_octaves = 0,
  BRISK_maxNumKpt = 4294967296,
  BRISK_scaleestimation = true,
  BRISK_rotationestimation = true,
};

TEST(Brisk, Validation) {
  bool do_gtest_checks = true;
  RunValidation(do_gtest_checks);
}

bool RunValidation(bool do_gtest_checks) {
  std::string imagepath = "./src/test/test_data/";
  std::string datasetfilename = "brisk_verification_data.set";

  std::string datasetfullpath = imagepath + "/" + datasetfilename;

  std::cout << "Checking if there is a dataset at ..." << datasetfullpath;
  std::ifstream dataset_file_stream(datasetfullpath.c_str());
  bool have_dataset = dataset_file_stream.good();
  dataset_file_stream.close();

  std::cout << (have_dataset ? " True " : " False") << std::endl;

  std::vector<DatasetEntry> dataset;

  if (!have_dataset) {
    // Read the dataset.
    std::cout << "No dataset found at " << datasetfullpath
        << " will create one from the images in " << imagepath << std::endl;

    std::vector < std::string > imgpaths;
    bool doLexicalsort = true;
    std::vector < std::string > search_paths;
    search_paths.push_back(imagepath);
    brisk::Getfilelists(search_paths, doLexicalsort, "ppm", &imgpaths);

    // Make the dataset.
    std::cout << "Reading dataset path: " << imagepath << " got images: "
        << std::endl;
    for (std::vector<std::string>::iterator it = imgpaths.begin(), end =
        imgpaths.end(); it != end; ++it) {
      dataset.push_back(DatasetEntry());
      DatasetEntry& image = dataset.at(dataset.size() - 1);
      image.readImage(*it);
      std::cout << image.print();
    }

    // Run the pipeline.
    RunPipeline(dataset, datasetfullpath);

    // Save the dataset.
    std::cout << "Done handling images from " << imagepath << ". " << std::endl
        << "Now saving to " << datasetfullpath << std::endl << std::endl;

    std::ofstream ofs(std::string(datasetfullpath).c_str());
    serialization::Serialize(dataset, &ofs);

    std::cout << "Done. Now re-run to check against the dataset." << std::endl;
    if (do_gtest_checks) {
      EXPECT_TRUE(have_dataset) <<
          "No existing dataset found to verify against.";
    }
    return 0;
  } else {
    std::cout << "Dataset found at " << datasetfullpath << std::endl;

    // Deserialize the dataset.
    std::ifstream ifs(std::string(datasetfullpath).c_str());
    serialization::DeSerialize(&dataset, &ifs);

    std::vector<DatasetEntry> verifyds;
    verifyds = dataset;  // Intended deep copy.

    std::cout << "Loaded dataset:" << std::endl;
    int i = 0;
    for (std::vector<DatasetEntry>::const_iterator it = dataset.begin(), end =
        dataset.end(); it != end; ++it, ++i) {
      std::cout << i << ": " << it->print() << std::endl;
    }

    // Remove all processed data, only keep the images.
    for (std::vector<DatasetEntry>::iterator it = dataset.begin(), end = dataset
        .end(); it != end; ++it)
      it->clear_processed_data(doDescriptorComputation, doKeypointDetection);

    // Run the pipeline on the dataset.
    RunPipeline(dataset, datasetfullpath);

    // Run the verification.
    bool verificationOK = RunVerification(dataset, verifyds, do_gtest_checks);
    if (verificationOK) {
      std::cout << std::endl << "******* Verification success *******"
          << std::endl << std::endl;
    } else {
      std::cout << std::endl << "******* Verification failed *******"
          << std::endl << std::endl;
    }
    if (drawKeypoints) {
      Draw(dataset);
    }

    for (int i = 0; verificationOK && i < 20; ++i) {
      brisk::timing::DebugTimer timerOverall("BRISK overall");
      RunPipeline(dataset, datasetfullpath);
      timerOverall.Stop();
    }

    return !verificationOK;
  }
}

void RunPipeline(std::vector<DatasetEntry>& dataset,
                 const std::string& briskbasepath) {
  std::cout << "Running the pipeline..." << std::endl;

  // Detection.
  std::shared_ptr<cv::FeatureDetector> detector(
      new brisk::ScaleSpaceFeatureDetector<brisk::HarrisScoreCalculator>(
          BRISK_octaves, BRISK_uniformityradius, BRISK_absoluteThreshold));

  if (doKeypointDetection || dataset.at(0).GetKeyPoints().empty()) {
    for (std::vector<DatasetEntry>::iterator it = dataset.begin(), end = dataset
        .end(); it != end; ++it) {
      // Now you can query for the current image to add tags to timers etc.
      it->setThisAsCurrentEntry();

      brisk::timing::DebugTimer timerdetect(
          DatasetEntry::getCurrentEntry()->GetPath() + "_detect");
      detector->detect(*it->GetImgMutable(), *it->GetKeyPointsMutable());
      timerdetect.Stop();

      // Test userdata.
      Blob& blob = DatasetEntry::getCurrentEntry()->GetBlob("testImage");
      if (!blob.HasverificationData()) {
        blob.SetVerificationData(it->GetImage().data,
                                 it->GetImage().rows * it->GetImage().cols);
      } else {
        blob.SetCurrentData(it->GetImage().data,
                            it->GetImage().rows * it->GetImage().cols);
      }
    }
  }

  // Extraction.
  std::shared_ptr<cv::DescriptorExtractor> descriptorExtractor(
      new brisk::BriskDescriptorExtractor(BRISK_rotationestimation,
                                          BRISK_scaleestimation));
  if (doDescriptorComputation || dataset.at(0).GetDescriptors().rows == 0) {
    for (std::vector<DatasetEntry>::iterator it = dataset.begin(), end = dataset
        .end(); it != end; ++it) {
      it->setThisAsCurrentEntry();  //now you can query for the current image to add tags to timers etc.
      brisk::timing::Timer timerextract(
          DatasetEntry::getCurrentEntry()->GetPath() + "_extract");
      descriptorExtractor->compute(it->GetImage(), *it->GetKeyPointsMutable(),
                                   *it->GetDescriptorsMutable());
      timerextract.Stop();
    }
  }

  // Output timing.
  brisk::timing::Timing::Print(std::cout);
}

bool RunVerification(std::vector<DatasetEntry>& current_dataset,
                     std::vector<DatasetEntry>& verification_dataset,
                     bool do_gtest_checks) {
  CHECK_EQ(current_dataset.size(), verification_dataset.size())
        << "Failed on database number of entries";

  bool failed = false;
  //now go through every image
  for (std::vector<DatasetEntry>::iterator it_curr = current_dataset.begin(),
      it_verif = verification_dataset.begin(), end_curr = current_dataset.end(),
      end_verif = verification_dataset.end();
      it_curr != end_curr && it_verif != end_verif; ++it_curr, ++it_verif) {
    it_curr->setThisAsCurrentEntry();  //now you can query for the current image to add tags to timers etc.
    try {
      failed |= (*it_curr != *it_verif);
    } catch (std::exception& e) {
      failed = true;
      std::cout << "------" << std::endl << "Failed on image "
          << it_curr->GetPath() << std::endl << "* Error: " << e.what()
          << "------" << std::endl;
      if (exitfirstfail)
        CHECK(!failed);
    }
  }
  return !failed;
}

void Draw(std::vector<DatasetEntry>& dataset) {
  // Drawing.
  cv::namedWindow("Keypoints");
  for (std::vector<DatasetEntry>::iterator it = dataset.begin(), end = dataset
      .end(); it != end; ++it) {
    // Now you can query DatasetEntry::getCurrentImageName() for the current
    // image to add tags to timers etc.
    it->setThisAsCurrentEntry();
    cv::Mat out;
    cv::drawKeypoints(it->GetImage(), it->GetKeyPoints(), out,
                      cv::Scalar::all(-1),
                      cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS);
    cv::imshow("Keypoints", out);
    cv::waitKey();
  }
}

DatasetEntry* DatasetEntry::current_entry = NULL;
}  // namespace brisk


int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
