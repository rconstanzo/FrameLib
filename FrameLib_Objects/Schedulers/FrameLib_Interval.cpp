
#include "FrameLib_Interval.h"

FrameLib_Interval::FrameLib_Interval(FrameLib_Context context, const FrameLib_Parameters::Serial *serialisedParameters, FrameLib_Proxy *proxy)
: FrameLib_Scheduler(context, proxy, &sParamInfo, 1, 1)
{
    mParameters.addDouble(kInterval, "interval", 64, 0);
    mParameters.setMin(0);
    
    mParameters.addEnum(kUnits, "units", 1);
    mParameters.addEnumItem(kSamples, "samples");
    mParameters.addEnumItem(kMS, "ms");
    mParameters.addEnumItem(kSeconds, "seconds");
    mParameters.addEnumItem(kHz, "hz");
    
    mParameters.addBool(kSwitchable, "switchable", false, 2);
    mParameters.setInstantiation();
    
    mParameters.addBool(kOn, "on", true, 3);

    mParameters.set(serialisedParameters);
    
    mSwitchable = mParameters.getBool(kSwitchable);
    
    setParameterInput(0);
    
    calculateInterval();
}

// Info

std::string FrameLib_Interval::objectInfo(bool verbose)
{
    return formatInfo("Schedules frames at regular intervals, which can be adjusted using the interval parameter: The output is an empty frame.",
                   "Schedules frames at regular intervals, which can be adjusted using the interval parameter.", verbose);
}

std::string FrameLib_Interval::inputInfo(unsigned long idx, bool verbose)
{
    return parameterInputInfo(verbose);
}

std::string FrameLib_Interval::outputInfo(unsigned long idx, bool verbose)
{
    return "Empty Trigger Frames";
}

// Parameter Info

FrameLib_Interval::ParameterInfo FrameLib_Interval::sParamInfo;

FrameLib_Interval::ParameterInfo::ParameterInfo()
{
    add("Sets the interval between frames in the units of the units parameter.");
    add("Sets the time units used to set the interval between frames.");
}

// Calculate Interval

void FrameLib_Interval::calculateInterval()
{
    FrameLib_TimeFormat interval = mParameters.getValue(kInterval);
    
    switch (static_cast<Units>(mParameters.getInt(kUnits)))
    {
        case kSamples:      break;
        case kMS:           interval = msToSamples(interval);           break;
        case kSeconds:      interval = secondsToSamples(interval);      break;
        case kHz:           interval = hzToSamples(interval);           break;
    }
    
    if (!interval)
        interval = FrameLib_TimeFormat::smallest();
    
    mInterval = interval;
}

// Object Reset

void FrameLib_Interval::objectReset()
{
    mRemaining = FrameLib_TimeFormat();
    calculateInterval();
}

// Update and Schedule

void FrameLib_Interval::update()
{
    if (mParameters.changed(kInterval) || mParameters.changed(kUnits))
        calculateInterval();
}

FrameLib_Interval::SchedulerInfo FrameLib_Interval::schedule(bool newFrame, bool noAdvance)
{
    bool on = mParameters.getBool(kOn) || !mSwitchable;
    bool start = on && !mRemaining.greaterThanZero();
    
    if (noAdvance)
        return SchedulerInfo();
    
    mRemaining = start ? mInterval : mRemaining;
    
    // Calculate if we can schedule up to the next new frame and if not then adjust the interval
    // N.B. - if switchable for completion the state must not be able to change *at* or before the next inputTime

    FrameLib_TimeFormat tillNextInput = getInputTime() - getCurrentTime();
    bool complete = !mSwitchable || (on && (mRemaining < tillNextInput));
    FrameLib_TimeFormat interval = complete ? mRemaining : tillNextInput;
    
    // Update the remaining time and schedule
        
    mRemaining = on ? mRemaining - interval : FrameLib_TimeFormat();
    
    return SchedulerInfo(interval, start, complete);
}
