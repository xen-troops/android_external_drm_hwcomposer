#ifndef PLATFORMIMAGINATION_H
#define PLATFORMIMAGINATION_H

#include "drmdevice.h"
#include "platform.h"
#include "platformdrmgeneric.h"

#include <stdatomic.h>

#include <hardware/gralloc.h>

namespace android {

class ImaginationImporter : public DrmGenericImporter {
 public:
  using DrmGenericImporter::DrmGenericImporter;

  int ConvertBoInfo(buffer_handle_t handle, hwc_drm_bo_t *bo) override;

#ifdef ENABLE_GEM_HANDLE_CACHING
  typedef struct bo_cached_descriptor {
    hwc_drm_bo_t bo;
    // Handle used by some external component(SF)
    bool used;
    // Deallocation needed
    bool needFlush;
  } bo_t;

  ~ImaginationImporter() override;
  int ImportBuffer(buffer_handle_t handle, hwc_drm_bo_t *bo) override;
  int ReleaseBuffer(hwc_drm_bo_t *bo) override;
  void HandleHotplug() override;

 private:
  std::map<unsigned long long, bo_t> cachedBo_;
  std::mutex cachedBoMutex_;
#endif
};
}  // namespace android

#endif  // PLATFORMIMAGINATION_H
