
#include "FrameLib_ProcessingQueue.h"
#include "FrameLib_DSP.h"

void FrameLib_ProcessingQueue::process(FrameLib_DSP *object)
{
    while (object)
    {
        object->dependenciesReady();
        FrameLib_DSP *newObject = object->mNextInThread;
        object->mNextInThread = nullptr;
        object = newObject;
    }
    mQueueSize--;
}

void FrameLib_ProcessingQueue::start(FrameLib_DSP *object)
{
    mInQueue++;
    mQueueSize++;
    mWorkers.signal(1);
    object->mNextInThread = nullptr;
    process(object);
    serviceQueue();
    --mInQueue;
}

void FrameLib_ProcessingQueue::add(FrameLib_DSP *object)
{
    int32_t queueSize = ++mQueueSize;
    int32_t inQueue = mInQueue;
    int32_t sigSize = queueSize - inQueue;
    int32_t maxSigSize = mWorkers.size() - inQueue;
    
    sigSize = sigSize > maxSigSize ? maxSigSize : sigSize;
    
    if (sigSize > 0)
        mWorkers.signal(sigSize);
    
    object->mNextInThread = nullptr;
    OSAtomicFifoEnqueue(&mQueue, &object->mQueueItem, offsetof(QueueItem, mNext));
}

void FrameLib_ProcessingQueue::serviceQueue()
{
    struct timespec a;
    a.tv_sec = 0;
    a.tv_nsec = 100;
    
    while (true)
    {
        while (QueueItem *next = (QueueItem *) OSAtomicFifoDequeue(&mQueue, offsetof(QueueItem, mNext)))
            process(next->mThis);
        
        // FIX - quick reliable and non-contentious exit strategies are needed here...
        
        if (mQueueSize == 0)
            return;
        
        // FIX - how long is a good time to yield for in a high performance thread?
        
        // Keep pointless contention down and give way to other threads?
        
        nanosleep(&a, nullptr);
    }
}

// FIX - old version - need to add timeout to the above
/*
void FrameLib_ProcessingQueue::add(FrameLib_DSP *object)
{
    assert(object->mInputTime != FrameLib_TimeFormat::largest() && "Object has already reached the end of time");
    assert((!object->mNext || mTop == object) && "Object is already in the queue and not at the top");

    if (mTimedOut)
        return;
    
    if (!mTop)
    {
        // Queue is empty - add and start processing the queue
        
        mTop = mTail = object;
        
        // Get time
        
        mClock.start();
        unsigned long count = 0;
        
        while (mTop)
        {
            object = mTop;
            object->dependenciesReady();
            mTop = object->mNext;
            object->mNext = nullptr;
            
            // Every so often check whether we're taking too long
            
            if (++count == sProcessPerTimeCheck)
            {
                if (mClock.elapsed() > sMaxTime)
                {
                    mTimedOut = true;
                    
                    // Clear the list
                    
                    while (mTop)
                    {
                        mErrorReporter.reportError(kErrorDSP, mTop->getProxy(), "FrameLib - DSP time out - FrameLib is disabled in this context until this is resolved");
                        object = mTop;
                        mTop = object->mNext;
                        object->mNext = nullptr;
                    }
                }
                count = 0;
            }
        }
        
        mTail = nullptr;
    }
    else
    {
        // Add to the queue (which is already processing)
        
        mTail->mNext = object;
        mTail = object;
    }
}*/
