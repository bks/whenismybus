project(whensmybus)

# Find the required Libaries
find_package(KDE4 REQUIRED)
include(KDE4Defaults)

add_definitions (${QT_DEFINITIONS} ${KDE4_DEFINITIONS})
include_directories(
   ${CMAKE_SOURCE_DIR}
   ${CMAKE_BINARY_DIR}
   ${KDE4_INCLUDES}
   )

set(rtddenver_engine_SRCS rtddenverengine.cpp)
set(rtddenver_engine_RCCS rtddenverengine.qrc)

set(rtdschedule_applet_SRCS rtdscheduleapplet.cpp)

# Now make sure all files get to the right place
qt4_add_resources(rtddenver_engine_RCC_SRCS ${rtddenver_engine_RCCS})
kde4_add_plugin(plasma_engine_rtddenver ${rtddenver_engine_SRCS} ${rtddenver_engine_RCC_SRCS})
target_link_libraries(plasma_engine_rtddenver
                      ${KDE4_KDECORE_LIBS}
                      ${KDE4_KIO_LIBS}
                      ${KDE4_PLASMA_LIBS}
                      ${QT_QTWEBKIT_LIBRARY})

kde4_add_plugin(plasma_applet_rtdschedule ${rtdschedule_applet_SRCS})
target_link_libraries(plasma_applet_rtdschedule
                      ${KDE4_KDECORE_LIBS}
                      ${KDE4_PLASMA_LIBS})

install(TARGETS plasma_engine_rtddenver plasma_applet_rtdschedule
        DESTINATION ${PLUGIN_INSTALL_DIR})
 
install(FILES plasma-engine-rtddenver.desktop plasma-applet-rtdschedule.desktop
        DESTINATION ${SERVICES_INSTALL_DIR})