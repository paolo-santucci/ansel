#
# Install Doc files
#
set(DOC_FILES ${ANSEL_AUTHORS_FILE} ../LICENSE)
if(USE_RAWDINAL)
  list(APPEND DOC_FILES ../src/external/rawdinal/THIRD_PARTY_NOTICES)
endif()
install(FILES ${DOC_FILES} DESTINATION ${CMAKE_INSTALL_DOCDIR} COMPONENT DTDocuments)

# Build manual page
add_subdirectory(man)
