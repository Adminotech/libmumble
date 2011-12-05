#include "client_lib.h"

#include <iostream>

#include "client.h"
#include "logging.h"
#include "settings.h"

namespace MumbleClient 
{
    MumbleClientLib* MumbleClientLib::instance_ = 0;

    MumbleClientLib::MumbleClientLib() 
    {
    }

    MumbleClientLib::~MumbleClientLib() 
    {
        delete instance_;
    }

    MumbleClientLib* MumbleClientLib::instance() 
    {
        if (!instance_)
            instance_ = new MumbleClientLib();
        return instance_;
    }

    MumbleClient* MumbleClientLib::NewClient() 
    {
        return new MumbleClient(&io_service_);
    }

    void MumbleClientLib::Run() 
    {
        io_service_.reset();
        io_service_.run();
    }

    void MumbleClientLib::Shutdown() 
    {
        ::google::protobuf::ShutdownProtobufLibrary();
    }

    int32_t MumbleClientLib::GetLogLevel() 
    {
        return logging::GetLogLevel();
    }

    void MumbleClientLib::SetLogLevel(int32_t level) 
    {
        logging::SetLogLevel(level);
    }

}
