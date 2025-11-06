#include "Base/Log.h"
#include "Demo/Application.h"


int main()
{
    Log::Init();

    Application app;
    app.Run();
}