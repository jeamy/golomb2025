#include "glibc_c23_math_compat.h"
#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <omp.h>
#include <string>
#include <vector>
#include <algorithm>

extern "C" {
#include "../include/golomb.h"
}

// (hint pointer removed; not needed)

/* ---------------- Checkpointing header ---------------- */
typedef struct {
    char     magic[4];   // "GRCP"
    uint32_t version;    // 1
    uint32_t n;
    uint32_t L;
    uint64_t total;
    uint32_t hint_s;
    uint32_t hint_t;
    uint32_t hint_used;  // 0/1
} cp_header_t;

static int cp_load_file(const char *path,
                        int n,
                        int target_length,
                        long long total,
                        int hint_s,
                        int hint_t,
                        int hint_used,
                        uint32_t *done_words,
                        size_t words)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    cp_header_t h;
    size_t r = fread(&h, 1, sizeof h, fp);
    if (r != sizeof h || memcmp(h.magic, "GRCP", 4) != 0 || h.version != 1) { fclose(fp); return 0; }
    if (h.n != (uint32_t)n || h.L != (uint32_t)target_length || h.total != (uint64_t)total) { fclose(fp); return 0; }
    if (h.hint_s != (uint32_t)hint_s || h.hint_t != (uint32_t)hint_t || h.hint_used != (uint32_t)hint_used) { fclose(fp); return 0; }
    size_t want = words * sizeof(uint32_t);
    r = fread(done_words, 1, want, fp);
    fclose(fp);
    return r == want;
}

static int cp_save_file(const char *path,
                        int n,
                        int target_length,
                        long long total,
                        int hint_s,
                        int hint_t,
                        int hint_used,
                        const uint32_t *done_words,
                        size_t words)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *fp = fopen(tmp, "wb");
    if (!fp) return 0;
    cp_header_t h;
    memcpy(h.magic, "GRCP", 4);
    h.version = 1;
    h.n = (uint32_t)n;
    h.L = (uint32_t)target_length;
    h.total = (uint64_t)total;
    h.hint_s = (uint32_t)hint_s;
    h.hint_t = (uint32_t)hint_t;
    h.hint_used = (uint32_t)hint_used;
    size_t w1 = fwrite(&h, 1, sizeof h, fp);
    size_t w2 = fwrite(done_words, 1, words * sizeof(uint32_t), fp);
    int ok = (w1 == sizeof h) && (w2 == words * sizeof(uint32_t));
    if (fclose(fp) != 0) ok = 0;
    if (!ok) { remove(tmp); return 0; }
    if (rename(tmp, path) != 0) { remove(tmp); return 0; }
    return 1;
}

/* ---------------- Host bitset helpers (match C solver) ---------------- */
static inline void set_bit64(uint64_t *bs, int idx) { bs[idx >> 6] |= 1ULL << (idx & 63); }
static inline int  test_bit64(const uint64_t *bs, int idx) { return (bs[idx >> 6] >> (idx & 63)) & 1ULL; }

/* ---------------- GPU candidate prefilter ---------------- */
struct Cand { int s, t, u_hint, score; };

__global__ void prefilter_kernel(int n, int L, Cand *cands, int64_t total, unsigned char *ok)
{
    int64_t i = blockIdx.x * 1LL * blockDim.x + threadIdx.x;
    if (i >= total) return;
    int s = cands[i].s;
    int t = cands[i].t;
    int best_u = 0;
    // Depth=3 state: pos[0]=0, pos[1]=s, pos[2]=t
    int rem = n - 3; // remaining marks including final
    int tri_needed = rem * (rem - 1) / 2; // minimal additional length needed after 't'
    if (t + tri_needed > L) { ok[i] = 0; return; }

    // existing distances at depth=3
    int d_s = s;
    int d_t = t;
    int d_st = t - s;

    // First next bound using triangular after-placing bound
    int rem_after1 = rem - 1;                    // after choosing u
    int tri_after1 = rem_after1 * (rem_after1 - 1) / 2;
    int max_u = L - tri_after1;
    if (max_u <= t) { ok[i] = 0; return; }

    unsigned char ok1 = 0, ok2 = 0;
    for (int u = t + 1; u <= max_u; ++u) {
        int du0 = u;       // u - 0
        int du1 = u - s;   // u - s
        int du2 = u - t;   // u - t
        // uniqueness vs existing
        if (du0 == d_s || du0 == d_t || du0 == d_st) continue;
        if (du1 == d_s || du1 == d_t || du1 == d_st) continue;
        if (du2 == d_s || du2 == d_t || du2 == d_st) continue;
        // pairwise distinct among du0,du1,du2 always true as 0<s<t<u
        ok1 = 1; // one-step feasible

        // Two-step feasibility: try to place v > u
        int rem2 = rem_after1 - 1;                 // remaining after placing u
        int tri_needed2 = rem2 * (rem2 - 1) / 2;   // minimal addl length after v must be <= L - v
        if (u + tri_needed2 > L) continue;
        int rem_after2 = rem2 - 1;
        int tri_after2 = rem_after2 * (rem_after2 - 1) / 2;
        int max_v = L - tri_after2;
        if (max_v <= u) continue;

        // Distances present after u:
        // {d_s, d_t, d_st, du0, du1, du2}
        for (int v = u + 1; v <= max_v; ++v) {
            int dv0 = v;       // v - 0
            int dv1 = v - s;   // v - s
            int dv2 = v - t;   // v - t
            int dv3 = v - u;   // v - u
            // compare to existing set
            if (dv0 == d_s || dv0 == d_t || dv0 == d_st || dv0 == du0 || dv0 == du1 || dv0 == du2) continue;
            if (dv1 == d_s || dv1 == d_t || dv1 == d_st || dv1 == du0 || dv1 == du1 || dv1 == du2) continue;
            if (dv2 == d_s || dv2 == d_t || dv2 == d_st || dv2 == du0 || dv2 == du1 || dv2 == du2) continue;
            if (dv3 == d_s || dv3 == d_t || dv3 == d_st || dv3 == du0 || dv3 == du1 || dv3 == du2) continue;
            // pairwise distinct among dv0..dv3; trivial order ensures dv0>dv1>dv2>dv3>0 but check collisions among dv* themselves:
            if (dv0 == dv1 || dv0 == dv2 || dv0 == dv3) continue;
            if (dv1 == dv2 || dv1 == dv3) continue;
            if (dv2 == dv3) continue;
            ok2 = 2; best_u = (best_u == 0 ? u : best_u); break;
        }
        if (ok2) break;
        if (!best_u) best_u = u; // remember first one-step-feasible u as fallback
    }
    ok[i] = ok2 ? ok2 : ok1;
    cands[i].u_hint = best_u;
}

/* ---------------- Heartbeat ---------------- */
static volatile int g_done = 0;
static volatile int g_current_L = -1;
static double g_vt_sec = 0.0;
static struct timespec g_ts_start;

static void *heartbeat_thread(void *)
{
    while (!g_done) {
        struct timespec ts_now; clock_gettime(CLOCK_MONOTONIC, &ts_now);
        double since = (ts_now.tv_sec - g_ts_start.tv_sec) + (ts_now.tv_nsec - g_ts_start.tv_nsec) / 1e9;
        int L = g_current_L;
        if (g_vt_sec > 0.0 && L >= 0) {
            // format mm:ss.mmm
            int minutes = (int)(since / 60.0);
            double seconds = since - minutes * 60.0;
            if (minutes > 0) fprintf(stderr, "[VT] %02d:%06.3f elapsed – current L=%d\n", minutes, seconds, L);
            else              fprintf(stderr, "[VT] %.3f s elapsed – current L=%d\n", seconds, L);
            fflush(stderr);
        }
        struct timespec req = { (time_t)g_vt_sec, (long)((g_vt_sec - (time_t)g_vt_sec) * 1e9) };
        nanosleep(&req, NULL);
    }
    return NULL;
}

/* ---------------- Optional async prefilter worker ---------------- */
struct PrefilterJob {
    int n;
    int L;
    long long total;
    Cand *cands; // input snapshot for device copy
    std::vector<unsigned char> ok_out;
    size_t ok2_cnt{0}, ok1_cnt{0};
    volatile int done{0};
    std::vector<Cand> cands_out; // device-updated copies (for u_hint)
};

static void *prefilter_worker(void *arg)
{
    PrefilterJob *job = (PrefilterJob*)arg;
    long long total = job->total;
    Cand *d_cands = nullptr; unsigned char *d_ok = nullptr; unsigned char *h_ok = nullptr;
    // bind device for this thread
    cudaSetDevice(0);
    cudaMalloc(&d_cands, sizeof(Cand) * (size_t)total);
    cudaMalloc(&d_ok, (size_t)total);
    cudaMemcpy(d_cands, job->cands, sizeof(Cand) * (size_t)total, cudaMemcpyHostToDevice);
    int threads = 256; int blocks = (int)((total + threads - 1) / threads);
    prefilter_kernel<<<blocks, threads>>>(job->n, job->L, d_cands, total, d_ok);
    cudaError_t kerr = cudaGetLastError();
    if (kerr != cudaSuccess) {
        fprintf(stderr, "[CUDA] prefilter kernel launch error: %s (%d)\n", cudaGetErrorString(kerr), (int)kerr);
    }
    cudaDeviceSynchronize();
    h_ok = (unsigned char*)malloc((size_t)total);
    cudaMemcpy(h_ok, d_ok, (size_t)total, cudaMemcpyDeviceToHost);
    // Copy device cands into job->cands_out to avoid racing writes to main cands
    job->cands_out.resize((size_t)total);
    cudaMemcpy(job->cands_out.data(), d_cands, sizeof(Cand) * (size_t)total, cudaMemcpyDeviceToHost);
    size_t ok2 = 0, ok1 = 0;
    for (size_t i = 0; i < (size_t)total; ++i) { if (h_ok[i] >= 2) ++ok2; else if (h_ok[i] == 1) ++ok1; }
    job->ok2_cnt = ok2; job->ok1_cnt = ok1;
    job->ok_out.assign(h_ok, h_ok + (size_t)total);
    cudaFree(d_cands); cudaFree(d_ok); free(h_ok);
    job->done = 1;
    return NULL;
}

/* ---------------- Main (CUDA-enhanced mp) ---------------- */
int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <n> [-b] [-v] [-H] [-f <file>] [-fi <sec>] [-vt <min>] [-wu <N>] [-dh] [-dw <W>] [-ap]\n", argv[0]);
        return 1;
    }
    int n = atoi(argv[1]);
    bool verbose = false;
    bool use_b = false;
    bool hints = false; // enable LUT hint order and fast-lane only with -H
    const char *cp_path = NULL;
    int cp_interval = 60;
    double vt_min = 0.0;
    // Tunables: warmup size and depth-3 hinting
    long long warmup_limit = 8192; // default
    const char *env_wu = getenv("GOLOMB_WARMUP");
    if (env_wu && *env_wu) {
        long long v = atoll(env_wu); if (v > 0) warmup_limit = v;
    }
    int dfs3_hint = (getenv("GOLOMB_DFS3_HINT") != NULL) ? 1 : 0;
    int u_win = 16; const char *env_uw = getenv("GOLOMB_UWIN"); if (env_uw && *env_uw) { int v = atoi(env_uw); if (v > 0) u_win = v; }
    int async_pref = (getenv("GOLOMB_ASYNC_PREF") != NULL) ? 1 : 0;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0) verbose = true;
        else if (strcmp(argv[i], "-b") == 0) use_b = true;
        else if (strcmp(argv[i], "-H") == 0) hints = true;
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) { cp_path = argv[++i]; }
        else if (strcmp(argv[i], "-fi") == 0 && i + 1 < argc) { cp_interval = atoi(argv[++i]); if (cp_interval <= 0) cp_interval = 60; }
        else if (strcmp(argv[i], "-vt") == 0 && i + 1 < argc) { vt_min = atof(argv[++i]); }
        else if (strcmp(argv[i], "-wu") == 0 && i + 1 < argc) { long long v = atoll(argv[++i]); if (v > 0) warmup_limit = v; }
        else if (strcmp(argv[i], "-dh") == 0) { dfs3_hint = 1; }
        else if (strcmp(argv[i], "-dw") == 0 && i + 1 < argc) { int v = atoi(argv[++i]); if (v > 0) u_win = v; }
        else if (strcmp(argv[i], "-ap") == 0) { async_pref = 1; }
        else {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            return 2;
        }
    }

    /* Enable SIMD in C dfs unless explicitly disabled. solver.c was built with -march=native. */
    g_use_simd = (getenv("GOLOMB_NO_SIMD") == NULL);

    const ruler_t *ref = lut_lookup_by_marks(n);
    if (!use_b || !ref) {
        fprintf(stderr, "This CUDA variant currently requires -b and a known LUT length for n=%d.\n", n);
        return 3;
    }
    int target_length = ref->length;

    // Start time and heartbeat
    clock_gettime(CLOCK_MONOTONIC, &g_ts_start);
    time_t t_start_wall = time(NULL);
    char start_iso[32]; strftime(start_iso, sizeof start_iso, "%F %T", localtime(&t_start_wall));
    fprintf(stderr, "Start time: %s\n", start_iso);
    g_current_L = target_length;
    g_vt_sec = vt_min > 0 ? vt_min * 60.0 : 0.0;
    pthread_t hb_th; if (g_vt_sec > 0.0) pthread_create(&hb_th, NULL, heartbeat_thread, NULL);

    // Build candidates like -mp
    int half = target_length / 2;
    int T = target_length - (n - 2);
    int second_max = half; if (second_max > T - 1) second_max = T - 1; if (second_max < 1) second_max = 1;

    long long total = 0;
    for (int s = 1; s <= second_max; ++s) {
        int cnt = T - s; if (cnt > 0) total += cnt;
    }
    std::vector<Cand> cands; cands.reserve((size_t)total);
    int use_hint_order = (hints && ref && getenv("GOLOMB_NO_HINTS") == NULL) ? 1 : 0;
    for (int s = 1; s <= second_max; ++s) {
        for (int t = s + 1; t <= T; ++t) {
            int score = 0;
            if (use_hint_order) {
                int ds = s - ref->pos[1]; if (ds < 0) ds = -ds;
                int dt = t - ref->pos[2]; if (dt < 0) dt = -dt;
                score = ds + dt;
            }
            cands.push_back({s, t, 0, score});
        }
    }
    if (use_hint_order && cands.size() > 1) {
        std::stable_sort(cands.begin(), cands.end(), [](const Cand &a, const Cand &b){
            if (a.score != b.score) return a.score < b.score;
            if (a.s != b.s) return a.s < b.s;
            return a.t < b.t;
        });
    }

    // Checkpoint bitset
    size_t words = (size_t)((total + 31) / 32); if (words == 0) words = 1;
    std::vector<uint32_t> done_words(words, 0);
    if (cp_path && *cp_path) {
        int hs = use_hint_order && ref ? ref->pos[1] : 0;
        int ht = use_hint_order && ref ? ref->pos[2] : 0;
        (void)cp_load_file(cp_path, n, target_length, total, hs, ht, use_hint_order, done_words.data(), words);
        (void)cp_save_file(cp_path, n, target_length, total, hs, ht, use_hint_order, done_words.data(), words);
    }

    // Optional GPU prefilter: mark candidates that can advance one or two steps without immediate duplicates
    std::vector<unsigned char> ok_host; ok_host.reserve((size_t)total);
    int device_count = 0;
    cudaError_t derr = cudaGetDeviceCount(&device_count);
    int rt_ver = 0, dr_ver = 0; cudaRuntimeGetVersion(&rt_ver); cudaDriverGetVersion(&dr_ver);
    const char *cvd = getenv("CUDA_VISIBLE_DEVICES");
    fprintf(stderr, "[CUDA] Runtime=%d Driver=%d CUDA_VISIBLE_DEVICES=%s\n", rt_ver, dr_ver, cvd ? cvd : "(unset)");
    if (derr != cudaSuccess) {
        fprintf(stderr, "[CUDA] cudaGetDeviceCount error: %s (%d)\n", cudaGetErrorString(derr), (int)derr);
    }
    if (device_count > 0) {
        int dev = 0;
        cudaError_t sderr = cudaSetDevice(dev);
        if (sderr != cudaSuccess) {
            fprintf(stderr, "[CUDA] cudaSetDevice(%d) failed: %s (%d)\n", dev, cudaGetErrorString(sderr), (int)sderr);
        }
        // create context
        cudaFree(0);
        cudaDeviceProp prop; cudaGetDeviceProperties(&prop, dev);
        fprintf(stderr, "[CUDA] Using device %d: %s\n", dev, prop.name);
    } else {
        fprintf(stderr, "[CUDA] No CUDA device found – running CPU-only prefilter.\n");
    }
    // Enable GPU prefilter only when -H is NOT set; skip entirely under hints fast-lane
    pthread_t pf_th; PrefilterJob pf_job{}; int pf_started = 0;
    if (!use_hint_order && device_count > 0 && total > 0) {
        if (async_pref) {
            pf_job.n = n; pf_job.L = target_length; pf_job.total = total; pf_job.cands = cands.data();
            // Ensure CUDA context exists for the thread
            cudaFree(0);
            pthread_create(&pf_th, NULL, prefilter_worker, &pf_job);
            pf_started = 1;
        } else {
            // synchronous prefilter
            Cand *d_cands = nullptr; unsigned char *d_ok = nullptr; unsigned char *h_ok = (unsigned char*)malloc((size_t)total);
            cudaMalloc(&d_cands, sizeof(Cand) * (size_t)total);
            cudaMalloc(&d_ok, (size_t)total);
            cudaMemcpy(d_cands, cands.data(), sizeof(Cand) * (size_t)total, cudaMemcpyHostToDevice);
            int threads = 256; int blocks = (int)((total + threads - 1) / threads);
            prefilter_kernel<<<blocks, threads>>>(n, target_length, d_cands, total, d_ok);
            cudaError_t kerr = cudaGetLastError();
            if (kerr != cudaSuccess) {
                fprintf(stderr, "[CUDA] prefilter kernel launch error: %s (%d)\n", cudaGetErrorString(kerr), (int)kerr);
            }
            cudaDeviceSynchronize();
            cudaMemcpy(h_ok, d_ok, (size_t)total, cudaMemcpyDeviceToHost);
            cudaMemcpy(cands.data(), d_cands, sizeof(Cand) * (size_t)total, cudaMemcpyDeviceToHost);
            size_t ok2_cnt = 0, ok1_cnt = 0;
            for (size_t i = 0; i < (size_t)total; ++i) { if (h_ok[i] >= 2) ++ok2_cnt; else if (h_ok[i] == 1) ++ok1_cnt; }
            fprintf(stderr, "[CUDA] Prefiltered %lld candidates: %zu two-step, %zu one-step.\n", total, ok2_cnt, ok1_cnt);
            ok_host.assign(h_ok, h_ok + (size_t)total);
            cudaFree(d_cands); cudaFree(d_ok); free(h_ok);
        }
    }

    // Fast-lane: try exact LUT pair on CPU first only if hints are enabled (-H)
    volatile int found = 0; ruler_t res_local{};
    if (use_hint_order) {
        int s0 = ref->pos[1], t0 = ref->pos[2];
        if (s0 >= 1 && s0 <= second_max && t0 > s0 && t0 <= T) {
            uint64_t bs[BS_WORDS] = {0};
            int pos[MAX_MARKS]; pos[0] = 0; pos[1] = s0; pos[2] = t0;
            set_bit64(bs, s0);
            int d13 = t0; int d23 = t0 - s0;
            if (!test_bit64(bs, d13) && !test_bit64(bs, d23)) {
                set_bit64(bs, d13); set_bit64(bs, d23);
                if (dfs(3, n, target_length, pos, bs, verbose)) {
                    res_local.marks = n; res_local.length = pos[n - 1]; memcpy(res_local.pos, pos, n * sizeof(int)); found = 1;
                }
            }
        }
    }

    // Quick stage: try a larger prefix of candidates with dfs at depth=3 (pure DFS over (s,t)).
    // This leverages the GPU's feasibility flags early without global reordering.
    if (!found && total > 0) {
        long long warmup = total < warmup_limit ? total : warmup_limit;
        // Build warmup index list: when -H is not used, prioritize ok>=2 then ok==1 without reordering global cands
        std::vector<long long> warm_idx; warm_idx.reserve((size_t)warmup);
        if (!use_hint_order && ok_host.size() == (size_t)total) {
            for (long long i = 0; i < total && (long long)warm_idx.size() < warmup; ++i) if (ok_host[(size_t)i] >= 2) warm_idx.push_back(i);
            for (long long i = 0; i < total && (long long)warm_idx.size() < warmup; ++i) if (ok_host[(size_t)i] == 1) warm_idx.push_back(i);
            for (long long i = 0; i < total && (long long)warm_idx.size() < warmup; ++i) warm_idx.push_back(i);
        } else {
            for (long long i = 0; i < warmup; ++i) warm_idx.push_back(i);
        }
        #pragma omp parallel for schedule(dynamic, 16)
        for (long long j = 0; j < (long long)warm_idx.size(); ++j) {
            if (found) continue;
            long long i = warm_idx[(size_t)j];
            if (found) continue;
            int second = cands[(size_t)i].s;
            int third  = cands[(size_t)i].t;
            if (third - second == second) continue; // skip isosceles triangle
            uint64_t bs[BS_WORDS] = {0};
            int pos[MAX_MARKS]; pos[0] = 0; pos[1] = second; pos[2] = third;
            set_bit64(bs, second);
            int d13 = third; int d23 = third - second;
            if (test_bit64(bs, d13) || test_bit64(bs, d23)) continue;
            set_bit64(bs, d13); set_bit64(bs, d23);
            if (dfs(3, n, target_length, pos, bs, verbose)) {
                #pragma omp critical
                {
                    if (!found) { res_local.marks = n; res_local.length = pos[n - 1]; memcpy(res_local.pos, pos, n * sizeof(int)); found = 1; }
                }
            }
        }
    }

    // If prefilter was launched asynchronously, wait here so u_hint values are ready for the main loop
    if (pf_started) {
        pthread_join(pf_th, NULL);
        fprintf(stderr, "[CUDA] Prefiltered %lld candidates: %zu two-step, %zu one-step.\n", total, pf_job.ok2_cnt, pf_job.ok1_cnt);
        ok_host.swap(pf_job.ok_out);
        // merge u_hint back into main cands without changing order
        if (pf_job.cands_out.size() == (size_t)total) {
            for (size_t i = 0; i < (size_t)total; ++i) cands[i].u_hint = pf_job.cands_out[i].u_hint;
        }
    }

    struct timespec ts_last_flush; clock_gettime(CLOCK_MONOTONIC, &ts_last_flush);

    // Parallel CPU search across candidates (mp-like): pure DFS(3) over (s,t), respecting checkpoint
    #pragma omp parallel for schedule(dynamic, 16)
    for (long long i = 0; i < total; ++i) {
        if (found) continue;
        // skip processed
        if (cp_path && *cp_path) {
            size_t wi = (size_t)(i >> 5); uint32_t mask = 1u << (i & 31);
            if (done_words[wi] & mask) continue;
        }
        int second = cands[(size_t)i].s;
        int third  = cands[(size_t)i].t;
        if (third - second == second) goto checkpoint_update; // skip isosceles triangle
        // Initialize base state for depth=3. Optionally bias depth-3 by trying u near u_hint first, then call dfs(3)
        {
            uint64_t bs[BS_WORDS] = {0};
            set_bit64(bs, second);
            int d13 = third; int d23 = third - second;
            if (!test_bit64(bs, d13) && !test_bit64(bs, d23)) {
                set_bit64(bs, d13); set_bit64(bs, d23);
                int pos[MAX_MARKS]; pos[0] = 0; pos[1] = second; pos[2] = third;
                bool ok_found = false;
                if (dfs3_hint) {
                    // Compute bound for u (depth=4)
                    const int rem_after2 = n - 4;
                    const int tri_after2 = rem_after2 * (rem_after2 - 1) / 2;
                    const int max_u = target_length - tri_after2;
                    const int u_hint = cands[(size_t)i].u_hint;
                    auto try_u = [&](int u)->bool {
                        const int du0 = u;
                        const int du1 = u - second;
                        const int du2 = u - third;
                        if (du1 <= 0 || du2 <= 0) return false;
                        if (test_bit64(bs, du0) || test_bit64(bs, du1) || test_bit64(bs, du2)) return false;
                        if (du0 == du1 || du0 == du2 || du1 == du2) return false;
                        uint64_t bs_u[BS_WORDS]; memcpy(bs_u, bs, sizeof(bs_u));
                        set_bit64(bs_u, du0); set_bit64(bs_u, du1); set_bit64(bs_u, du2);
                        int pos_u[MAX_MARKS];
                        pos_u[0] = pos[0]; pos_u[1] = pos[1]; pos_u[2] = pos[2]; pos_u[3] = u;
                        if (dfs(4, n, target_length, pos_u, bs_u, verbose)) {
                            #pragma omp critical
                            {
                                if (!found) { res_local.marks = n; res_local.length = pos_u[n - 1]; memcpy(res_local.pos, pos_u, n * sizeof(int)); found = 1; }
                            }
                            return true;
                        }
                        return false;
                    };
                    if (!found && max_u > third && u_hint > third && u_hint <= max_u) {
                        if (try_u(u_hint)) ok_found = true;
                    }
                    if (!found && !ok_found && max_u > third && u_hint > 0) {
                        int start = std::max(third + 1, u_hint - u_win);
                        int end   = std::min(max_u, u_hint + u_win);
                        for (int u = start; u <= end && !found; ++u) {
                            if (u == u_hint) continue;
                            if (try_u(u)) { ok_found = true; break; }
                        }
                    }
                }
                if (!ok_found && dfs(3, n, target_length, pos, bs, verbose)) {
                    #pragma omp critical
                    {
                        if (!found) { res_local.marks = n; res_local.length = pos[n - 1]; memcpy(res_local.pos, pos, n * sizeof(int)); found = 1; }
                    }
                }
            }
        }
checkpoint_update:
        if (cp_path && *cp_path) {
            size_t wi = (size_t)(i >> 5); uint32_t mask = 1u << (i & 31);
            __sync_fetch_and_or(&done_words[wi], mask);
            struct timespec ts_now; clock_gettime(CLOCK_MONOTONIC, &ts_now);
            time_t dt = ts_now.tv_sec - ts_last_flush.tv_sec;
            if (dt >= cp_interval) {
                #pragma omp critical
                {
                    struct timespec ts_chk; clock_gettime(CLOCK_MONOTONIC, &ts_chk);
                    if (ts_chk.tv_sec - ts_last_flush.tv_sec >= cp_interval) {
                        int hs2 = use_hint_order && ref ? ref->pos[1] : 0;
                        int ht2 = use_hint_order && ref ? ref->pos[2] : 0;
                        (void)cp_save_file(cp_path, n, target_length, total, hs2, ht2, use_hint_order, done_words.data(), words);
                        ts_last_flush = ts_chk;
                    }
                }
            }
        }
    }

    if (cp_path && *cp_path) {
        int hs = use_hint_order && ref ? ref->pos[1] : 0;
        int ht = use_hint_order && ref ? ref->pos[2] : 0;
        (void)cp_save_file(cp_path, n, target_length, total, hs, ht, use_hint_order, done_words.data(), words);
    }

    g_done = 1; if (g_vt_sec > 0.0) pthread_join(hb_th, NULL);

    if (found) {
        // Print in the same format as the C variant:
        // length=..\nmarks=..\npositions=..\ndistances=..\nmissing=..
        int L = res_local.length;
        int m = res_local.marks;
        printf("length=%d\nmarks=%d\npositions=", L, m);
        for (int i = 0; i < m; ++i) {
            printf("%d%s", res_local.pos[i], (i == m - 1) ? "" : " ");
        }
        // Build distance presence 1..L
        std::vector<unsigned char> present((size_t)L + 1, 0);
        for (int j = 0; j < m; ++j) {
            for (int i = 0; i < j; ++i) {
                int d = res_local.pos[j] - res_local.pos[i];
                if (d >= 1 && d <= L) present[(size_t)d] = 1;
            }
        }
        // distances line
        printf("\ndistances=");
        for (int d = 1, first = 1; d <= L; ++d) {
            if (present[(size_t)d]) {
                if (!first) putchar(' ');
                printf("%d", d);
                first = 0;
            }
        }
        // missing line
        printf("\nmissing=");
        for (int d = 1, first = 1; d <= L; ++d) {
            if (!present[(size_t)d]) {
                if (!first) putchar(' ');
                printf("%d", d);
                first = 0;
            }
        }
        putchar('\n');
        return 0;
    }
    fprintf(stderr, "No ruler found at L=%d (unexpected for LUT-verified -b).\n", target_length);
    return 1;
}
