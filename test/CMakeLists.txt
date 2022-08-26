project (uiod-test)

file(GLOB HEADER_FILES
    "*.h"
	)

file(GLOB SOURCE_FILES
    "*.cpp"
	)

add_executable (${PROJECT_NAME} ${SOURCE_FILES} ${HEADER_FILES})