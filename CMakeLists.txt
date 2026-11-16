cmake_minimum_required(VERSION 3.22)
project(VH4Sim VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.12 GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(VH4Sim PRODUCT_NAME "VH4 Sim" COMPANY_NAME "Personale" VERSION ${PROJECT_VERSION})
target_sources(VH4Sim PRIVATE app/Main.cpp app/MainComponent.cpp app/MainComponent.h app/AmpProcessor.h dsp/vh4dsp.hpp)
target_compile_definitions(VH4Sim PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="VH4 Sim" JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(VH4Sim PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
if(MSVC)
    target_compile_options(VH4Sim PRIVATE /fp:fast /O2)
endif()

# ASIO (solo Windows x64, facoltativo): -DVH4_ASIO_SDK=C:/percorso/ASIOSDK
if(VH4_ASIO_SDK)
    target_compile_definitions(VH4Sim PRIVATE JUCE_ASIO=1)
    target_include_directories(VH4Sim PRIVATE ${VH4_ASIO_SDK}/common)
endif()

# strumento da riga di comando per provare il suono e misurare la velocita' (non fa parte dell'app)
add_executable(vh4_offline EXCLUDE_FROM_ALL tools/offline.cpp)
