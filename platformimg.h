/*
 * Copyright (C) 2018 The Android Open Source Project
 * Copyright (C) 2018 EPAM Systems Inc.
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

#ifndef ANDROID_PLATFORM_DRM_GENERIC_H_
#define ANDROID_PLATFORM_DRM_GENERIC_H_

#include "drmdevice.h"
#include "platform.h"

#include <hardware/gralloc.h>
#include <hardware/gralloc1.h>
#include <map>

#include "img_gralloc_common_public.h"

namespace android {

typedef struct bo_descriptor {
    hwc_drm_bo_t bo;
    bool used;
    bool needFlush;
} bo_t;

class ImgImporter : public Importer {
 public:
  ImgImporter(DrmDevice *drm);
  ~ImgImporter() override;

  int Init();

  bool CanImportBuffer(buffer_handle_t /*handle*/) { return true; }
  int ImportBuffer(buffer_handle_t handle, hwc_drm_bo_t *bo) override;
  int ReleaseBuffer(hwc_drm_bo_t *bo) override;
  int ReleaseImgBuffer(hwc_drm_bo_t* bo);
  void FlushCache () override;

 private:
  uint32_t ConvertImgHalFormatToDrm(IMG_native_handle_t *img_hnd, hwc_drm_bo_t *bo);
  uint32_t ConvertAndroidHalFormatToDrm(int format);

  DrmDevice *drm_;

  std::map<unsigned long long, bo_t> cachedBo_;
  std::mutex cachedBoMutex_;
};
}

#endif
