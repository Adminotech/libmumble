set(SOURCE_DIR ../celt/libcelt)

add_library(libcelt
    ${SOURCE_DIR}/bands.c
    ${SOURCE_DIR}/celt.c
    ${SOURCE_DIR}/cwrs.c
    ${SOURCE_DIR}/entcode.c
    ${SOURCE_DIR}/entdec.c
    ${SOURCE_DIR}/entenc.c
    ${SOURCE_DIR}/header.c
    ${SOURCE_DIR}/kiss_fft.c
    ${SOURCE_DIR}/laplace.c
    ${SOURCE_DIR}/mathops.c
    ${SOURCE_DIR}/mdct.c
    ${SOURCE_DIR}/modes.c
    ${SOURCE_DIR}/pitch.c
    ${SOURCE_DIR}/plc.c 
    ${SOURCE_DIR}/quant_bands.c
    ${SOURCE_DIR}/rate.c
    ${SOURCE_DIR}/vq.c
)

if (WIN32)
	include_directories(win32)
else()
	include_directories(.)
endif()

set_target_properties(libcelt PROPERTIES COMPILE_DEFINITIONS HAVE_CONFIG_H)

if(WIN32)
    set_target_properties (libcelt PROPERTIES DEBUG_POSTFIX d)
    set_target_properties (libcelt PROPERTIES PREFIX "")
endif()
