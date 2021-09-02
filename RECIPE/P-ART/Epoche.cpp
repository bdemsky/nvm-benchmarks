//
// Created by florian on 22.10.15.
//
#ifndef EPOCHE_CPP
#define EPOCHE_CPP

#include <assert.h>
#include <iostream>
#include <limits>
#include "Epoche.h"
#include "libpmem.h"

using namespace ART;

inline DeletionList::DeletionList() {
    #ifdef VERIFYFIX
    jaaru_ignore_analysis((char*)&headDeletionList,sizeof(headDeletionList));
    jaaru_ignore_analysis((char*)&freeLabelDeletes,sizeof(freeLabelDeletes));
    jaaru_ignore_analysis((char*)&deletitionListCount,sizeof(deletitionListCount));
    jaaru_ignore_analysis((char*)&localEpoche,sizeof(localEpoche));
    jaaru_ignore_analysis((char*)&thresholdCounter,sizeof(thresholdCounter));
    jaaru_ignore_analysis((char*)&deleted,sizeof(deleted));
    jaaru_ignore_analysis((char*)&added,sizeof(added));
    #endif
}

inline DeletionList::~DeletionList() {
    assert(deletitionListCount == 0 && headDeletionList == nullptr);
    LabelDelete *cur = nullptr, *next = freeLabelDeletes;
    while (next != nullptr) {
        cur = next;
        next = cur->next;
        delete cur;
    }
    freeLabelDeletes = nullptr;
}

inline std::size_t DeletionList::size() {
    return deletitionListCount;
}

inline void DeletionList::remove(LabelDelete *label, LabelDelete *prev) {
    if (prev == nullptr) {
        headDeletionList = label->next;
    } else {
        prev->next = label->next;
    }
    deletitionListCount -= label->nodesCount;

    label->next = freeLabelDeletes;
    #ifdef VERIFYFIX
    jaaru_ignore_analysis((char*)&label->next,sizeof(label->next));
    #endif
    freeLabelDeletes = label;
    deleted += label->nodesCount;
}

inline void DeletionList::add(void *n, uint64_t globalEpoch) {
    #ifdef VERIFYFIX
    jaaru_ignore_analysis((char*)&deletitionListCount,sizeof(deletitionListCount));
    #endif
    deletitionListCount++;
    LabelDelete *label;
    if (headDeletionList != nullptr && headDeletionList->nodesCount < headDeletionList->nodes.size()) {
        label = headDeletionList;
    } else {
        if (freeLabelDeletes != nullptr) {
            label = freeLabelDeletes;
            freeLabelDeletes = freeLabelDeletes->next;
        } else {
            label = new LabelDelete();
        }
        label->nodesCount = 0;
        label->next = headDeletionList;
#ifdef VERIFYFIX
       jaaru_ignore_analysis((char*)&(label->next),sizeof(label->next));
#endif
        headDeletionList = label;
    }
    label->nodes[label->nodesCount] = n;
    label->nodesCount++;
#ifdef VERIFYFIX
    jaaru_ignore_analysis((char*)&label->nodesCount,sizeof(label->nodesCount));
#endif
    label->epoche = globalEpoch;
#ifdef VERIFYFIX
    jaaru_ignore_analysis((char*)&added,sizeof(added));
#endif
    added++;
}

inline LabelDelete *DeletionList::head() {
    return headDeletionList;
}

inline void Epoche::enterEpoche(ThreadInfo &epocheInfo) {
    unsigned long curEpoche = currentEpoche.load(std::memory_order_relaxed);
    epocheInfo.getDeletionList().localEpoche.store(curEpoche, std::memory_order_release);
#ifdef VERIFYFIX
    jaaru_ignore_analysis((char*)&epocheInfo.getDeletionList().localEpoche,sizeof(epocheInfo.getDeletionList().localEpoche));
#endif
}

inline void Epoche::markNodeForDeletion(void *n, ThreadInfo &epocheInfo) {
#ifndef LOCK_INIT
    epocheInfo.getDeletionList().add(n, currentEpoche.load());
#ifdef VERIFYFIX
    jaaru_ignore_analysis((char*)&epocheInfo.getDeletionList().thresholdCounter,sizeof(epocheInfo.getDeletionList().thresholdCounter));
#endif
    epocheInfo.getDeletionList().thresholdCounter++;
#endif
}

inline void Epoche::exitEpocheAndCleanup(ThreadInfo &epocheInfo) {
    DeletionList &deletionList = epocheInfo.getDeletionList();
    if ((deletionList.thresholdCounter & (64 - 1)) == 1) {
#ifdef VERIFYFIX
        jaaru_ignore_analysis((char*)&currentEpoche,sizeof(currentEpoche));
#endif
        currentEpoche++;
    }
    if (deletionList.thresholdCounter > startGCThreshhold) {
        if (deletionList.size() == 0) {
            deletionList.thresholdCounter = 0;
            return;
        }
        deletionList.localEpoche.store(std::numeric_limits<uint64_t>::max());

        uint64_t oldestEpoche = std::numeric_limits<uint64_t>::max();
        for (uint i=0; i< number_of_threads; i++) {
            auto &epoche = deletionLists[i];    
            auto e = epoche.localEpoche.load();
            if (e < oldestEpoche) {
                oldestEpoche = e;
            }
        }

        LabelDelete *cur = deletionList.head(), *next, *prev = nullptr;
        while (cur != nullptr) {
            next = cur->next;

            if (cur->epoche < oldestEpoche) {
                for (std::size_t i = 0; i < cur->nodesCount; ++i) {
                    operator delete(cur->nodes[i]);
                }
                deletionList.remove(cur, prev);
            } else {
                prev = cur;
            }
            cur = next;
        }
        deletionList.thresholdCounter = 0;
    }
}

inline Epoche::~Epoche() {
    uint64_t oldestEpoche = std::numeric_limits<uint64_t>::max();
    for (uint i=0; i< number_of_threads; i++) {
        auto &epoche = deletionLists[i];
        auto e = epoche.localEpoche.load();
        if (e < oldestEpoche) {
            oldestEpoche = e;
        }
    }
    for (uint i=0; i< number_of_threads; i++) {
        auto &d = deletionLists[i];
        LabelDelete *cur = d.head(), *next, *prev = nullptr;
        while (cur != nullptr) {
            next = cur->next;

            assert(cur->epoche < oldestEpoche);
            for (std::size_t i = 0; i < cur->nodesCount; ++i) {
                operator delete(cur->nodes[i]);
            }
            d.remove(cur, prev);
            cur = next;
        }
    }
    delete [] deletionLists;
}

inline void Epoche::showDeleteRatio() {
    for (uint i=0; i< number_of_threads; i++) {
        auto &d = deletionLists[i];
        std::cout << "deleted " << d.deleted << " of " << d.added << std::endl;
    }
}

inline ThreadInfo::ThreadInfo(Epoche &epoche, uint64_t id)
        : epoche(epoche), deletionList(epoche.getDeletionList(id)) { }

inline DeletionList &ThreadInfo::getDeletionList() const {
    return deletionList;
}

inline Epoche &ThreadInfo::getEpoche() const {
    return epoche;
}

#endif //EPOCHE_CPP