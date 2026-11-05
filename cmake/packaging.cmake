set(VALHALLA_UPSTREAM_ASSETS "" CACHE FILEPATH "Verified v0.2.0-NG release archive for complete mod packaging")
if(VALHALLA_UPSTREAM_ASSETS)
 find_package(Python3 REQUIRED COMPONENTS Interpreter)
 add_custom_target(package_mod
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/package.py"
   --assets "${VALHALLA_UPSTREAM_ASSETS}" --dll "$<TARGET_FILE:${PROJECT_NAME}>"
   --output "${CMAKE_BINARY_DIR}/packaging/ValhallaCombat-${PROJECT_VERSION}-MenuFramework.zip"
  DEPENDS "${PROJECT_NAME}" VERBATIM)
endif()
