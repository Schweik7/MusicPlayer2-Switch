#include "SocketGuard.h"

#include <switch.h>

namespace
{
    int g_ref_count = 0;
}

namespace SocketGuard
{

bool Acquire()
{
    if (g_ref_count > 0)
    {
        ++g_ref_count;
        return true;
    }
    if (R_FAILED(socketInitializeDefault()))
        return false;
    g_ref_count = 1;
    return true;
}

void Release()
{
    if (g_ref_count <= 0)
        return;
    --g_ref_count;
    if (g_ref_count == 0)
        socketExit();
}

bool IsInited()
{
    return g_ref_count > 0;
}

}   // namespace SocketGuard
