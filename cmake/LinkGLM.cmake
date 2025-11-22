include(FetchContent)

macro(LinkGLM TARGET ACCESS)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY	https://github.com/g-truc/glm.git
        GIT_TAG 	    a532f5b1cf27d6a3c099437e6959cf7e398a0a67    # version 1.0.2
    )

    FetchContent_MakeAvailable(glm)

    set_target_properties(glm PROPERTIES FOLDER 3rdparty)
    target_link_libraries(${TARGET} ${ACCESS} glm::glm)
endmacro()