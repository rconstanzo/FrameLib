
#include "FrameLib_Pad.h"
#include "FrameLib_MaxClass.h"

extern "C" int C74_EXPORT main(void)
{
    FrameLib_MaxClass<FrameLib_Expand<FrameLib_Pad> >::makeClass(CLASS_BOX, "fl.pad~");
}
