RESOURCES_LIBRARY()

IF(NOT HOST_OS_LINUX AND NOT HOST_OS_WINDOWS AND NOT HOST_OS_DARWIN)
    MESSAGE(FATAL_ERROR Unsupported platform for YFM tool)
ENDIF()

DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE(
    YFM_TOOL
    sbr:6406698918 FOR DARWIN-ARM64
    sbr:6406698918 FOR DARWIN
    sbr:6406693802 FOR LINUX
    sbr:6406704048 FOR WIN32
)

END()
