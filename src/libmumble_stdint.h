#ifndef _LIBMUMBLE_STDINT_H
#define _LIBMUMBLE_STDINT_H

// Use system int if available, if not use boost
#if (__STDC_VERSION__ >= 199901L) || (_MSC_VER >= 1600)

#include <cstdint>

#else

#include <boost/cstdint.hpp>

using boost::int8_t;
using boost::int16_t;
using boost::int32_t;
using boost::int64_t;

using boost::uint8_t;
using boost::uint16_t;
using boost::uint32_t;
using boost::uint64_t;

#endif

#endif