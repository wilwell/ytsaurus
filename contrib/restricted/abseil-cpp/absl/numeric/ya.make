# Generated by devtools/yamaker.

LIBRARY()

LICENSE(Apache-2.0)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

VERSION(20240722.0)

ADDINCL(
    GLOBAL contrib/restricted/abseil-cpp
)

NO_COMPILER_WARNINGS()

NO_UTIL()

SRCS(
    int128.cc
)

END()
