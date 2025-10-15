#include "Base.h"
#include "Log.h"


namespace Debug 
{

bool ReportAssert(const char *expr, const char *file, const char *func, size_t line)
{
    LOG_FATAL("({}:{}) Assertion `{}` has failed!", file, line, expr);
    return true;
}

}
