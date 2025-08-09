#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <ctime>
#include <pthread.h>
#include <omp.h>
#include <string>
#include <vector>
#include <algorithm>

extern "C" {
#include "../include/golomb.h"
}

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

/* ---------------- Scalar DFS (no SIMD) ---------------- */
static inline void set_bit64(uint64_t *bs, int idx) { bs[idx >> 6] |= 1ULL << (idx & 63); }
static inline void clr_bit64(uint64_t *bs, int idx) { bs[idx >> 6] &= ~(1ULL << (idx & 63)); }
static inline int  test_bit64(const uint64_t *bs, int idx) { return (bs[idx >> 6] >> (idx & 63)) & 1ULL; }

static bool dfs_scalar(int depth, int n, int target_len, int *pos, uint64_t *dist_bs, bool verbose)
{
    if (depth == n) return pos[n - 1] == target_len;
    int last = pos[depth - 1];
    if (last + (n - depth) > target_len) return false;
    int max_next = target_len - (n - depth - 1);
    if (depth == 1) {
        int limit = target_len / 2;
        if (limit < last + 1) limit = last + 1;
        if (max_next > limit) max_next = limit;
    }
    for (int next = last + 1; next <= max_next; ++next) {
        bool ok = true;
        for (int i = 0; i < depth; ++i) {
            int d = next - pos[i];
            if (test_bit64(dist_bs, d)) { ok = false; break; }
        }
        if (!ok) continue;
        pos[depth] = next;
        for (int i = 0; i < depth; ++i) set_bit64(dist_bs, next - pos[i]);
        if (verbose && depth < 6) { printf("depth %d add %d\n", depth, next); }
        if (dfs_scalar(depth + 1, n, target_len, pos, dist_bs, verbose)) return true;
        for (int i = 0; i < depth; ++i) clr_bit64(dist_bs, next - pos[i]);
    }
    return false;
}

/* ---------------- GPU candidate prefilter ---------------- */
struct Cand { int s, t, score; };

__global__ void prefilter_kernel(int n, int target_len, const Cand *cands, int64_t total, unsigned char *ok)
{
    int64_t i = blockIdx.x * 1LL * blockDim.x + threadIdx.x;
    if (i >= total) return;
    int s = cands[i].s;
    int t = cands[i].t;
    int max_next = target_len - (n - 3 - 1); // depth=3 => max_next = L - (n-4)
    if (max_next <= t) { ok[i] = 0; return; }
    // Existing distances: s, t, t-s
    int ds23 = t - s;
    unsigned char good = 0;
    for (int next = t + 1; next <= max_next; ++next) {
        int d0 = next;           // next - 0
        int d1 = next - s;       // next - s
        int d2 = next - t;       // next - t
        // check against existing distances
        if (d0 == s || d0 == t || d0 == ds23) continue;
        if (d1 == s || d1 == t || d1 == ds23) continue;
        if (d2 == s || d2 == t || d2 == ds23) continue;
        // also ensure d0, d1, d2 are pairwise distinct (they are, since 0 < s < t < next)
        good = 1; break;
    }
    ok[i] = good;
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
            if (minutes > 0) printf("[VT] %02d:%06.3f elapsed – current L=%d\n", minutes, seconds, L);
            else              printf("[VT] %.3f s elapsed – current L=%d\n", seconds, L);
            fflush(stdout);
        }
        struct timespec req = { (time_t)g_vt_sec, (long)((g_vt_sec - (time_t)g_vt_sec) * 1e9) };
        nanosleep(&req, NULL);
    }
    return NULL;
}

/* ---------------- Main (CUDA-enhanced mp) ---------------- */
int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <n> [-b] [-v] [-f <file>] [-fi <sec>] [-vt <min>]\n", argv[0]);
        return 1;
    }
    int n = atoi(argv[1]);
    bool verbose = false;
    bool use_b = false;
    const char *cp_path = NULL;
    int cp_interval = 60;
    double vt_min = 0.0;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0) verbose = true;
        else if (strcmp(argv[i], "-b") == 0) use_b = true;
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) { cp_path = argv[++i]; }
        else if (strcmp(argv[i], "-fi") == 0 && i + 1 < argc) { cp_interval = atoi(argv[++i]); if (cp_interval <= 0) cp_interval = 60; }
        else if (strcmp(argv[i], "-vt") == 0 && i + 1 < argc) { vt_min = atof(argv[++i]); }
        else {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            return 2;
        }
    }

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
    printf("Start time: %s\n", start_iso);
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
    int use_hint_order = (ref && getenv("GOLOMB_NO_HINTS") == NULL) ? 1 : 0;
    for (int s = 1; s <= second_max; ++s) {
        for (int t = s + 1; t <= T; ++t) {
            int score = 0;
            if (use_hint_order) {
                int ds = s - ref->pos[1]; if (ds < 0) ds = -ds;
                int dt = t - ref->pos[2]; if (dt < 0) dt = -dt;
                score = ds + dt;
            }
            cands.push_back({s, t, score});
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

    // Optional GPU prefilter: mark candidates that can advance one more mark without immediate duplicates
    int device_count = 0;
    cudaError_t derr = cudaGetDeviceCount(&device_count);
    int rt_ver = 0, dr_ver = 0; cudaRuntimeGetVersion(&rt_ver); cudaDriverGetVersion(&dr_ver);
    const char *cvd = getenv("CUDA_VISIBLE_DEVICES");
    printf("[CUDA] Runtime=%d Driver=%d CUDA_VISIBLE_DEVICES=%s\n", rt_ver, dr_ver, cvd ? cvd : "(unset)");
    if (derr != cudaSuccess) {
        printf("[CUDA] cudaGetDeviceCount error: %s (%d)\n", cudaGetErrorString(derr), (int)derr);
    }
    if (device_count > 0) {
        int dev = 0;
        cudaError_t sderr = cudaSetDevice(dev);
        if (sderr != cudaSuccess) {
            printf("[CUDA] cudaSetDevice(%d) failed: %s (%d)\n", dev, cudaGetErrorString(sderr), (int)sderr);
        }
        // create context
        cudaFree(0);
        cudaDeviceProp prop; cudaGetDeviceProperties(&prop, dev);
        printf("[CUDA] Using device %d: %s\n", dev, prop.name);
    } else {
        printf("[CUDA] No CUDA device found – running CPU-only prefilter.\n");
    }
    if (device_count > 0 && total > 0) {
        Cand *d_cands = nullptr; unsigned char *d_ok = nullptr; unsigned char *h_ok = (unsigned char*)malloc((size_t)total);
        cudaMalloc(&d_cands, sizeof(Cand) * (size_t)total);
        cudaMalloc(&d_ok, (size_t)total);
        cudaMemcpy(d_cands, cands.data(), sizeof(Cand) * (size_t)total, cudaMemcpyHostToDevice);
        int threads = 256; int blocks = (int)((total + threads - 1) / threads);
        prefilter_kernel<<<blocks, threads>>>(n, target_length, d_cands, total, d_ok);
        cudaDeviceSynchronize();
        cudaMemcpy(h_ok, d_ok, (size_t)total, cudaMemcpyDeviceToHost);
        // Rebuild candidate order: keep original order, but place ok==1 first
        {
            std::vector<Cand> reordered; reordered.reserve((size_t)total);
            size_t ok_cnt = 0;
            // first pass: ok == 1
            for (size_t i = 0; i < (size_t)total; ++i) {
                if (h_ok[i]) { reordered.push_back(cands[i]); ++ok_cnt; }
            }
            // second pass: ok == 0
            for (size_t i = 0; i < (size_t)total; ++i) {
                if (!h_ok[i]) reordered.push_back(cands[i]);
            }
            cands.swap(reordered);
            printf("[CUDA] Prefiltered %lld candidates: %zu pass first check.\n", total, ok_cnt);
        }
        cudaFree(d_cands); cudaFree(d_ok); free(h_ok);
    }

    // Fast-lane: try exact LUT pair on CPU first if hints enabled
    volatile int found = 0; ruler_t res_local{};
    if (ref && getenv("GOLOMB_NO_HINTS") == NULL) {
        int s0 = ref->pos[1], t0 = ref->pos[2];
        if (s0 >= 1 && s0 <= second_max && t0 > s0 && t0 <= T) {
            uint64_t bs[BS_WORDS] = {0};
            int pos[MAX_MARKS]; pos[0] = 0; pos[1] = s0; pos[2] = t0;
            set_bit64(bs, s0);
            int d13 = t0; int d23 = t0 - s0;
            if (!test_bit64(bs, d13) && !test_bit64(bs, d23)) {
                set_bit64(bs, d13); set_bit64(bs, d23);
                if (dfs_scalar(3, n, target_length, pos, bs, verbose)) {
                    res_local.marks = n; res_local.length = pos[n - 1]; memcpy(res_local.pos, pos, n * sizeof(int)); found = 1;
                }
            }
        }
    }

    struct timespec ts_last_flush; clock_gettime(CLOCK_MONOTONIC, &ts_last_flush);

    // Parallel CPU search across candidates (static mp), respecting checkpoint
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
        uint64_t bs[BS_WORDS] = {0};
        int pos[MAX_MARKS]; pos[0] = 0; pos[1] = second; pos[2] = third;
        set_bit64(bs, second);
        int d13 = third; int d23 = third - second;
        if (test_bit64(bs, d13) || test_bit64(bs, d23)) goto mark_done;
        set_bit64(bs, d13); set_bit64(bs, d23);
        if (dfs_scalar(3, n, target_length, pos, bs, verbose)) {
            #pragma omp critical
            {
                if (!found) { res_local.marks = n; res_local.length = pos[n - 1]; memcpy(res_local.pos, pos, n * sizeof(int)); found = 1; }
            }
        }
    mark_done:
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
        // print ruler
        print_ruler(&res_local);
        return 0;
    }
    fprintf(stderr, "No ruler found at L=%d (unexpected for LUT-verified -b).\n", target_length);
    return 1;
}
