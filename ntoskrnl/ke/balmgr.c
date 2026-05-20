/*
* PROJECT:         ReactOS Kernel
* LICENSE:         GPL - See COPYING in the top level directory
* FILE:            ntoskrnl/ke/balmgr.c
* PURPOSE:         Balance Set Manager
* PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
*/

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

#define THREAD_BOOST_PRIORITY (LOW_REALTIME_PRIORITY - 1)
ULONG KiReadyScanLast;

/* PRIVATE FUNCTIONS *********************************************************/

VOID
NTAPI
KiScanReadyQueues(IN PKDPC Dpc,
                  IN PVOID DeferredContext,
                  IN PVOID SystemArgument1,
                  IN PVOID SystemArgument2)
{
    PULONG ScanLast = DeferredContext;
    ULONG ScanIndex = *ScanLast;
    ULONG Count = 10;
    ULONG Number = 16;
    PKPRCB Prcb;
    ULONG Index;
    ULONG WaitLimit = KeTickCount.LowPart - 300;
    ULONG Summary;
    KIRQL OldIrql;
    PLIST_ENTRY ListHead, NextEntry, TargetEntry;
    PKTHREAD Thread;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    /* 1. Prevent out-of-bounds array reads if CPU counts hot-plugged */
    if (ScanIndex >= (ULONG)KeNumberProcessors) ScanIndex = 0;

    /* 2. Safely resolve the PRCB target pointer BEFORE accessing its internal members */
    Prcb = KiProcessorBlock[ScanIndex];
    Index = Prcb->QueueIndex;

    /* Lock the dispatcher and PRCB */
    OldIrql = KiAcquireDispatcherLock();
    KiAcquirePrcbLock(Prcb);

    /* Check if there's any dynamic priority tier thread that needs an anti-starvation help */
    Summary = Prcb->ReadySummary & ((1 << THREAD_BOOST_PRIORITY) - 2);
    if (Summary)
    {
        /* Start scan loop */
        do
        {
            /* Normalize the index */
            if (Index > (THREAD_BOOST_PRIORITY - 1)) Index = 1;

            /* Loop for ready threads */
            if (Summary & PRIORITY_MASK(Index))
            {
                /* Sanity check */
                ASSERT(!IsListEmpty(&Prcb->DispatcherReadyListHead[Index]));

                /* Update summary and select list */
                Summary ^= PRIORITY_MASK(Index);
                ListHead = &Prcb->DispatcherReadyListHead[Index];
                NextEntry = ListHead->Flink;
                
                do
                {
                    /* FIX: Map structural bounds using ReadyListEntry, NOT WaitListEntry */
                    Thread = CONTAINING_RECORD(NextEntry, KTHREAD, ReadyListEntry);
                    ASSERT(Thread->Priority == Index);

                    /* Check if the thread has been waiting too long (>= 3 seconds) */
                    if (WaitLimit >= Thread->WaitTime)
                    {
                        /* Save target entry pointer for isolated extraction */
                        TargetEntry = NextEntry;

                        /* Rewind loop tracking pointer to the previous link to maintain a valid reference */
                        NextEntry = NextEntry->Blink;

                        ASSERT((Prcb->ReadySummary & PRIORITY_MASK(Index)));
                        
                        /* Extract the starved thread from the scheduler's live list */
                        if (RemoveEntryList(TargetEntry))
                        {
                            /* The list is empty now; update the PRCB ready summary bits */
                            Prcb->ReadySummary ^= PRIORITY_MASK(Index);
                        }

                        /* Verify priority decrement and calculate anti-starvation adjustments */
                        ASSERT((Thread->PriorityDecrement >= 0) &&
                               (Thread->PriorityDecrement <= Thread->Priority));
                        
                        Thread->PriorityDecrement += (THREAD_BOOST_PRIORITY - Thread->Priority);
                        
                        ASSERT((Thread->PriorityDecrement >= 0) &&
                               (Thread->PriorityDecrement <= THREAD_BOOST_PRIORITY));

                        /* Update priority metrics and shift onto the deferred ready list matrix */
                        Thread->Priority = THREAD_BOOST_PRIORITY;
                        Thread->Quantum = WAIT_QUANTUM_DECREMENT * 4;
                        KiInsertDeferredReadyList(Thread);
                        
                        Count--;
                    }

                    /* Safely advance to the next entry in the sequence */
                    NextEntry = NextEntry->Flink;
                    Number--;
                    
                } while ((NextEntry != ListHead) && (Number != 0) && (Count != 0));
            }

            /* Increase scanning priority index slot */
            Index++;
            
        } while ((Summary != 0) && (Number != 0) && (Count != 0));
    }

    /* Release the localized PRCB and global dispatcher architecture locks */
    KiReleasePrcbLock(Prcb);
    KiReleaseDispatcherLock(OldIrql);

    /* Update the queue index checkpoint state for next interval pass */
    if ((Count != 0) && (Number != 0))
    {
        /* We fully scanned the queues; cycle baseline back to index 1 */
        Prcb->QueueIndex = 1;
    }
    else
    {
        /* Search budget exhausted; store current slot index for resuming later */
        Prcb->QueueIndex = (UCHAR)Index;
    }

    /* Increment the target CPU slot metrics for the next background execution pass */
    ScanIndex++;
    if (ScanIndex >= (ULONG)KeNumberProcessors) ScanIndex = 0;

    /* Save the next target execution seed back into the context buffer safely */
    *ScanLast = ScanIndex;
}
VOID
NTAPI
KeBalanceSetManager(IN PVOID Context)
{
    KDPC ScanDpc;
    KTIMER PeriodTimer;
    LARGE_INTEGER DueTime;
    KWAIT_BLOCK WaitBlockArray[1];
    PVOID WaitObjects[1];
    NTSTATUS Status;

    /* Set us at a low real-time priority level */
    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);

    /* Setup the timer and scanner DPC */
    KeInitializeTimerEx(&PeriodTimer, SynchronizationTimer);
    KeInitializeDpc(&ScanDpc, KiScanReadyQueues, &KiReadyScanLast);

    /* Setup the periodic timer */
    DueTime.QuadPart = -1 * 10 * 1000 * 1000;
    KeSetTimerEx(&PeriodTimer, DueTime, 1000, &ScanDpc);

    /* Setup the wait objects */
    WaitObjects[0] = &PeriodTimer;
    //WaitObjects[1] = MmWorkingSetManagerEvent; // NO WS Management Yet!

    /* Start wait loop */
    do
    {
        /* Wait on our objects */
        Status = KeWaitForMultipleObjects(1,
                                          WaitObjects,
                                          WaitAny,
                                          Executive,
                                          KernelMode,
                                          FALSE,
                                          NULL,
                                          WaitBlockArray);
        switch (Status)
        {
            /* Check if our timer expired */
            case STATUS_WAIT_0:

                /* Adjust lookaside lists */
                //ExAdjustLookasideDepth();

                /* Call the working set manager */
                //MmWorkingSetManager();

                /* FIXME: Outswap stacks */

                /* Done */
                break;

            /* Check if the working set manager notified us */
            case STATUS_WAIT_1:

                /* Call the working set manager */
                //MmWorkingSetManager();
                break;

            /* Anything else */
            default:
                DPRINT1("BALMGR: Illegal wait status, %lx =\n", Status);
                break;
        }
    } while (TRUE);
}
