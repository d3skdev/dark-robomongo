# Note for maintainers
# --------------------
#
# Do not use absolute paths in DESTINATION arguments for install() command.
# Because the same install code will be executed again by CPack. And CPack will
# change internally CMAKE_INSTALL_PREFIX to point to some temporary folder
# for package content.
#
#


# Temporary change
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install"
    CACHE STRING "Install path prefix, prepended onto install directories"
    FORCE)

if(SYSTEM_LINUX)
    set(bin_dir             bin)
    set(lib_dir             lib)
    set(resources_dir       share)
    set(license_dir         .)

    set(qt_plugins_dir      ${lib_dir})
    set(qt_conf_dir         ${bin_dir})
    set(qt_conf_plugins     "../lib")
elseif(SYSTEM_MACOSX)
    set(bundle_name         "Dark Robo 3T.app")
    set(contents_path       ${bundle_name}/Contents)

    set(bin_dir             ${contents_path}/MacOS)
    set(styles_dir          ${contents_path}/MacOS/styles)
    set(lib_dir             ${contents_path}/Frameworks)
    set(resources_dir       ${contents_path}/Resources)
    set(license_dir         ${resources_dir})

    set(qt_plugins_dir      ${contents_path}/PlugIns/Qt)
    set(qt_conf_dir         ${resources_dir})
    set(qt_conf_plugins     "PlugIns/Qt")
elseif(SYSTEM_WINDOWS)
    set(bin_dir             .)
    set(styles_dir          ${bin_dir}/styles)
    set(lib_dir             .)
    set(resources_dir       ./resources)
    set(license_dir         .)

    set(qt_plugins_dir      ${lib_dir})
    set(qt_conf_dir         ${bin_dir})
    set(qt_conf_plugins     .)
endif()

# Generate qt.conf file
configure_file(
    "${CMAKE_SOURCE_DIR}/install/qt.conf.in"
    "${CMAKE_BINARY_DIR}/qt.conf")

# Install qt.conf file
install(
    FILES       "${CMAKE_BINARY_DIR}/qt.conf"
    DESTINATION "${qt_conf_dir}")

# Install OpenSSL dynamic lib files
if(SYSTEM_WINDOWS)
    install(
        FILES 
        "${OpenSSL_DIR}/libssl-1_1-x64.dll"
        "${OpenSSL_DIR}/libcrypto-1_1-x64.dll"
        DESTINATION ${bin_dir})
elseif(SYSTEM_MACOSX)
    install(
        FILES 
        "${OpenSSL_DIR}/libssl.1.1.dylib"
        "${OpenSSL_DIR}/libcrypto.1.1.dylib"
        DESTINATION ${lib_dir}/lib)
elseif(SYSTEM_LINUX)
    install(
        FILES 
        "${OpenSSL_DIR}/libssl.so"
        "${OpenSSL_DIR}/libssl.so.1.0.0"        
        "${OpenSSL_DIR}/libcrypto.so"        
        "${OpenSSL_DIR}/libcrypto.so.1.0.0"
        DESTINATION ${lib_dir})         
endif()

# Install binary
install(
    TARGETS robomongo
    RUNTIME DESTINATION ${bin_dir}
    BUNDLE DESTINATION .)

# Install license, copyright and changelogs files
install(
    FILES
        ${CMAKE_SOURCE_DIR}/LICENSE
        ${CMAKE_SOURCE_DIR}/COPYRIGHT
        ${CMAKE_SOURCE_DIR}/CHANGELOG
        ${CMAKE_SOURCE_DIR}/DESCRIPTION
    DESTINATION ${license_dir})

# Install common dependencies
SET(QT_LIBS Core Gui Widgets PrintSupport Network Xml)
if(NOT SYSTEM_LINUX)
    SET(QT_LIBS ${QT_LIBS} WebEngineWidgets WebEngineCore Quick 
                           QuickWidgets WebChannel Qml Positioning)
endif()
install_qt_lib(${QT_LIBS})
install_qt_plugins(QGifPlugin QICOPlugin)
install_icu_libs()
set(QT_STYLES_DIR ${Qt5Core_DIR}/../../../plugins/styles/)
set(QT_BIN_DIR ${Qt5Core_DIR}/../../../bin/)
set(QT_RESOURCES_DIR ${Qt5Core_DIR}/../../../resources/)

if(SYSTEM_LINUX)
    install_qt_lib(XcbQpa DBus)
    install_qt_plugins(
        QXcbIntegrationPlugin)
        
    install(
        FILES
            "/usr/lib/x86_64-linux-gnu/libstdc++.so.6"
            "/usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.28"              
        DESTINATION ${lib_dir})
elseif(SYSTEM_MACOSX)
    install_qt_lib(MacExtras DBus)
    install_qt_plugins(
        QCocoaIntegrationPlugin
        QMinimalIntegrationPlugin
        QOffscreenIntegrationPlugin)

    # Install icon
    install(
        FILES       "${CMAKE_SOURCE_DIR}/install/macosx/robomongo.icns"
        DESTINATION "${resources_dir}")

    # Install styles    
    install(FILES "${QT_STYLES_DIR}/libqmacstyle.dylib" DESTINATION ${styles_dir})
elseif(SYSTEM_WINDOWS)
    install_qt_plugins(
        QWindowsIntegrationPlugin
        QMinimalIntegrationPlugin
        QOffscreenIntegrationPlugin)

    # Qt WebEngine dependencies
    install(DIRECTORY ${QT_RESOURCES_DIR} DESTINATION ${resources_dir})
    install(FILES 
                "${QT_BIN_DIR}/libEGL.dll"
                "${QT_BIN_DIR}/libGLESv2.dll" 
                "${QT_BIN_DIR}/opengl32sw.dll" 
                "${QT_BIN_DIR}/QtWebEngineProcess.exe" 
            DESTINATION ${bin_dir})

    # Install Styles
    install(FILES "${QT_STYLES_DIR}/qwindowsvistastyle.dll" DESTINATION ${styles_dir})

    # Install runtime libraries:
    # msvcp120.dll
    # msvcr120.dll
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    include(InstallRequiredSystemLibraries)
endif()
