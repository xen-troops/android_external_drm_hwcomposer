#define LOG_TAG "hwc-platform-imagination"

#include "platformimagination.h"

#include <log/log.h>
#include <xf86drm.h>

#include "img_gralloc1_public.h"

namespace android {

Importer *Importer::CreateInstance(DrmDevice *drm) {
  ImaginationImporter *importer = new ImaginationImporter(drm);
  if (!importer)
    return NULL;

  int ret = importer->Init();
  if (ret) {
    ALOGE("Failed to initialize the Imagination importer %d", ret);
    delete importer;
    return NULL;
  }
  return importer;
}

int ImaginationImporter::ConvertBoInfo(buffer_handle_t handle,
                                       hwc_drm_bo_t *bo) {
  IMG_native_handle_t *hnd = (IMG_native_handle_t *)handle;
  if (!hnd)
    return -EINVAL;

  /* Extra bits are responsible for buffer compression and memory layout */
  if (hnd->iFormat & ~0x10f) {
    ALOGV("Special buffer formats are not supported");
    return -EINVAL;
  }

  bo->width = hnd->iWidth;
  bo->height = hnd->iHeight;
  bo->usage = hnd->usage;
  bo->prime_fds[0] = hnd->fd[0];
  bo->pitches[0] = ALIGN(hnd->iWidth, HW_ALIGN) * hnd->uiBpp >> 3;
  bo->hal_format = hnd->iFormat;
  bo->pixel_stride = hnd->aiStride[0];

  switch (hnd->iFormat) {
#ifdef HAL_PIXEL_FORMAT_BGRX_8888
    case HAL_PIXEL_FORMAT_BGRX_8888:
      bo->format = DRM_FORMAT_XRGB8888;
      break;
#endif
    default:
      bo->format = ConvertHalFormatToDrm(hnd->iFormat & 0xf);
      if (bo->format == DRM_FORMAT_INVALID) {
        ALOGV("Cannot convert hal format to drm format %u", hnd->iFormat);
        return -EINVAL;
      }
  }

  return 0;
}

#ifdef ENABLE_GEM_HANDLE_CACHING
ImaginationImporter::~ImaginationImporter() {
  std::lock_guard<std::mutex> lock(cachedBoMutex_);
  for (auto &pair : cachedBo_) {
    DrmGenericImporter::ReleaseBuffer(&pair.second.bo);
  }
  cachedBo_.clear();
}

int ImaginationImporter::ImportBuffer(buffer_handle_t ghandle,
                                      hwc_drm_bo_t *bo) {
  IMG_native_handle_t *img_hnd = (IMG_native_handle_t *)ghandle;
  int ret = 0;

  if (!img_hnd)
    return -EINVAL;

  {
    std::lock_guard<std::mutex> lock(cachedBoMutex_);
    auto it = cachedBo_.find(img_hnd->ui64Stamp);
    if (it != cachedBo_.end()) {
      it->second.used = true;
      *bo = it->second.bo;
      return 0;
    }
  }
  ret = DrmGenericImporter::ImportBuffer(ghandle, bo);
  if (ret) {
    ALOGE("could not create drm fb %d", ret);
    return ret;
  }
  {
    std::lock_guard<std::mutex> lock(cachedBoMutex_);

    bo->priv = reinterpret_cast<void *>(img_hnd->ui64Stamp);
    bo_t buffer;
    buffer.bo = *bo;
    buffer.needFlush = false;
    buffer.used = true;

    cachedBo_.insert(std::make_pair(img_hnd->ui64Stamp, buffer));
  }
  return ret;
}

int ImaginationImporter::ReleaseBuffer(hwc_drm_bo_t *bo) {
  std::lock_guard<std::mutex> lock(cachedBoMutex_);
  unsigned long long key = reinterpret_cast<unsigned long long>(bo->priv);
  auto &buffer = cachedBo_[key];
  buffer.used = false;
  if (buffer.needFlush) {
    DrmGenericImporter::ReleaseBuffer(&buffer.bo);
    buffer.needFlush = false;
    cachedBo_.erase(key);
  }
  return 0;
}
void ImaginationImporter::HandleHotplug() {
  std::lock_guard<std::mutex> lock(cachedBoMutex_);
  for (auto it = cachedBo_.begin(); it != cachedBo_.end();) {
    it->second.needFlush = true;
    if (!it->second.used) {
      DrmGenericImporter::ReleaseBuffer(&it->second.bo);
      it = cachedBo_.erase(it);
    } else {
      ++it;
    }
  }
}
#endif
}  // namespace android
