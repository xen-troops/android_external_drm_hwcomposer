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

#define LOG_TAG "hwc-platform-img"

#include "platform.h"
#include "platformimg.h"

#include <drm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cutils/log.h>
#include <hardware/gralloc1.h>

namespace android {

#define IMG_HW_ALIGN    64
	/* macro for alignment */
#define ALIGN_ROUND_UP(X, Y)  (((X)+(Y)-1) & ~((Y)-1))

#define HAL_PIXEL_FORMAT_VENDOR_EXT(fmt) (0x100 | (fmt & 0xF))
#define HAL_PIXEL_FORMAT_BGRX_8888   HAL_PIXEL_FORMAT_VENDOR_EXT(1)


#ifdef USE_IMG_IMPORTER
// static
Importer *Importer::CreateInstance(DrmDevice *drm) {
  ImgImporter *importer = new ImgImporter(drm);
  if (!importer)
    return NULL;

  int ret = importer->Init();
  if (ret) {
    ALOGE("Failed to initialize the img importer %d", ret);
    delete importer;
    return NULL;
  }
  return importer;
}
#endif

ImgImporter::ImgImporter(DrmDevice *drm) : drm_(drm) {
}

ImgImporter::~ImgImporter() {
  std::lock_guard<std::mutex> lock(cachedBoMutex_);
  for (auto& pair : cachedBo_){
    ReleaseImgBuffer(&pair.second.bo);
  }
  cachedBo_.clear();
}


int ImgImporter::Init() {
  return 0;
}

uint32_t ImgImporter::ConvertImgHalFormatToDrm(IMG_native_handle_t *img_hnd, hwc_drm_bo_t *bo) {
  int stride_width = ALIGN_ROUND_UP(img_hnd->iWidth, IMG_HW_ALIGN);
  uint32_t handle = -1;

  int ret = drmPrimeFDToHandle(drm_->fd(), img_hnd->fd[0], &handle);
  if (ret) {
    ALOGE("failed to import prime fd %d ret=%d", img_hnd->fd[0], ret);
    return ret;
  }

  bo->width = img_hnd->iWidth;
  bo->height = img_hnd->iHeight;

  switch(img_hnd->iFormat) {
  case HAL_PIXEL_FORMAT_BGRX_8888:
    bo->gem_handles[0] = handle;
    bo->pitches[0] = stride_width * 4;
    bo->format = DRM_FORMAT_XRGB8888;
    break;
  case HAL_PIXEL_FORMAT_BGRA_8888:
    bo->gem_handles[0] = handle;
    bo->pitches[0] = stride_width * 4;
    bo->format = DRM_FORMAT_ARGB8888;
    break;
  case HAL_PIXEL_FORMAT_RGBX_8888:
    bo->gem_handles[0] = handle;
    bo->pitches[0] = stride_width * 4;
    bo->format = DRM_FORMAT_XBGR8888;
    break;
  case HAL_PIXEL_FORMAT_RGBA_8888:
    bo->gem_handles[0] = handle;
    bo->pitches[0] = stride_width * 4;
    bo->format = DRM_FORMAT_ABGR8888;
    break;
  case HAL_PIXEL_FORMAT_RGB_888:
    bo->gem_handles[0] = handle;
    bo->pitches[0] = stride_width * 3;
    bo->format = DRM_FORMAT_RGB888;
    break;
  case HAL_PIXEL_FORMAT_RGB_565:
    bo->gem_handles[0] = handle;
    bo->pitches[0] = stride_width * 2;
    bo->format = DRM_FORMAT_RGB565;
    break;
  case HAL_PIXEL_FORMAT_YV12:
    bo->gem_handles[0] = handle;
    bo->gem_handles[1] = handle;
    bo->pitches[0] = stride_width;
    bo->pitches[1] = stride_width;
    bo->offsets[1] = bo->pitches[0] * img_hnd->iHeight;
    bo->format = DRM_FORMAT_NV12;
    break;
  default:
	ALOGE("Failed to convert format 0x%x", img_hnd->iFormat);
    bo->format = -1;
	return -EINVAL;
  }
  return 0;
}


uint32_t ImgImporter::ConvertAndroidHalFormatToDrm(int format) {
  switch(format) {
  case HAL_PIXEL_FORMAT_BGRX_8888:
    return DRM_FORMAT_XRGB8888;
  case HAL_PIXEL_FORMAT_BGRA_8888:
    return DRM_FORMAT_ARGB8888;
  case HAL_PIXEL_FORMAT_RGBX_8888:
    return DRM_FORMAT_XBGR8888;
  case HAL_PIXEL_FORMAT_RGBA_8888:
    return DRM_FORMAT_ABGR8888;
  case HAL_PIXEL_FORMAT_RGB_888:
    return DRM_FORMAT_RGB888;
  case HAL_PIXEL_FORMAT_RGB_565:
    return DRM_FORMAT_RGB565;
  }

  ALOGE("Failed to convert format 0x%x", format);
  return -EINVAL;
}

int ImgImporter::ImportBuffer(buffer_handle_t ghandle, hwc_drm_bo_t *bo) {

  IMG_native_handle_t *img_hnd = (IMG_native_handle_t *)ghandle;
  int ret = 0;

  if (!img_hnd)
    return -EINVAL;

  {
      std::lock_guard<std::mutex> lock(cachedBoMutex_);
      auto it = cachedBo_.find(img_hnd->ui64Stamp);
      if(it != cachedBo_.end()) {
          it->second.used = true;
          *bo = it->second.bo;
          return 0;
      }
  }

  memset(bo, 0, sizeof(hwc_drm_bo_t));
  ret = ConvertImgHalFormatToDrm(img_hnd, bo);
  if (ret) {
    ALOGE("could not ConvertHalFormatToDrm %d", ret);
    return ret;
  }

  ret = drmModeAddFB2(drm_->fd(), bo->width, bo->height, bo->format, bo->gem_handles, bo->pitches, bo->offsets, &bo->fb_id, 0);

  if (ret) {
    ALOGE("could not create drm fb %d", ret);
    return ret;
  }

  {
      std::lock_guard<std::mutex> lock(cachedBoMutex_);

      bo->priv = reinterpret_cast<void*>(img_hnd->ui64Stamp);
      bo_t buffer;
      buffer.bo = *bo;
      buffer.needFlush = false;
      buffer.used = true;

      cachedBo_.insert(std::make_pair(img_hnd->ui64Stamp, buffer));
  }

  return ret;
}

void ImgImporter::FlushCache ()
{
  std::lock_guard<std::mutex> lock(cachedBoMutex_);
  ALOGI("ImgImporter cache(size %d)", (int)cachedBo_.size());
  for (auto it = cachedBo_.begin(); it != cachedBo_.end();) {
    it->second.needFlush = true;
    if (!it->second.used) {
      ReleaseImgBuffer(&it->second.bo);
      it = cachedBo_.erase(it);
    } else {
        ++it;
    }
  }
}

int ImgImporter::ReleaseBuffer(hwc_drm_bo_t *bo) {
  std::lock_guard<std::mutex> lock(cachedBoMutex_);
  unsigned long long key = reinterpret_cast<unsigned long long>(bo->priv);
  auto& buffer = cachedBo_[key];
  buffer.used = false;
  if (buffer.needFlush) {
    ReleaseImgBuffer(&buffer.bo);
    buffer.needFlush = false;
    cachedBo_.erase(key);
  }
  return 0;
}

int ImgImporter::ReleaseImgBuffer(hwc_drm_bo_t* bo) {
  if (bo->fb_id) {
    if (drmModeRmFB(drm_->fd(), bo->fb_id))
      ALOGE("Failed to rm fb %d", bo->fb_id);
  }
  struct drm_gem_close gem_close;
  memset(&gem_close, 0, sizeof(gem_close));
  int num_gem_handles = sizeof(bo->gem_handles) / sizeof(bo->gem_handles[0]);
  for (int i = 0; i < num_gem_handles; i++) {
    if (!bo->gem_handles[i])
      continue;

    gem_close.handle = bo->gem_handles[i];
    int ret = drmIoctl(drm_->fd(), DRM_IOCTL_GEM_CLOSE, &gem_close);
    if (ret)
      ALOGE("Failed to close gem handle %d %d", i, ret);
    else
      bo->gem_handles[i] = 0;
  }

  return 0;
}

#ifdef USE_IMG_IMPORTER
std::unique_ptr<Planner> Planner::CreateInstance(DrmDevice *) {
  std::unique_ptr<Planner> planner(new Planner);
  planner->AddStage<PlanStageGreedy>();
  return planner;
}
#endif
}
