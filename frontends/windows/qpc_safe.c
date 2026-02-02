/*
 * Copyright 2024 NetSurf Browser Project
 *
 * QueryPerformanceCounter runtime detection for Windows XP compatibility
 */

#include "qpc_safe.h"
#include "wisp/utils/log.h"
#include <intrin.h>
#include <math.h>
#include <stdio.h>
#include <windows.h>

static enum qpc_reliability g_qpc_status = QPC_RELIABILITY_UNKNOWN;
static LARGE_INTEGER g_qpc_frequency;
static LARGE_INTEGER g_qpc_offset;
static DWORD g_tick_offset;

/**
 * Test CPU for constant TSC feature (Intel/AMD)
 */
static BOOL has_constant_tsc(void)
{
    int cpuInfo[4] = {0};

    /* Check if CPUID supports extended features */
    __cpuid(cpuInfo, 0x80000000);
    if (cpuInfo[0] < 0x80000007) {
        return FALSE; /* No extended features */
    }

    /* Check AMD constant TSC bit */
    __cpuid(cpuInfo, 0x80000007);
    return (cpuInfo[3] & (1 << 8)) != 0; /* Constant TSC bit */
}

/**
 * Test QPC consistency across multiple cores
 */
static BOOL test_qpc_multicore_consistency(void)
{
    LARGE_INTEGER start, end, freq;
    DWORD_PTR process_mask, system_mask;
    DWORD_PTR old_thread_mask;
    HANDLE current_thread;

    if (!GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask)) {
        return FALSE;
    }

    /* Get current thread handle */
    current_thread = GetCurrentThread();

    /* Save current thread affinity */
    old_thread_mask = SetThreadAffinityMask(current_thread, 1);
    if (old_thread_mask == 0) {
        return FALSE;
    }

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    Sleep(50); /* 50ms test on core 0 */
    QueryPerformanceCounter(&end);

    /* Calculate time on core 0 */
    double core0_time = (double)(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;

    /* Test on different cores if available */
    DWORD core_count = 0;
    DWORD_PTR mask = 1;

    for (int i = 1; i < 32 && core_count < 3; i++) {
        if (process_mask & (1 << i)) {
            SetThreadAffinityMask(current_thread, 1 << i);

            QueryPerformanceCounter(&start);
            Sleep(50); /* 50ms test on core i */
            QueryPerformanceCounter(&end);

            double core_time = (double)(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;

            /* Allow 2% tolerance between cores */
            if (fabs(core_time - core0_time) > (core0_time * 0.02)) {
                SetThreadAffinityMask(current_thread, old_thread_mask);
                return FALSE; /* Significant difference between
                         cores */
            }
            core_count++;
        }
    }

    /* Restore original thread affinity */
    SetThreadAffinityMask(current_thread, old_thread_mask);
    return TRUE;
}

/**
 * Test QPC monotonicity over multiple samples
 */
static BOOL test_qpc_monotonicity(void)
{
    LARGE_INTEGER prev, current;
    int backwards_count = 0;

    QueryPerformanceCounter(&prev);

    for (int i = 0; i < 10000; i++) {
        QueryPerformanceCounter(&current);

        if (current.QuadPart < prev.QuadPart) {
            backwards_count++;
            if (backwards_count > 5) { /* Allow occasional glitch */
                return FALSE;
            }
        }
        prev = current;
    }

    return TRUE;
}

/**
 * Test QPC timing accuracy against Sleep()
 */
static BOOL test_qpc_timing_accuracy(void)
{
    LARGE_INTEGER freq, start, end;
    double elapsed_ms, expected_ms;

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    Sleep(100); /* 100ms */

    QueryPerformanceCounter(&end);

    elapsed_ms = (double)(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;
    expected_ms = 100.0;

    /* Allow 10% tolerance for Sleep() inaccuracy */
    return fabs(elapsed_ms - expected_ms) < (expected_ms * 0.10);
}

/**
 * Comprehensive QPC reliability test
 */
enum qpc_reliability qpc_test_reliability(void)
{
    BOOL has_constant_tsc_val = FALSE;
    BOOL multicore_ok = FALSE;
    BOOL monotonicity_ok = FALSE;
    BOOL timing_ok = FALSE;

    /* Test 1: CPU feature detection */
    has_constant_tsc_val = has_constant_tsc();

    /* Test 2: Multi-core consistency (most important for XP) */
    multicore_ok = test_qpc_multicore_consistency();

    /* Test 3: Monotonicity test */
    monotonicity_ok = test_qpc_monotonicity();

    /* Test 4: Timing accuracy */
    timing_ok = test_qpc_timing_accuracy();

    /* Decision logic */
    if (multicore_ok && monotonicity_ok && timing_ok) {
        return QPC_RELIABILITY_GOOD;
    } else if (has_constant_tsc_val && multicore_ok) {
        return QPC_RELIABILITY_GOOD;
    } else if (multicore_ok || monotonicity_ok) {
        return QPC_RELIABILITY_UNSTABLE;
    } else {
        return QPC_RELIABILITY_BAD;
    }
}

/**
 * Initialize QPC safe timing subsystem
 */
void qpc_safe_init(void)
{
    if (g_qpc_status != QPC_RELIABILITY_UNKNOWN) {
        return; /* Already initialized */
    }

    /* Get QPC frequency */
    if (!QueryPerformanceFrequency(&g_qpc_frequency)) {
        g_qpc_status = QPC_RELIABILITY_BAD;
        return;
    }

    /* Test QPC reliability */
    g_qpc_status = qpc_test_reliability();

    /* Initialize baseline values */
    if (g_qpc_status == QPC_RELIABILITY_GOOD) {
        QueryPerformanceCounter(&g_qpc_offset);
        NSLOG(wisp, INFO, "QPC initialized successfully (High Precision)");
    } else if (g_qpc_status == QPC_RELIABILITY_UNSTABLE) {
        /* Treat unstable as BAD for safety - fallback to GetTickCount64
         */
        g_tick_offset = GetTickCount64();
        NSLOG(wisp, INFO, "QPC initialized (Unstable - potential drift), falling back to GetTickCount64");
    } else {
        /* Use GetTickCount64() as baseline for unreliable QPC */
        g_tick_offset = GetTickCount64();
        NSLOG(wisp, INFO, "QPC unreliable, falling back to GetTickCount64 (Low Precision)");
    }
}

/**
 * Get safe monotonic time in milliseconds
 */
bool qpc_safe_get_monotonic_ms(uint64_t *time_ms)
{
    if (g_qpc_status == QPC_RELIABILITY_UNKNOWN) {
        qpc_safe_init();
    }

    if (time_ms == NULL) {
        return false;
    }

    switch (g_qpc_status) {
    case QPC_RELIABILITY_GOOD: {
        /* Use QPC for high precision */
        LARGE_INTEGER current;
        if (!QueryPerformanceCounter(&current)) {
            return false;
        }

        /* Calculate milliseconds from offset */
        LARGE_INTEGER elapsed;
        elapsed.QuadPart = current.QuadPart - g_qpc_offset.QuadPart;
        *time_ms = (uint64_t)((elapsed.QuadPart * 1000) / g_qpc_frequency.QuadPart);
        break;
    }

    case QPC_RELIABILITY_UNSTABLE:
    case QPC_RELIABILITY_BAD: {
        /* Fall back to GetTickCount64() */
        *time_ms = GetTickCount64() - g_tick_offset;
        break;
    }

    default:
        return false;
    }

    return true;
}

/**
 * Get safe monotonic time in microseconds
 */
bool qpc_safe_get_monotonic_us(uint64_t *time_us)
{
    if (g_qpc_status == QPC_RELIABILITY_UNKNOWN) {
        qpc_safe_init();
    }

    if (time_us == NULL) {
        return false;
    }

    switch (g_qpc_status) {
    case QPC_RELIABILITY_GOOD: {
        /* Use QPC for high precision */
        LARGE_INTEGER current;
        if (!QueryPerformanceCounter(&current)) {
            return false;
        }

        /* Calculate microseconds from offset */
        LARGE_INTEGER elapsed;
        elapsed.QuadPart = current.QuadPart - g_qpc_offset.QuadPart;

        /* To avoid overflow: seconds * 1000000 + (remainder * 1000000)
         * / freq */
        uint64_t seconds = elapsed.QuadPart / g_qpc_frequency.QuadPart;
        uint64_t remainder = elapsed.QuadPart % g_qpc_frequency.QuadPart;

        *time_us = (seconds * 1000000) + ((remainder * 1000000) / g_qpc_frequency.QuadPart);
        break;
    }

    case QPC_RELIABILITY_UNSTABLE:
    case QPC_RELIABILITY_BAD: {
        /* Fall back to GetTickCount64() */
        *time_us = (GetTickCount64() - g_tick_offset) * 1000;
        break;
    }

    default:
        return false;
    }

    return true;
}
