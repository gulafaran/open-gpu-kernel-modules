/*
 * Copyright (c) 2015-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/module.h>

#include "nvidia-drm-os-interface.h"
#include "nvidia-drm.h"
#include "nvidia-drm-fence.h"

#include "nvidia-drm-conftest.h"

#if defined(NV_DRM_AVAILABLE)

MODULE_PARM_DESC(
    modeset,
    "Enable atomic kernel modesetting (1 = enable (default), 0 = disable)");
module_param_named(modeset, nv_drm_modeset_module_param, bool, 0444);

#if defined(NV_DRM_FBDEV_AVAILABLE)
MODULE_PARM_DESC(
    fbdev,
    "Create a framebuffer device (1 = enable (default), 0 = disable)");
module_param_named(fbdev, nv_drm_fbdev_module_param, bool, 0444);
#endif

MODULE_PARM_DESC(
    vblank,
    "Enable drm vblank notification support (1 = enable, 0 = disable (default))");
module_param_named(vblank, nv_drm_vblank_module_param, bool, 0444);

#if defined(NV_DRM_HAS_COLOROP)
MODULE_PARM_DESC(
    color_pipeline,
    "Enable DRM color pipeline support (1 = enable (default), 0 = disable)");
module_param_named(color_pipeline, nv_drm_color_pipeline_module_param, bool, 0444);
#endif

#if defined(NV_DMA_FENCE_OPS_HAS_SET_DEADLINE)
MODULE_PARM_DESC(
    deadline_boost,
    "Boost GPU clocks when a fence is about to miss a deadline set by a waiter, "
    "e.g. a compositor's page flip (1 = enable (default), 0 = disable)");
module_param_named(deadline_boost, nv_drm_deadline_boost_module_param, bool, 0644);

MODULE_PARM_DESC(
    deadline_boost_margin_us,
    "Count a deadline as missed if its fence is still pending this many "
    "microseconds before the deadline (default 0)");
module_param_named(deadline_boost_margin_us,
                   nv_drm_deadline_boost_margin_us_module_param, uint, 0644);

MODULE_PARM_DESC(
    deadline_boost_min_lead_us,
    "Ignore deadlines set less than this many microseconds in the future, "
    "as they could not have been met (default 1000)");
module_param_named(deadline_boost_min_lead_us,
                   nv_drm_deadline_boost_min_lead_us_module_param, uint, 0644);

MODULE_PARM_DESC(
    deadline_boost_misses,
    "Boost after this many deadline misses within one second (default 3)");
module_param_named(deadline_boost_misses,
                   nv_drm_deadline_boost_misses_module_param, uint, 0644);

MODULE_PARM_DESC(
    deadline_boost_onset_frames,
    "Boost after this many tight frames (fences that signaled with less than "
    "25% of their deadline lead to spare) within deadline_boost_onset_window_ms; "
    "0 disables (default 2)");
module_param_named(deadline_boost_onset_frames,
                   nv_drm_deadline_boost_onset_frames_module_param, uint, 0644);

MODULE_PARM_DESC(
    deadline_boost_onset_window_ms,
    "Window in milliseconds for deadline_boost_onset_frames (default 2500)");
module_param_named(deadline_boost_onset_window_ms,
                   nv_drm_deadline_boost_onset_window_ms_module_param, uint, 0644);

MODULE_PARM_DESC(
    deadline_boost_backoff_max_ms,
    "Each boost triggered without deadline_boost_backoff_reset_ms of calm "
    "since the previous trigger doubles the wait before the next boost, up to "
    "this many milliseconds; 0 disables the backoff (default 16000)");
module_param_named(deadline_boost_backoff_max_ms,
                   nv_drm_deadline_boost_backoff_max_ms_module_param, uint, 0644);

MODULE_PARM_DESC(
    deadline_boost_backoff_reset_ms,
    "Reset the boost backoff after this many milliseconds without a boost "
    "trigger (default 10000)");
module_param_named(deadline_boost_backoff_reset_ms,
                   nv_drm_deadline_boost_backoff_reset_ms_module_param, uint, 0644);

static int nv_drm_deadline_boost_stats_get(char *buf,
                                           const struct kernel_param *kp)
{
    return nv_drm_fence_deadline_stats(buf, PAGE_SIZE);
}

static const struct kernel_param_ops nv_drm_deadline_boost_stats_ops = {
    .get = nv_drm_deadline_boost_stats_get,
};

MODULE_PARM_DESC(
    deadline_boost_stats,
    "Fence deadline statistics: deadlines set and missed, bucketed by how far "
    "in the future they were set, deadlines untracked (no free slot), "
    "signaled early, checked at their deadline, and boosts requested");
module_param_cb(deadline_boost_stats, &nv_drm_deadline_boost_stats_ops,
                NULL, 0444);
#endif

#endif /* NV_DRM_AVAILABLE */

/*************************************************************************
 * Linux loading support code.
 *************************************************************************/

static int __init nv_linux_drm_init(void)
{
    return nv_drm_init();
}

static void __exit nv_linux_drm_exit(void)
{
    nv_drm_exit();
}

module_init(nv_linux_drm_init);
module_exit(nv_linux_drm_exit);

  MODULE_LICENSE("Dual MIT/GPL");

MODULE_INFO(supported, "external");
MODULE_VERSION(NV_VERSION_STRING);
MODULE_DESCRIPTION("NVIDIA DRM kernel module");
