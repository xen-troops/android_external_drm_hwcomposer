/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "BackendXenDrmDu.h"

#include "BackendManager.h"

namespace android {

HWC2::Error BackendXenDrmDu::ValidateDisplay(DrmHwcTwo::HwcDisplay *display,
                                    uint32_t *num_types,
                                    uint32_t */*num_requests*/) {

  auto& layers = display->layers();
  if (layers.size() > 1) {
    for (std::pair<const hwc2_layer_t, DrmHwcTwo::HwcLayer> &l : display->layers()) {
      l.second.set_validated_type(HWC2::Composition::Client);
      ++*num_types;
    }
    return HWC2::Error::HasChanges;
  }
  layers[0].set_validated_type(HWC2::Composition::Device);
  return HWC2::Error::HasChanges;
}

REGISTER_BACKEND("xendrm-du", BackendXenDrmDu);

}  // namespace android