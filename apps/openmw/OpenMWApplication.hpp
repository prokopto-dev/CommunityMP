#ifndef OPENMW_APPLICATION_HPP
#define OPENMW_APPLICATION_HPP

#include <string_view>

namespace Files
{
    struct ConfigurationManager;
}

namespace OMW
{
    class Engine;
}

bool configureOpenMwApplication(int argc, char* argv[], OMW::Engine& engine, Files::ConfigurationManager& cfgMgr,
    std::string_view logName = "OpenMW");

int runApplication(int argc, char* argv[]);

#endif
