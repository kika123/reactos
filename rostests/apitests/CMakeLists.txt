include_directories(include)

add_subdirectory(advapi32)
add_subdirectory(atl)
add_subdirectory(browseui)
add_subdirectory(com)
add_subdirectory(crt)
add_subdirectory(dciman32)
add_subdirectory(gdi32)
add_subdirectory(gditools)
add_subdirectory(iphlpapi)
if(NOT ARCH STREQUAL "amd64")
    add_subdirectory(kernel32)
endif()
add_subdirectory(msvcrt)
add_subdirectory(ntdll)
add_subdirectory(ole32)
add_subdirectory(pefile)
add_subdirectory(powrprof)
add_subdirectory(setupapi)
add_subdirectory(shell32)
add_subdirectory(psapi)
add_subdirectory(user32)
add_subdirectory(user32_dynamic)
if(NOT ARCH STREQUAL "amd64")
    add_subdirectory(win32kdll)
    add_subdirectory(win32nt)
endif()
add_subdirectory(winhttp)
add_subdirectory(wininet)
add_subdirectory(ws2_32)
