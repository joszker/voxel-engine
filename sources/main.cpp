#include "Base/Log.h"
#include "Application.h"


int main()
{
    Log::Init();

    Application app;
    app.Run();
}