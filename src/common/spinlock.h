#pragma once

#include <common/defines.h>
#include <aarch64/intrinsic.h>

typedef struct {
    volatile bool locked;
} SpinLock;

void init_spinlock(SpinLock *);
bool try_acquire_spinlock(SpinLock *); // 暂时用不到
void acquire_spinlock(SpinLock *);
void release_spinlock(SpinLock *);
