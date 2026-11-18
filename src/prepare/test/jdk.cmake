# Selects javac, java and jar only from JAVA_HOME, which CTest sets from the VOYEMU_JDK cache entry.
if(NOT IS_ABSOLUTE "$ENV{JAVA_HOME}" OR NOT IS_DIRECTORY "$ENV{JAVA_HOME}/bin")
    message(FATAL_ERROR "Absolute JAVA_HOME of the pinned JDK required; run through CTest or set JAVA_HOME")
endif()
find_program(JAVAC javac PATHS "$ENV{JAVA_HOME}/bin" NO_DEFAULT_PATH REQUIRED)
find_program(JAVA java PATHS "$ENV{JAVA_HOME}/bin" NO_DEFAULT_PATH REQUIRED)
find_program(JAR jar PATHS "$ENV{JAVA_HOME}/bin" NO_DEFAULT_PATH REQUIRED)
