/******************************************************************************
 * Copyright 2018 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the License);
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/
#include "modules/perception/traffic_light_recognition/recognition/classify.h"


#include "cyber/common/file.h"
#include "cyber/profiler/profiler.h"
#include "modules/perception/common/camera/common/util.h"
#include "modules/perception/common/inference/inference_factory.h"
#include "modules/perception/common/inference/model_util.h"
#include "modules/perception/common/inference/utils/resize.h"
#include "modules/perception/common/util.h"

namespace apollo {
namespace perception {
namespace trafficlight {

void ClassifyBySimple::Init(const ClassifyParam& model_config,
                            const int gpu_id) {
  AINFO << "Enter Classify init";
  net_inputs_.clear();
  net_outputs_.clear();
  AINFO << "clear success";
  net_inputs_ =
      inference::GetBlobNames(model_config.info().inputs());
  net_outputs_ =
      inference::GetBlobNames(model_config.info().outputs());

  AINFO << net_inputs_.size();
  AINFO << net_outputs_.size();

  for (auto name : net_inputs_) {
    AINFO << "net input blobs: " << name;
  }
  for (auto name : net_outputs_) {
    AINFO << "net output blobs: " << name;
  }

  std::string model_path = GetModelPath(model_config.info().name());

  std::string proto_file =
      GetModelFile(model_path, model_config.info().proto_file().file());
  AINFO << "proto_file " << proto_file;

  std::string weight_file =
      GetModelFile(model_path, model_config.info().weight_file().file());

  AINFO << "weight_file" << weight_file;

  const auto& model_type = model_config.info().framework();
  rt_net_.reset(inference::CreateInferenceByName(model_type, proto_file,
                                                 weight_file, net_outputs_,
                                                 net_inputs_, model_path));

  rt_net_->set_gpu_id(gpu_id);
  gpu_id_ = gpu_id;

  resize_height_ = model_config.classify_resize_height();
  resize_width_ = model_config.classify_resize_width();
  unknown_threshold_ = model_config.classify_threshold();
  mean_.reset(new base::Blob<float>(1, 3, 1, 1));
  float* p = mean_->mutable_cpu_data();
  if (model_config.is_bgr()) {
    data_provider_image_option_.target_color = base::Color::BGR;
    p[0] = model_config.mean_b();
    p[1] = model_config.mean_g();
    p[2] = model_config.mean_r();
  } else {
    data_provider_image_option_.target_color = base::Color::RGB;
    p[0] = model_config.mean_r();
    p[1] = model_config.mean_g();
    p[2] = model_config.mean_b();
  }
  scale_ = model_config.scale();

  std::vector<int> shape = {1, resize_height_, resize_width_, 3};
  mean_buffer_.reset(new base::Blob<float>(shape));

  std::map<std::string, std::vector<int>> input_reshape;
  inference::AddShape(&input_reshape, model_config.info().inputs());

  AINFO << "input_reshape: " << input_reshape[net_inputs_[0]][0] << ", "
        << input_reshape[net_inputs_[0]][1] << ", "
        << input_reshape[net_inputs_[0]][2] << ", "
        << input_reshape[net_inputs_[0]][3];

  if (!rt_net_->Init(input_reshape)) {
    AWARN << "net init fail.";
  }

  image_.reset(
      new base::Image8U(resize_height_, resize_width_, base::Color::BGR));
}

void ClassifyBySimple::Perform(const camera::TrafficLightFrame* frame,
                               std::vector<base::TrafficLightPtr>* lights) {
  if (cudaSetDevice(gpu_id_) != cudaSuccess) {
    AERROR << "Failed to set device to " << gpu_id_;
    return;
  }
  std::shared_ptr<base::Blob<uint8_t>> rectified_blob;

  auto input_blob_recog = rt_net_->get_blob(net_inputs_[0]);
  auto output_blob_recog = rt_net_->get_blob(net_outputs_[0]);

  for (base::TrafficLightPtr light : *lights) {
    if (!light->region.is_detected) {
      continue;
    }

    data_provider_image_option_.crop_roi = light->region.detection_roi;
    data_provider_image_option_.do_crop = true;
    data_provider_image_option_.target_color = base::Color::BGR;
    frame->data_provider->GetImage(data_provider_image_option_, image_.get());

    AINFO << "get img done";

    const float* mean = mean_.get()->cpu_data();
    inference::ResizeGPU(*image_, input_blob_recog,
                         frame->data_provider->src_width(), 0, mean[0], mean[1],
                         mean[2], true, scale_);

    AINFO << "resize gpu finish.";
    cudaDeviceSynchronize();

    PERF_BLOCK("traffic_light_recognition_inference")
    rt_net_->Infer();
    PERF_BLOCK_END

    cudaDeviceSynchronize();
    AINFO << "infer finish.";

    float* out_put_data = output_blob_recog->mutable_cpu_data();
    Prob2Color(out_put_data, unknown_threshold_, light);
  }
}

void ClassifyBySimple::Prob2Color(const float* out_put_data, float threshold,
                                  base::TrafficLightPtr light) {
  int max_color_id = 0;
  std::vector<base::TLColor> status_map = {
      base::TLColor::TL_BLACK, base::TLColor::TL_RED, base::TLColor::TL_YELLOW,
      base::TLColor::TL_GREEN};
  std::vector<std::string> name_map = {"Black", "Red", "Yellow", "Green"};
  std::vector<float> prob(out_put_data, out_put_data + status_map.size());
  auto max_prob = std::max_element(prob.begin(), prob.end());
  max_color_id = (*max_prob > threshold)
                     ? static_cast<int>(std::distance(prob.begin(), max_prob))
                     : 0;

  light->status.color = status_map[max_color_id];
  light->status.confidence = out_put_data[max_color_id];
  AINFO << "Light status recognized as " << name_map[max_color_id];
  AINFO << "Color Prob:";
  for (size_t j = 0; j < status_map.size(); j++) {
    AINFO << out_put_data[j];
  }
}

}  // namespace trafficlight
}  // namespace perception
}  // namespace apollo
