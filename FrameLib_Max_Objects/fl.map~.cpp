
#include "FrameLib_Map.h"
#include "FrameLib_MaxClass.h"

extern "C" int C74_EXPORT main(void)
{
    FrameLib_MaxClass<FrameLib_Expand<FrameLib_Map> >::makeClass(CLASS_BOX, "fl.map~");
}
