#include "ghostwave/core.hpp"
#include <windows.h>
#include <shellapi.h>
#include <wx/init.h>
#include <iostream>
int wmain(int argc,wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    wxInitializer wx;
    if(!wx) { std::cerr<<"Initialization failed\n"; return 1; }
    std::vector<std::string> args; for(int i=1;i<argc;++i) args.push_back(gw::utf8(argv[i]));
    return gw::command_line(args,[] {
        auto executable=gw::app_dir()/"GhostWaveStudio.exe";
        auto result=ShellExecuteW(nullptr,L"open",executable.c_str(),L"--gui",gw::app_dir().c_str(),SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result)>32?0:1;
    });
}
