#include "btree.h"
#include <string>
#include <random>
#include <assert.h>
using namespace std;

extern "C" {
    void * getRegionFromID(uint ID);
    void setRegionFromID(uint ID, void *ptr);
}

typedef struct thread_data {
    uint32_t id;
    fastfair::btree *fair;
} thread_data_t;

fastfair::btree *ffair;
uint64_t *counters = NULL;

void run(char **argv) {
    std::cout << "Simple Example of Fast & Fair" << std::endl;

    uint64_t n = std::atoll(argv[1]);
    uint64_t *keys = new uint64_t[n];

    // Generate keys
    for (uint64_t i = 0; i < n; i++) {
        keys[i] = i + 1;
    }

    int num_thread = atoi(argv[2]);
    if(getRegionFromID(0) != NULL && getRegionFromID(1) != NULL)
    {
        ffair = (fastfair::btree *) getRegionFromID(0);
        assert(ffair);
        counters = (uint64_t *) getRegionFromID(1);
        assert(counters);
    } else {
        ffair = new fastfair::btree();
        setRegionFromID(0, ffair);
        //Make sure counters and hashtable aren't in the same line:
        // 64 bytes + n*sizeof(uint64_t) + 64 bytes.
        counters = (uint64_t *)calloc(n + 16, sizeof(uint64_t));
        counters = &counters[8];
        fastfair::clflush((char*)counters, sizeof(uint64_t)*n, false, true);
        setRegionFromID(1, counters);
    }
    thread_data_t *tds = (thread_data_t *) calloc(num_thread, sizeof(thread_data_t));

    std::atomic<int> next_thread_id(0);

    {
        // Load
        auto starttime = std::chrono::system_clock::now();
        auto func = [&]() {
            int thread_id = next_thread_id.fetch_add(1);
            tds[thread_id].id = thread_id;
            tds[thread_id].fair = ffair;

            uint64_t start_key = n / num_thread * (uint64_t)thread_id;
            uint64_t end_key = start_key + n / num_thread;
            uint64_t index = start_key;
            // First read to see the actual values made out to the memory
            for (; index < start_key + counters[thread_id]; index++) {
                char * val = tds[thread_id].fair->btree_search( keys[index]);
                if (val != (char *)keys[index]) {
                    std::cout << "[FAST_FAIR] wrong key read: " << (uint64_t)val << "expected: " << keys[index] << std::endl;
                    break;
                }
            }

            for (uint64_t i = index; i < end_key; i++) {
                tds[thread_id].fair->btree_insert( keys[i], (char *) keys[i]);
                counters[thread_id]++;
                fastfair::clflush((char*)&counters[thread_id], sizeof(counters[thread_id]), false, true);
            }
        };

        std::vector<std::thread> thread_group;

        for (int i = 0; i < num_thread; i++)
            thread_group.push_back(std::thread{func});

        for (int i = 0; i < num_thread; i++)
            thread_group[i].join();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now() - starttime);
        printf("Throughput: load, %f ,ops/us\n", (n * 1.0) / duration.count());
    }
    
    delete[] keys;
}


int main(int argc, char **argv) {
    if (argc != 3) {
        printf("usage: %s [n] [nthreads]\nn: number of keys (integer)\nnthreads: number of threads (integer)\n", argv[0]);
        return 1;
    }

    run(argv);
    return 0;
}