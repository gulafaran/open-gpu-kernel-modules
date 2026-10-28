/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
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

/*
 * DRM-style usage stats (Documentation/gpu/drm-usage-stats.rst) in the
 * fdinfo of the /dev/nvidiaN files, for htop and similar tools.
 *
 * RM only knows usage per process: GSP samples which process is active on
 * each engine, and video memory is summed over the RM clients of a process.
 * So all files a process opened on a GPU show the same numbers under one
 * drm-client-id (the pid), which the tools count once.
 *
 * The engine time is accumulated from the GSP perfmon samples: each sample
 * credits its utilization over the sample period to the process that was
 * active. The samples are fetched at most once per NV_FDINFO_REFRESH_INTERVAL
 * while someone reads fdinfo, and only for processes whose fdinfo was read.
 * Nothing is fetched while the GPU is runtime suspended.
 */

#include "os-interface.h"
#include "nv-linux.h"

#include <linux/pid.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>
#include <linux/sort.h>

#define NV_FDINFO_REFRESH_INTERVAL  (HZ)

static bool nv_fdinfo_enable = true;
module_param_named(fdinfo, nv_fdinfo_enable, bool, 0444);
MODULE_PARM_DESC(fdinfo, "Report per-process GPU usage in the fdinfo of /dev/nvidiaN (default: 1).");

typedef struct nv_fdinfo_client_s
{
    struct list_head entry;
    nv_linux_state_t *nvl;
    struct pid *pid;
    unsigned int refs;                      /* files pointing at this client */
    NvU64 busy_ns[NV_FDINFO_ENGINE_COUNT];
    NvBool vidmem_valid;
    NvU64 vidmem_total;
    NvU64 vidmem_shared;
    unsigned long vidmem_refresh_jiffies;
} nv_fdinfo_client_t;

static const char * const nv_fdinfo_engine_names[NV_FDINFO_ENGINE_COUNT] = {
    [NV_FDINFO_ENGINE_GR]    = "gfx",
    [NV_FDINFO_ENGINE_NVENC] = "enc",
    [NV_FDINFO_ENGINE_NVDEC] = "dec",
    [NV_FDINFO_ENGINE_NVJPG] = "jpeg",
    [NV_FDINFO_ENGINE_NVOFA] = "ofa",
};

/* Protects the client list, the clients, nvl->fdinfo and the sample buffer */
static DEFINE_MUTEX(nv_fdinfo_lock);
static LIST_HEAD(nv_fdinfo_clients);
static nv_fdinfo_util_sample_t nv_fdinfo_samples[NV_FDINFO_UTIL_SAMPLE_COUNT];
static NvU64 nv_fdinfo_periods[NV_FDINFO_UTIL_SAMPLE_COUNT];

void nv_fdinfo_file_init(nv_linux_file_private_t *nvlfp)
{
    nvlfp->fdinfo_pid = get_pid(task_tgid(current));
}

void nv_fdinfo_file_free(nv_linux_file_private_t *nvlfp)
{
    nv_fdinfo_client_t *client = nvlfp->fdinfo_client;

    if (client != NULL)
    {
        mutex_lock(&nv_fdinfo_lock);
        if (--client->refs == 0)
        {
            list_del(&client->entry);
            put_pid(client->pid);
            NV_KFREE(client, sizeof(*client));
        }
        mutex_unlock(&nv_fdinfo_lock);
        nvlfp->fdinfo_client = NULL;
    }

    put_pid(nvlfp->fdinfo_pid);
    nvlfp->fdinfo_pid = NULL;
}

static int nv_fdinfo_cmp_sample(const void *a, const void *b)
{
    NvU64 ta = ((const nv_fdinfo_util_sample_t *)a)->timestamp;
    NvU64 tb = ((const nv_fdinfo_util_sample_t *)b)->timestamp;

    return (ta > tb) - (ta < tb);
}

static int nv_fdinfo_cmp_u64(const void *a, const void *b)
{
    NvU64 va = *(const NvU64 *)a;
    NvU64 vb = *(const NvU64 *)b;

    return (va > vb) - (va < vb);
}

static nv_fdinfo_client_t *nv_fdinfo_find_client(nv_linux_state_t *nvl, NvU32 pid)
{
    nv_fdinfo_client_t *client;

    list_for_each_entry(client, &nv_fdinfo_clients, entry)
    {
        if ((client->nvl == nvl) && (pid_nr(client->pid) == pid))
        {
            return client;
        }
    }

    return NULL;
}

/* Called with nv_fdinfo_lock held */
static void nv_fdinfo_refresh_engines(nv_linux_state_t *nvl, nvidia_stack_t *sp)
{
    nv_fdinfo_t *fdi = &nvl->fdinfo;
    nv_fdinfo_util_sample_t *samples = nv_fdinfo_samples;
    NvU64 period;
    unsigned int count, periods, i, e;

    if ((fdi->refresh_jiffies != 0) && time_before(jiffies, fdi->refresh_jiffies))
    {
        return;
    }
    fdi->refresh_jiffies = jiffies + NV_FDINFO_REFRESH_INTERVAL;

    /* GSP doesn't sample while the GPU is suspended; don't wake it */
    if (!pm_runtime_active(nvl->dev))
    {
        return;
    }

    if (rm_fdinfo_get_util_samples(sp, NV_STATE_PTR(nvl), samples) != NV_OK)
    {
        return;
    }

    /* The buffer is a ring; unused entries have timestamp 0 and sort first */
    sort(samples, NV_FDINFO_UTIL_SAMPLE_COUNT, sizeof(samples[0]),
         nv_fdinfo_cmp_sample, NULL);

    for (i = 0; (i < NV_FDINFO_UTIL_SAMPLE_COUNT) && (samples[i].timestamp == 0); i++)
        ;
    samples += i;
    count = NV_FDINFO_UTIL_SAMPLE_COUNT - i;

    if (count < 2)
    {
        return;
    }

    /* The timebase went backwards (GPU reset or power cycle): start over */
    if (samples[count - 1].timestamp < fdi->last_timestamp)
    {
        fdi->last_timestamp = 0;
    }

    /*
     * A sample covers one sampling period; a longer gap before it means
     * sampling paused, so credit at most the median period.
     */
    for (i = 1, periods = 0; i < count; i++)
    {
        if (samples[i].timestamp > samples[i - 1].timestamp)
        {
            nv_fdinfo_periods[periods++] = samples[i].timestamp - samples[i - 1].timestamp;
        }
    }

    if (periods == 0)
    {
        return;
    }

    sort(nv_fdinfo_periods, periods, sizeof(nv_fdinfo_periods[0]),
         nv_fdinfo_cmp_u64, NULL);
    period = nv_fdinfo_periods[periods / 2];

    for (i = 1; i < count; i++)
    {
        NvU64 duration;

        if (samples[i].timestamp <= fdi->last_timestamp ||
            samples[i].timestamp <= samples[i - 1].timestamp)
        {
            continue;
        }

        duration = min(samples[i].timestamp - samples[i - 1].timestamp, period);

        for (e = 0; e < NV_FDINFO_ENGINE_COUNT; e++)
        {
            nv_fdinfo_client_t *client;

            if ((samples[i].pid[e] == NV_FDINFO_PID_INVALID) ||
                (samples[i].util[e] == 0))
            {
                continue;
            }

            client = nv_fdinfo_find_client(nvl, samples[i].pid[e]);
            if (client != NULL)
            {
                client->busy_ns[e] += div_u64(min(samples[i].util[e], 10000U) * duration, 10000);
            }
        }
    }

    fdi->last_timestamp = samples[count - 1].timestamp;
}

/* Called with nv_fdinfo_lock held */
static void nv_fdinfo_refresh_vidmem(nv_fdinfo_client_t *client,
                                     nvidia_stack_t *sp,
                                     pid_t pid)
{
    nv_linux_state_t *nvl = client->nvl;
    NvU64 total, shared;

    if (client->vidmem_valid && time_before(jiffies, client->vidmem_refresh_jiffies))
    {
        return;
    }
    client->vidmem_refresh_jiffies = jiffies + NV_FDINFO_REFRESH_INTERVAL;

    /* Keep the last value while suspended, no allocations change then */
    if (!pm_runtime_active(nvl->dev))
    {
        return;
    }

    if (rm_fdinfo_get_vidmem(sp, NV_STATE_PTR(nvl), pid, &total, &shared) == NV_OK)
    {
        client->vidmem_total = total;
        client->vidmem_shared = shared;
        client->vidmem_valid = NV_TRUE;
    }
}

static nv_fdinfo_client_t *nv_fdinfo_get_client(nv_linux_file_private_t *nvlfp,
                                                nv_linux_state_t *nvl)
{
    nv_fdinfo_client_t *client;

    if (nvlfp->fdinfo_client != NULL)
    {
        return nvlfp->fdinfo_client;
    }

    list_for_each_entry(client, &nv_fdinfo_clients, entry)
    {
        if ((client->nvl == nvl) && (client->pid == nvlfp->fdinfo_pid))
        {
            goto found;
        }
    }

    NV_KZALLOC(client, sizeof(*client));
    if (client == NULL)
    {
        return NULL;
    }

    client->nvl = nvl;
    client->pid = get_pid(nvlfp->fdinfo_pid);
    list_add(&client->entry, &nv_fdinfo_clients);

found:
    client->refs++;
    nvlfp->fdinfo_client = client;

    return client;
}

void nv_fdinfo_show(struct seq_file *m, nv_linux_file_private_t *nvlfp)
{
    nv_linux_state_t *nvl;
    nv_fdinfo_client_t *client;
    nvidia_stack_t *sp = NULL;
    NvU64 busy_ns[NV_FDINFO_ENGINE_COUNT];
    NvBool vidmem_valid;
    NvU64 vidmem_total, vidmem_shared;
    pid_t pid;
    unsigned int e;

    if (!nv_fdinfo_enable || !nv_is_open_complete(nvlfp) || (nvlfp->open_rc != 0))
    {
        return;
    }

    nvl = nvlfp->nvptr;

    /* The pid as the reader sees it; 0 if the opener isn't in its namespace */
    pid = pid_vnr(nvlfp->fdinfo_pid);
    if ((nvl == NULL) || (pid == 0))
    {
        return;
    }

    if (nv_kmem_cache_alloc_stack(&sp) != 0)
    {
        return;
    }

    mutex_lock(&nv_fdinfo_lock);

    client = nv_fdinfo_get_client(nvlfp, nvl);
    if (client == NULL)
    {
        mutex_unlock(&nv_fdinfo_lock);
        nv_kmem_cache_free_stack(sp);
        return;
    }

    nv_fdinfo_refresh_engines(nvl, sp);
    nv_fdinfo_refresh_vidmem(client, sp, pid);

    memcpy(busy_ns, client->busy_ns, sizeof(busy_ns));
    vidmem_valid = client->vidmem_valid;
    vidmem_total = client->vidmem_total;
    vidmem_shared = client->vidmem_shared;

    mutex_unlock(&nv_fdinfo_lock);

    nv_kmem_cache_free_stack(sp);

    seq_printf(m, "drm-driver:\t%s\n", MODULE_NAME);
    seq_printf(m, "drm-pdev:\t%s\n", dev_name(nvl->dev));
    seq_printf(m, "drm-client-id:\t%d\n", pid);

    for (e = 0; e < NV_FDINFO_ENGINE_COUNT; e++)
    {
        seq_printf(m, "drm-engine-%s:\t%llu ns\n", nv_fdinfo_engine_names[e], busy_ns[e]);
    }

    if (vidmem_valid)
    {
        seq_printf(m, "drm-total-vram:\t%llu KiB\n", vidmem_total / 1024);
        seq_printf(m, "drm-shared-vram:\t%llu KiB\n", vidmem_shared / 1024);
        seq_printf(m, "drm-resident-vram:\t%llu KiB\n", vidmem_total / 1024);
    }
}
