
#include "FrameLib_Objects.h"
#include "FrameLib_MaxClass.h"

extern "C" int C74_EXPORT main(void)
{
    FrameLib_MaxClass<FrameLib_Expand<FrameLib_NotEqual>, true>::makeClass(CLASS_BOX, "fl.notequals~");
}
