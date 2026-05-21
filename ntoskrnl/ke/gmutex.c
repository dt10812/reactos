/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ke/gmutex.c
 * PURPOSE:         Implements Public Export Wrappers for Guarded Mutexes
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 * Filip Navara (navaraf@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* Undefine the macros we implement as public exports here to prevent recursion */
#undef KeInitializeGuardedMutex
#undef KeAcquireGuardedMutex
#undef KeReleaseGuardedMutex
#undef KeAcquireGuardedMutexUnsafe
#undef KeReleaseGuardedMutexUnsafe
#undef KeTryToAcquireGuardedMutex

/* PUBLIC FUNCTIONS **********************************************************/

/**
 * @name KeInitializeGuardedMutex
 * @implemented
 * Initializes a lightweight Guarded Mutex synchronization object.
 */
VOID
FASTCALL
KeInitializeGuardedMutex(OUT PKGUARDED_MUTEX GuardedMutex)
{
    ASSERT(GuardedMutex != NULL);

    /* Invoke the architecture-specific inline engine */
    _KeInitializeGuardedMutex(GuardedMutex);
}

/**
 * @name KeAcquireGuardedMutex
 * @implemented
 * Acquires exclusive ownership of a Guarded Mutex, implicitly entering
 * a guarded region to disable all Kernel APC processing loops.
 */
VOID
FASTCALL
KeAcquireGuardedMutex(IN PKGUARDED_MUTEX GuardedMutex)
{
    ASSERT(GuardedMutex != NULL);
    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    /* Invoke the architecture-specific inline engine */
    _KeAcquireGuardedMutex(GuardedMutex);
}

/**
 * @name KeReleaseGuardedMutex
 * @implemented
 * Releases ownership of a Guarded Mutex and exits the guarded execution region.
 */
VOID
FASTCALL
KeReleaseGuardedMutex(IN OUT PKGUARDED_MUTEX GuardedMutex)
{
    ASSERT(GuardedMutex != NULL);
    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    /* Invoke the architecture-specific inline engine */
    _KeReleaseGuardedMutex(GuardedMutex);
}

/**
 * @name KeAcquireGuardedMutexUnsafe
 * @implemented
 * Acquires a Guarded Mutex without mutating the thread's guarded region.
 * The caller must already reside within a manually declared critical/guarded frame.
 */
VOID
FASTCALL
KeAcquireGuardedMutexUnsafe(IN OUT PKGUARDED_MUTEX GuardedMutex)
{
    ASSERT(GuardedMutex != NULL);
    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    /* Invoke the architecture-specific inline engine */
    _KeAcquireGuardedMutexUnsafe(GuardedMutex);
}

/**
 * @name KeReleaseGuardedMutexUnsafe
 * @implemented
 * Releases a Guarded Mutex without mutating the underlying APC region environment.
 */
VOID
FASTCALL
KeReleaseGuardedMutexUnsafe(IN OUT PKGUARDED_MUTEX GuardedMutex)
{
    ASSERT(GuardedMutex != NULL);
    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    /* Invoke the architecture-specific inline engine */
    _KeReleaseGuardedMutexUnsafe(GuardedMutex);
}

/**
 * @name KeTryToAcquireGuardedMutex
 * @implemented
 * Polls the lock state to acquire the Guarded Mutex immediately if free.
 * Returns TRUE if ownership was claimed, FALSE if it is already locked.
 */
BOOLEAN
FASTCALL
KeTryToAcquireGuardedMutex(IN OUT PKGUARDED_MUTEX GuardedMutex)
{
    ASSERT(GuardedMutex != NULL);
    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    /* Invoke the architecture-specific inline engine and return the state */
    return _KeTryToAcquireGuardedMutex(GuardedMutex);
}

/**
 * @name KeEnterGuardedRegion
 * @implemented
 * Enters a guarded region. This causes all (incl. special kernel) APCs
 * to be disabled.
 */
VOID
NTAPI
_KeEnterGuardedRegion(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();

    /* Guarded regions manipulate the core scheduler thread state structures.
     * Running this at DISPATCH_LEVEL or above would cause severe system stability issues.
     */
    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    Thread->SpecialApcDisable--;
}

/**
 * @name KeLeaveGuardedRegion
 * @implemented
 * Leaves a guarded region and delivers pending APCs if possible.
 */
VOID
NTAPI
_KeLeaveGuardedRegion(VOID)
{
    /* FIX: STRIP RECURSIVE RE-ENTRANCY PATHS BY RESOLVING LOCAL POINTER TO ACTIVE KTHREAD DEFINITION */
    PKTHREAD Thread = KeGetCurrentThread();

    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);
    Thread->SpecialApcDisable++;

    if ((Thread->SpecialApcDisable == 0) && 
        (!IsListEmpty(&Thread->ApcState.ApcListHead[KernelMode])))
    {
        if (KeGetCurrentIrql() == PASSIVE_LEVEL)
        {
            KiCheckForPendingApc(Thread);
        }
        else
        {
            HalRequestSoftwareInterrupt(APC_LEVEL);
        }
    }
}
/* EOF */