
#include "Max_Base.h"

#include "FrameLib_Multichannel.h"
#include "FrameLib_DSP.h"
#include "FrameLib_Globals.h"

#include <vector>

// FIX - sort out formatting style/ naming?
// FIX - improve reporting of extra connections + look into feedback detection...
// FIX - think about adding assist helpers for this later...
// FIX - threadsafety??
// FIX - look at static class definitions

t_class *objectClass = NULL;
t_class *wrapperClass = NULL;
t_class *mutatorClass = NULL;

//////////////////////////////////////////////////////////////////////////
///////////////////////////// Sync Check Class ///////////////////////////
//////////////////////////////////////////////////////////////////////////

struct SyncCheck
{
    SyncCheck() : mTime(-1), mObject(NULL) {}
    
    bool operator()()
    {
        SyncCheck *info = syncGet();
        
        if (!info)
            return false;
        
        void *object = info->mObject;
        long time = info->mTime;
        
        if (time == mTime && object == mObject)
            return false;
        
        mTime = time;
        mObject = object;
        
        return true;
    }
    
    static SyncCheck *syncGet()
    {
        return (SyncCheck *) gensym("__FrameLib__SYNC__")->s_thing;
    }
    
    long mTime;
    void *mObject;
    
};

SyncCheck syncInfo;

void syncSet(void *object, long time)
{
    syncInfo.mObject = object;
    syncInfo.mTime = time;
    gensym("__FrameLib__SYNC__")->s_thing = (t_object *) (object ? &syncInfo : NULL);
}

//////////////////////////////////////////////////////////////////////////
////////////////////// Mutator for Synchronisation ///////////////////////
//////////////////////////////////////////////////////////////////////////

class Mutator : public MaxBase
{
    
public:
    
    Mutator(t_symbol *sym, long ac, t_atom *av)
    {
        mOutlet = outlet_new(this, "sync");
    }
    
    static void classInit(t_class *c, t_symbol *nameSpace, const char *classname)
    {
        addMethod<Mutator, &Mutator::mutate>(c, "signal");
    }
    
    void mutate(t_symbol *sym, long ac, t_atom *av)
    {
        syncSet(this, gettime());
        outlet_anything(mOutlet, gensym("sync"), 0, 0);
        syncSet(NULL, gettime());
    }
    
private:
    
    void *mOutlet;
};

//////////////////////////////////////////////////////////////////////////
////////////////////// Wrapper for Synchronisation ///////////////////////
//////////////////////////////////////////////////////////////////////////

template <class T> class Wrapper : public MaxBase
{
    
public:
    
    Wrapper(t_symbol *s, long argc, t_atom *argv)
    {
        // Create Patcher (you must report this as a subpatcher to get audio working)
        
        t_dictionary *d = dictionary_new();
        t_atom a;
        t_atom *av = NULL;
        long ac = 0;
        
        atom_setparse(&ac, &av, "@defrect 0 0 300 300");
        attr_args_dictionary(d, ac, av);
        atom_setobj(&a, d);
        mPatch = (t_object *)object_new_typed(CLASS_NOBOX, gensym("jpatcher"),1, &a);
        
        // Create Objects
        
        // FIX - this should not be a macor for the name (needs to be stored somehow?? with the class...
        
        char name[256];
        sprintf(name, "unsynced.%s", OBJECT_NAME);
        
        // FIX - make me better
        
        char *text = NULL;
        long textSize = 0;
        
        atom_gettext(argc, argv, &textSize, &text, 0);
        mObject = jbox_get_object((t_object *) newobject_sprintf(mPatch, "@maxclass newobj @text \"%s %s\" @patching_rect 0 0 30 10", name, text));
        mMutator = (t_object *) object_new_typed(CLASS_NOBOX, gensym("__fl.signal.mutator"), 0, NULL);
        
        // Free resources we no longer need
        
        sysmem_freeptr(av);
        freeobject((t_object *)d);
        sysmem_freeptr(text);
        
        // Get the object itself (typed)
        
        T *internal = (T *) mObject;
        
        long numIns = internal->getNumIns();
        long numOuts = internal->getNumOuts();
        long numAudioIns = internal->getNumAudioIns();
        long numAudioOuts = internal->getNumAudioOuts();
        
        internal->mUserObject = *this;
        
        // Create I/O
        
        mInOutlets.resize(numIns + numAudioIns - 1);
        mProxyIns.resize(numIns + numAudioIns - 1);
        mAudioOuts.resize(numAudioOuts - 1);
        mOuts.resize(numOuts);
        
        // Inlets for messages/signals
        
        mSyncIn = (t_object *) outlet_new(NULL, NULL);
        
        for (long i = numIns + numAudioIns - 2; i >= 0 ; i--)
        {
            mInOutlets[i] = (t_object *) outlet_new(NULL, NULL);
            mProxyIns[i] = (t_object *)  (i ? proxy_new(this, i, &mProxyNum) : NULL);
        }
        
        // Outlets for messages/signals
        
        for (long i = numOuts - 1; i >= 0 ; i--)
            mOuts[i] = (t_object *) outlet_new(this, NULL);
        for (long i = numAudioOuts - 2; i >= 0 ; i--)
            mAudioOuts[i] = (t_object *) outlet_new(this, "signal");
        
        // Object pointer types for internal object and mutator
        
        // Create Connections
        
        // Connect the audio sync in to the object and through to the mutator
        
        outlet_add(mSyncIn, inlet_nth(mObject, 0));
        outlet_add(outlet_nth(mObject, 0), inlet_nth(mMutator, 0));
        
        // Connect inlets (all types)
        
        for (long i = 0; i < numAudioIns + numIns - 1; i++)
            outlet_add(mInOutlets[i], inlet_nth(mObject, i + 1));
        
        // Connect outlets (audio then frame and sync message outlets)
        
        for (long i = 0; i < numAudioOuts - 1; i++)
            outlet_add(outlet_nth(mObject, i + 1), mAudioOuts[i]);
        
        for (long i = 0; i < numOuts; i++)
        {
            outlet_add(outlet_nth(mObject, i + numAudioOuts), mOuts[i]);
            outlet_add(outlet_nth(mMutator, 0), mOuts[i]);
        }
    }
    
    ~Wrapper()
    {
        // Delete ins and proxies
        
        for (std::vector <t_object *>::iterator it = mProxyIns.begin(); it != mProxyIns.end(); it++)
        {
            if (*it)
                freeobject(*it);
        }
        
        for (std::vector <t_object *>::iterator it = mInOutlets.begin(); it != mInOutlets.end(); it++)
            freeobject(*it);
        
        // Free objects - N.B. - free the patch, but not the object within it (which will be freed by deleting the patch)
        
        freeobject(mSyncIn);
        freeobject(mMutator);
        freeobject(mPatch);
    }
    
    void assist(void *b, long m, long a, char *s)
    {
        ((T *)mObject)->assist(b, m, a + 1, s);
    }
    
    void *subpatcher(long index, void *arg)
    {
        if ((t_ptr_uint) arg <= 1 || NOGOOD(arg))
            return NULL;
        
        return (index == 0) ? (void *) mPatch : NULL;
    }
    
    void sync(t_symbol *sym, long ac, t_atom *av)
    {
        if (mSyncChecker())
            outlet_anything(mSyncIn, gensym("signal"), ac, av);
    }
    
    void anything(t_symbol *sym, long ac, t_atom *av)
    {
        long inlet = getInlet();
        
        outlet_anything(mInOutlets[inlet], sym, ac, av);
    }
    
    // Initialise Class
    
    static void classInit(t_class *c, t_symbol *nameSpace, const char *classname)
    {
        addMethod<Wrapper<T>, &Wrapper<T>::sync>(c, "sync");
        addMethod<Wrapper<T>, &Wrapper<T>::anything>(c, "anything");
        addMethod<Wrapper<T>, &Wrapper<T>::subpatcher>(c, "subpatcher");
        addMethod<Wrapper<T>, &Wrapper<T>::assist>(c, "assist");
        
        // N.B. MUST add signal handling after dspInit to override the builtin responses
        
        dspInit(c);
        addMethod<Wrapper<T>, &Wrapper<T>::anything>(c, "signal");
        
        // Make sure the mutator class exists
        
        const char mutatorClassName[] = "__fl.signal.mutator";
                
        if (!class_findbyname(CLASS_NOBOX, gensym(mutatorClassName)))
            Mutator::makeClass<Mutator, &mutatorClass>(CLASS_NOBOX, mutatorClassName);
    }
    
private:
    
    // Objects (need freeing except the internal object which is owned by the patch)
    
    t_object *mPatch;
    t_object *mObject;
    t_object *mMutator;
    
    // Inlets (must be freed)
    
    std::vector <t_object *> mInOutlets;
    std::vector <t_object *> mProxyIns;
    t_object *mSyncIn;
    
    // Outlets (don't need to free - only the arrays need freeing)
    
    std::vector <t_object *> mAudioOuts;
    std::vector <t_object *> mOuts;
    
    // Sync Check
    
    SyncCheck mSyncChecker;
    
    // Dummy for stuffloc on proxies
    
    long mProxyNum;
};

//////////////////////////////////////////////////////////////////////////
////////////////// Max Object Class for Synchronisation //////////////////
//////////////////////////////////////////////////////////////////////////

template <class T> class FrameLib_MaxObj : public MaxBase
{
    // Connection Mode Enum
    
    enum ConnectMode { kConnect, kConfirm, kDoubleCheck };
    
    struct ConnectionInfo
    {
        ConnectionInfo(FrameLib_MaxObj *obj, unsigned long idx, t_object *topPatch, ConnectMode type) :
        object(obj), index(idx), topLevelPatch(topPatch), mode(type) {}
        
        FrameLib_MaxObj *object;
        unsigned long index;
        t_object *topLevelPatch;
        ConnectMode mode;
        
    };

    struct Input
    {
        t_object *proxy;
        
        FrameLib_MaxObj *object;
        unsigned long index;
        bool reportError;
    };

    t_symbol *ps_frame_connection_info() { return gensym("__frame__connection__info__"); }
    
private:
    
    // Global and Common Items
    
    FrameLib_Global *getGlobal()
    {
        return (FrameLib_Global *) gensym("__FrameLib__Global__")->s_thing;
    }

    void setGlobal(FrameLib_Global *global)
    {
       gensym("__FrameLib__Global__")->s_thing = (t_object *) global;
    }

    FrameLib_Common *getCommon()
    {
        char str[256];
        sprintf(str, "__FrameLib__Common__%llx", (unsigned long long) mTopLevelPatch);
        return (FrameLib_Common *) gensym(str)->s_thing;
    }

    void setCommon(FrameLib_Common *common)
    {
        char str[256];
        sprintf(str, "__FrameLib__Common__%llx", (unsigned long long) mTopLevelPatch);
        gensym(str)->s_thing = (t_object *) common;
    }

    FrameLib_MultiChannel::ConnectionQueue *getConnectionQueue()
    {
        return getCommon()->mConnectionQueue;
    }

    FrameLib_DSP::DSPQueue *getDSPQueue()
    {
        return getCommon()->mDSPQueue;
    }

    FrameLib_Global_Allocator *getGlobalAllocator()
    {
        return getGlobal()->mAllocator;
    }

    FrameLib_Local_Allocator *getLocalAllocator()
    {
        return getCommon()->mAllocator;
    }

    void commonInit()
    {
        FrameLib_Global *global;
        FrameLib_Common *common;
        
        mTopLevelPatch = jpatcher_get_toppatcher(gensym("#P")->s_thing);
        
        if (!(global = getGlobal()))
            setGlobal(new FrameLib_Global);
        else
            getGlobal()->increment();
        
        if (!(common = getCommon()))
            setCommon(new FrameLib_Common(getGlobalAllocator()));
        else
            common->increment();
    }

    void commonFree()
    {
        setCommon(getCommon()->decrement());
        setGlobal(getGlobal()->decrement());
    }

    // Parameter Parsing
    
    bool isAttributeTag(t_symbol *sym)
    {
        return (sym && sym->s_name[0] == '#' && strlen(sym->s_name) > 1);
    }

    bool isInputTag(t_symbol *sym)
    {
        return (sym && sym->s_name[0] == '/' && strlen(sym->s_name) > 1);
    }
                
    FrameLib_Attributes::Serial *parseAttributes(long argc, t_atom *argv)
    {
        t_symbol *sym;
        double array[4096];
        char argNames[64];
        long i, j;
       
        // Allocate
        
        FrameLib_Attributes::Serial *serialisedAttributes = new FrameLib_Attributes::Serial();
        
        // Parse arguments
        
        for (i = 0; i < argc; i++)
        {
            sprintf(argNames, "%ld", i);
            
            sym = atom_getsym(argv + i);
            
            if (isAttributeTag(sym) || isInputTag(sym))
                break;
            
    #ifndef OBJECT_ARGS_SET_ALL_INPUTS
            if (sym != gensym(""))
                serialisedAttributes->write(argNames, sym->s_name);
            else
            {
                array[0] = atom_getfloat(argv + i);
                serialisedAttributes->write(argNames, array, 1);
            }
    #endif
        }
        
        // Parse attributes
        
        while (i < argc)
        {
            // Strip stray items
            
            for (j = 0; i < argc; i++, j++)
            {
                if (isAttributeTag(atom_getsym(argv + i)) || isInputTag(atom_getsym(argv + i)))
                {
                    sym = atom_getsym(argv + i);
                    break;
                }
            
                if (j == 0)
                    object_error(mUserObject, "stray items after entry %s", sym->s_name);
            }
            
            // Check for lack of values or end of list
            
            if ((++i >= argc) || isAttributeTag(atom_getsym(argv + i)) || isInputTag(atom_getsym(argv + i)))
            {
                if (i < (argc + 1))
                    object_error(mUserObject, "no values given for entry %s", sym->s_name);
                continue;
            }
            
            // Do strings (if not an input)
            
            if (!isInputTag(sym) && atom_getsym(argv + i) != gensym(""))
            {
                serialisedAttributes->write(sym->s_name + 1, atom_getsym(argv + i++)->s_name);
                continue;
            }
            
            // Collect doubles
            
            for (j = 0; i < argc; i++, j++)
            {
                if (isAttributeTag(atom_getsym(argv + i)) || isInputTag(atom_getsym(argv + i)))
                    break;
                    
                if (atom_getsym(argv + i) != gensym(""))
                    object_error(mUserObject, "string %s in entry list where value expected", atom_getsym(argv + i)->s_name);
                
                array[j] = atom_getfloat(argv + i);
            }
            
            if (!isInputTag(sym))
                serialisedAttributes->write(sym->s_name + 1, array, j);
        }

        return serialisedAttributes;
    }

    // Input Parsing
    
    unsigned long inputNumber(t_symbol *sym)
    {
        return atoi(sym->s_name + 1) - 1;
    }

    void parseInputs(long argc, t_atom *argv)
    {
        t_symbol *sym;
        double array[4096];
        long i, j;
        
        // Parse arguments
        
        for (i = 0; i < argc; i++)
        {
            sym = atom_getsym(argv + i);
            
            if (isAttributeTag(sym) || isInputTag(sym))
                break;
            
    #ifdef OBJECT_ARGS_SET_ALL_INPUTS
            array[i] = atom_getfloat(argv + i);
    #endif
        }

    #ifdef OBJECT_ARGS_SET_ALL_INPUTS
        if (i)
        {
            for (j = 0; j < mObject->getNumIns(); j++)
                mObject->setFixedInput(j, array, i);
        }
    #endif

        // Parse attributes
        
        while (i < argc)
        {
            // Strip stray items
            
            for (j = 0; i < argc; i++, j++)
            {
                if (isAttributeTag(atom_getsym(argv + i)) || isInputTag(atom_getsym(argv + i)))
                {
                    sym = atom_getsym(argv + i);
                    break;
                }
            }
            
            // Check for lack of values or end of list
            
            if ((++i >= argc) || isAttributeTag(atom_getsym(argv + i)) || isInputTag(atom_getsym(argv + i)))
                continue;
            
            // Do strings (if not an input)
            
            if (!isInputTag(sym) && atom_getsym(argv + i) != gensym(""))
                continue;
            
            // Collect doubles
            
            for (j = 0; i < argc; i++, j++)
            {
                if (isAttributeTag(atom_getsym(argv + i)) || isInputTag(atom_getsym(argv + i)))
                    break;
        
                array[j] = atom_getfloat(argv + i);
            }
            
            if (isInputTag(sym))
                mObject->setFixedInput(inputNumber(sym), array, j);
        }
    }
    
    // IO Helpers

public:

    long getNumAudioIns()
    {
        return (long) mObject->getNumAudioIns() + (T::handlesAudio() ? 1 : 0);
    }

    long getNumAudioOuts()
    {
        return (long) mObject->getNumAudioOuts() + (T::handlesAudio() ? 1 : 0);
    }

    long getNumIns()
    {
        return (long) mObject->getNumIns();
    }

    long getNumOuts()
    {
        return (long) mObject->getNumOuts();
    }

    // Constructor and Destructor
    
public:

    FrameLib_MaxObj(t_symbol *s, long argc, t_atom *argv) : mConfirmIndex(-1), mConfirm(false)
    {
        // Init
        
        commonInit();
        mUserObject = (t_object *)this;

        // Object creation with attributes and arguments
        
        FrameLib_Attributes::Serial *serialisedAttributes = parseAttributes(argc, argv);

        mObject = new T(getConnectionQueue(), getDSPQueue(), serialisedAttributes, this);
        
        delete serialisedAttributes;
        
        parseInputs(argc, argv);
        
        mInputs.resize(getNumIns());
        mOutputs.resize(getNumOuts());
        
        for (long i = getNumIns() - 1; i >= 0; i--)
        {
            // N.B. - we create a proxy if the inlet is not the first inlet (not the first frame input or the object handles audio)
            
            mInputs[i].proxy = (t_object *) ((i || T::handlesAudio()) ? proxy_new(this, getNumAudioIns() + i, &mProxyNum) : NULL);
            mInputs[i].object = NULL;
            mInputs[i].index = 0;
            mInputs[i].reportError = TRUE;
        }
        
        for (unsigned long i = getNumOuts(); i > 0; i--)
            mOutputs[i - 1] = outlet_new(this, NULL);
        
        // Setup for audio, even if the object doesn't handle it, so that dsp recompile works correctly
        
        dspSetup(getNumAudioIns());
 
        for (unsigned long i = 0; i < getNumAudioOuts(); i++)
            outlet_new(this, "signal");
    }

    ~FrameLib_MaxObj()
    {
        dspFree();

        for (typename std::vector <Input>::iterator it = mInputs.begin(); it != mInputs.end(); it++)
            if (it->proxy)
                object_free(it->proxy);
        
        delete mObject;
        
        commonFree();
    }

    void assist (void *b, long m, long a, char *s)
    {
        if (m == ASSIST_OUTLET)
        {
            sprintf(s,"(signal) Output %ld", a + (T::handlesAudio() ? 0 : 1));
        }
        else
        {
            sprintf(s,"(signal) Input %ld", a + (T::handlesAudio() ? 0 : 1));
        }
    }

    // Perform and DSP

    void perform(t_object *dsp64, double **ins, long numins, double **outs, long numouts, long vec_size, long flags, void *userparam)
    {
        // N.B. Plus one due to sync inputs
        
        mObject->blockProcess(ins + 1, outs + 1, vec_size);
    }

    void dsp(t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
    {
        // Check / make connections
        
        connections();

        // Set sampling rate
        
        mObject->setSamplingRate(samplerate);
        
        // Add a perform routine to the chain if the object handles audio
        
        if (T::handlesAudio())
            addPerform<FrameLib_MaxObj, &FrameLib_MaxObj<T>::perform>(dsp64);
    }

    // Connection Routines
    
    static void connectionCall(FrameLib_MaxObj *x, unsigned long index, ConnectMode mode)
    {
        x->connect(index, mode);
    }

    void connectExternal(FrameLib_MaxObj *x, unsigned long index, ConnectMode mode)
    {
        object_method(x, gensym("internal_connect"), index, mode);
    }
    
    ConnectionInfo* getConnectionInfo()
    {
        return (ConnectionInfo *) ps_frame_connection_info()->s_thing;
    }
    
    void setConnectionInfo(ConnectionInfo *info)
    {
        ps_frame_connection_info()->s_thing = (t_object *) info;
    }
    
    void connect(unsigned long index, ConnectMode mode)
    {
        ConnectionInfo info(this, index, mTopLevelPatch, mode);
        
        setConnectionInfo(&info);
        outlet_anything(mOutputs[index], gensym("frame"), 0, NULL);
        setConnectionInfo(NULL);
    }

    void connections()
    {
        // Check input connections
        
        for (unsigned long i = 0; i < getNumIns(); i++)
        {
            mInputs[i].reportError = true;
            
            if (mInputs[i].object)
            {
                mConfirm = false;
                mConfirmIndex = i;
                
                if (mObject->isConnected(i))
                    connectExternal(mInputs[i].object, mInputs[i].index, kConfirm);
                
                if (!mConfirm)
                {
                    // Object is no longer connected as before
                    
                    mInputs[i].object = NULL;
                    mInputs[i].index = 0;
                    
                    if (mObject->isConnected(i))
                        mObject->deleteConnection(i);
                }
                
                mConfirm = false;
                mConfirmIndex = -1;
            }
        }
        
        // Make output connections
        
        for (unsigned long i = getNumOuts(); i > 0; i--)
            connect(i - 1, kConnect);
        
        // Reset DSP
        
        mObject->reset();
    }

    void frame()
    {
        ConnectionInfo *info = getConnectionInfo();
        
        long index = getInlet() - getNumAudioIns();
        
        if (!info || index < 0)
            return;
        
        switch (info->mode)
        {
            case kConnect:
            {
                bool connectionChange = (mInputs[index].object != info->object || mInputs[index].index != info->index);
                bool valid = (info->topLevelPatch == mTopLevelPatch && info->object != this);
                
                // Confirm that the object is valid
                
                if (!valid)
                {
                    if (info->object == this)
                        object_error(mUserObject, "direct feedback loop detected");
                    else
                        object_error(mUserObject, "cannot connect objects from different top level patchers");
                }
                
                // Check for double connection *only* if the internal object is connected (otherwise the previously connected object has been deleted)
                
                if (mInputs[index].object && mInputs[index].reportError && connectionChange && mObject->isConnected(index))
                {
                    mConfirmIndex = index;
                    connectExternal(mInputs[index].object, mInputs[index].index, kDoubleCheck);
                    mConfirmIndex = -1;
                    mInputs[index].reportError = false;
                }
                
                // Always change the connection if the new object is valid (only way to ensure new connections work)
                
                if (connectionChange && valid)
                {
                    mInputs[index].object = info->object;
                    mInputs[index].index = info->index;
                    
                    mObject->addConnection(info->object->mObject, info->index, index);
                }
            }
                break;
                
            case kConfirm:
                if (index == mConfirmIndex && mInputs[index].object == info->object && mInputs[index].index == info->index)
                    mConfirm = true;
                break;
                
            case kDoubleCheck:
                if (index == mConfirmIndex && mInputs[index].object == info->object && mInputs[index].index == info->index)
                    object_error(mUserObject, "extra connection to input %ld", index + 1);
                break;
        }
    }

    void sync()
    {
        if (mSyncChecker())
            for (unsigned long i = getNumOuts(); i > 0; i--)
                outlet_anything(mOutputs[i - 1], gensym("sync"), 0, NULL);
    }
    
    // Class Initilisation
    
    template <class U> static void makeClass(t_symbol *nameSpace, const char *className)
    {
        // If handles audio/scheduler then make wrapper class and name the inner object differently..
        
        char internalClassName[256];
        
        if (T::handlesAudio())
        {
            Wrapper<U>:: template makeClass<Wrapper<U>, &wrapperClass>(CLASS_BOX, className);
            sprintf(internalClassName, "unsynced.%s", className);
        }
        else
            strcpy(internalClassName, className);
        
        MaxBase::makeClass<U, &objectClass>(nameSpace, internalClassName);
    }
    
    static void classInit(t_class *c, t_symbol *nameSpace, const char *classname)
    {
        addMethod(c, (method) &connectionCall, "internal_connect");
        addMethod<FrameLib_MaxObj<T>, &FrameLib_MaxObj<T>::assist>(c, "assist");
        addMethod<FrameLib_MaxObj<T>, &FrameLib_MaxObj<T>::frame>(c, "frame");
        addMethod<FrameLib_MaxObj<T>, &FrameLib_MaxObj<T>::sync>(c, "sync");
        addMethod<FrameLib_MaxObj<T>, &FrameLib_MaxObj<T>::dsp>(c);
        
        dspInit(c);
    }

private:
    
    FrameLib_MultiChannel *mObject;
    
    std::vector <Input> mInputs;
    std::vector <void *> mOutputs;

    long mProxyNum;
    long mConfirmIndex;
    bool mConfirm;
    
    t_object *mTopLevelPatch;
    
     SyncCheck mSyncChecker;
    
public:
    
    t_object *mUserObject;
};

#ifndef CUSTOM_OBJECT
extern "C" int C74_EXPORT main(void)
{
    FrameLib_MaxObj<OBJECT_CLASS>::makeClass<FrameLib_MaxObj<OBJECT_CLASS> >(CLASS_BOX, OBJECT_NAME);
}
#endif
