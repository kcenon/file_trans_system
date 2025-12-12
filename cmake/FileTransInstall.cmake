##################################################
# File Transfer System Installation Configuration
#
# This module handles installation and package
# configuration for file_trans_system.
##################################################

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

function(setup_file_trans_installation)
    ##################################################
    # Install targets
    ##################################################

    install(TARGETS FileTransSystem
        EXPORT FileTransSystemTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    ##################################################
    # Install headers
    ##################################################

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
    )

    ##################################################
    # Export targets
    ##################################################

    install(EXPORT FileTransSystemTargets
        FILE FileTransSystemTargets.cmake
        NAMESPACE kcenon::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FileTransSystem
    )

    ##################################################
    # Package configuration
    ##################################################

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FileTransSystemConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/FileTransSystemConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FileTransSystem
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/FileTransSystemConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/FileTransSystemConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/FileTransSystemConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FileTransSystem
    )

endfunction()
