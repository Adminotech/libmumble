#ifndef _LIBMUMBLECLIENT_VISIBILITY_H_
#define _LIBMUMBLECLIENT_VISIBILITY_H_

#if _WIN32
    // Windows dynamic lib export/import
    #ifdef LIBMUMBLECLIENT_DYNAMIC
        #ifdef LIBMUMBLECLIENT_EXPORT_API
            #define DLL_PUBLIC __declspec(dllexport)
        #else
            #define DLL_PUBLIC __declspec(dllimport)
        #endif
        #define	DLL_LOCAL
    // Windows static lib
    #else
        #define DLL_PUBLIC
        #define	DLL_LOCAL
    #endif
#else
    // Linux, use visibility __attribute__ if there for both static and dynamic
    #if __GNUC__ >= 4
        #define DLL_PUBLIC __attribute__ ((visibility("default")))
        #define DLL_LOCAL  __attribute__ ((visibility("hidden")))
    #else
        #define DLL_PUBLIC
        #define DLL_LOCAL
    #endif
#endif

// All platforms static build, no special defines
#ifdef LIBMUMBLECLIENT_STATIC
    #define DLL_PUBLIC
    #define	DLL_LOCAL
#endif

#endif
