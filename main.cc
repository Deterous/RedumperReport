#include <exception>
#include <filesystem>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

import redumper_report;
import utils.logger;



using namespace gpsxre;



int main(int argc, char *argv[])
{
    int exit_code = 0;
    std::filesystem::path file;
    bool help = false;
    bool verbose = false;

#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif

    try
    {
        for(int i = 1; i < argc; ++i)
        {
            std::string arg(argv[i]);

            if(arg == "--help" || arg == "-h")
                help = true;
            else if(arg == "--verbose" || arg == "-v")
                verbose = true;
            else if(file.empty())
            {
                file = std::filesystem::path(arg);
            }
        }

        if(help || file.empty())
        {
            LOG("usage: redumper_report [options] <filename.log>");
            LOG("");
            LOG("\t--verbose,-v    \tverbose mode");
            LOG("");
            LOG("Note: Log files must have the same base filename.");
            exit_code = -1;
        }
        if(!file.empty())
            exit_code = redumper_report(file, verbose);
    }
    catch(const std::exception &e)
    {
        LOG("error: {}", e.what());
        exit_code = -1;
    }
    catch(...)
    {
        LOG("error: unhandled exception");
        exit_code = -2;
    }

    return exit_code;
}

