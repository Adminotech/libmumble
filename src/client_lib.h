#ifndef _LIBMUMBLECLIENT_CLIENT_LIB_H_
#define _LIBMUMBLECLIENT_CLIENT_LIB_H_

#include "visibility.h"
#include "libmumble_stdint.h"

#include <boost/asio.hpp>

namespace MumbleClient 
{
    class MumbleClient;

    class DLL_PUBLIC MumbleClientLib 
    {
    public:
        static MumbleClientLib* instance();

        MumbleClient* NewClient();
        
        void Run();
        void Shutdown();

        static int32_t GetLogLevel();
        static void SetLogLevel(int32_t level);

    private:
        DLL_LOCAL MumbleClientLib();
        DLL_LOCAL ~MumbleClientLib();

        DLL_LOCAL static MumbleClientLib* instance_;
        boost::asio::io_service io_service_;

        MumbleClientLib(const MumbleClientLib&);
        void operator=(const MumbleClientLib&);
    };
}

#endif
