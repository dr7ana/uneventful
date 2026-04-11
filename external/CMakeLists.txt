include(ExternalProject)

find_package(Git)
if(GIT_FOUND)
    message(STATUS "Checking submodules")
    include(check_submodule)
    check_submodule(unlog)
    if(UNEVENTFUL_USE_BUNDLED_LIBEVENT)
        check_submodule(libevent)
    endif()
endif()

# unlog
add_subdirectory(unlog)

# libevent
if(UNEVENTFUL_USE_BUNDLED_LIBEVENT)
    set(EVENT__LIBRARY_TYPE "STATIC" CACHE STRING "" FORCE)
    set(EVENT__DISABLE_TESTS ON CACHE BOOL "" FORCE)
    set(EVENT__DISABLE_MBEDTLS ON CACHE BOOL "" FORCE)
    set(EVENT__DISABLE_BENCHMARK ON CACHE BOOL "" FORCE)
    set(EVENT__DISABLE_SAMPLES ON CACHE BOOL "" FORCE)
    add_subdirectory(libevent EXCLUDE_FROM_ALL)

    add_library(libevent::ssl ALIAS event_openssl)
    add_library(libevent::core ALIAS event_core)
    add_library(libevent::threads ALIAS event_pthreads)
    add_library(libevent::extra ALIAS event_extra)
else()
    set(LIBEVENT_STATIC_LINK TRUE)
    find_package(Libevent 2.1.12 CONFIG REQUIRED COMPONENTS core extra pthreads openssl)

    set(UNEVENTFUL_LIBEVENT_CORE_TARGET "")
    set(UNEVENTFUL_LIBEVENT_EXTRA_TARGET "")
    set(UNEVENTFUL_LIBEVENT_PTHREADS_TARGET "")
    set(UNEVENTFUL_LIBEVENT_OPENSSL_TARGET "")

    if(TARGET libevent::core)
        set(UNEVENTFUL_LIBEVENT_CORE_TARGET libevent::core)
    elseif(TARGET Libevent::core)
        set(UNEVENTFUL_LIBEVENT_CORE_TARGET Libevent::core)
    endif()

    if(TARGET libevent::extra)
        set(UNEVENTFUL_LIBEVENT_EXTRA_TARGET libevent::extra)
    elseif(TARGET Libevent::extra)
        set(UNEVENTFUL_LIBEVENT_EXTRA_TARGET Libevent::extra)
    endif()

    if(TARGET libevent::pthreads)
        set(UNEVENTFUL_LIBEVENT_PTHREADS_TARGET libevent::pthreads)
    elseif(TARGET Libevent::pthreads)
        set(UNEVENTFUL_LIBEVENT_PTHREADS_TARGET Libevent::pthreads)
    endif()

    if(TARGET libevent::openssl)
        set(UNEVENTFUL_LIBEVENT_OPENSSL_TARGET libevent::openssl)
    elseif(TARGET Libevent::openssl)
        set(UNEVENTFUL_LIBEVENT_OPENSSL_TARGET Libevent::openssl)
    endif()

    if(NOT UNEVENTFUL_LIBEVENT_CORE_TARGET OR NOT UNEVENTFUL_LIBEVENT_EXTRA_TARGET OR
       NOT UNEVENTFUL_LIBEVENT_PTHREADS_TARGET)
        message(FATAL_ERROR "System Libevent package did not provide the expected imported targets")
    endif()

    if(NOT TARGET libevent::core)
        add_library(libevent::core INTERFACE IMPORTED GLOBAL)
        target_link_libraries(libevent::core INTERFACE ${UNEVENTFUL_LIBEVENT_CORE_TARGET})
    endif()

    if(NOT TARGET libevent::extra)
        add_library(libevent::extra INTERFACE IMPORTED GLOBAL)
        target_link_libraries(libevent::extra INTERFACE ${UNEVENTFUL_LIBEVENT_EXTRA_TARGET})
    endif()

    if(NOT TARGET libevent::threads)
        add_library(libevent::threads INTERFACE IMPORTED GLOBAL)
        target_link_libraries(libevent::threads INTERFACE ${UNEVENTFUL_LIBEVENT_PTHREADS_TARGET})
    endif()

    if(UNEVENTFUL_LIBEVENT_OPENSSL_TARGET AND NOT TARGET libevent::ssl)
        add_library(libevent::ssl INTERFACE IMPORTED GLOBAL)
        target_link_libraries(libevent::ssl INTERFACE ${UNEVENTFUL_LIBEVENT_OPENSSL_TARGET})
    endif()
endif()
