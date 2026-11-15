cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(TelegramBotApi VERSION 10.4 LANGUAGES CXX)

add_subdirectory(td EXCLUDE_FROM_ALL)

if (NOT DEFINED CMAKE_MODULE_PATH)
  set(CMAKE_MODULE_PATH "")
endif()
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/td/CMake" "${CMAKE_MODULE_PATH}")

if (NOT DEFINED CMAKE_INSTALL_BINDIR)
  set(CMAKE_INSTALL_BINDIR "bin")
endif()

if (POLICY CMP0074)
  # use environment variables to find libraries
  cmake_policy(SET CMP0074 NEW)
endif()

include(PreventInSourceBuild)
prevent_in_source_build()

if (TELEGRAM_BOT_API_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT IPO_SUPPORTED LANGUAGES CXX)
  if (IPO_SUPPORTED)
    string(REPLACE ";" " " CXX_FLAGS_IPO "${CMAKE_CXX_COMPILE_OPTIONS_IPO}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_FLAGS_IPO}")

    string(REPLACE ";" " " LINK_FLAGS_IPO "${CMAKE_CXX_LINK_OPTIONS_IPO}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${LINK_FLAGS_IPO}")
  endif()
endif()

set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

if (THREADS_HAVE_PTHREAD_ARG)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif()

include(TdSetUpCompiler)
td_set_up_compiler()

if (CLANG OR GCC)
  if (MEMPROF)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-no-pie CXX_NO_PIE_FLAG)
    if (CXX_NO_PIE_FLAG)
      set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -no-pie")
    elseif (APPLE)
      set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-no_pie")
    endif()
    include(AddCXXCompilerFlag)
    add_cxx_compiler_flag("-static-libstdc++")
    add_cxx_compiler_flag("-static-libgcc")
  endif()
endif()

set(TELEGRAM_BOT_API_SOURCE
  telegram-bot-api/telegram-bot-api.cpp

  telegram-bot-api/Client.cpp
  telegram-bot-api/ClientManager.cpp
  telegram-bot-api/HttpConnection.cpp
  telegram-bot-api/HttpStatConnection.cpp
  telegram-bot-api/Query.cpp
  telegram-bot-api/Stats.cpp
  telegram-bot-api/Watchdog.cpp
  telegram-bot-api/WebhookActor.cpp

  telegram-bot-api/Client.h
  telegram-bot-api/ClientManager.h
  telegram-bot-api/ClientParameters.h
  telegram-bot-api/HttpConnection.h
  telegram-bot-api/HttpServer.h
  telegram-bot-api/HttpStatConnection.h
  telegram-bot-api/Query.h
  telegram-bot-api/Stats.h
  telegram-bot-api/Watchdog.h
  telegram-bot-api/WebhookActor.h
)

add_executable(telegram-bot-api ${TELEGRAM_BOT_API_SOURCE})
target_include_directories(telegram-bot-api PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(telegram-bot-api PRIVATE memprof tdactor tdcore tddb tdnet tdutils)

install(TARGETS telegram-bot-api RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if (MSVC AND VCPKG_TOOLCHAIN)
  install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/" DESTINATION "${CMAKE_INSTALL_BINDIR}" FILES_MATCHING PATTERN "*.dll" PATTERN "*.pdb")
endif()

option(TELEGRAM_BOT_API_BUILD_TESTS "Build the telegram-bot-api tests" OFF)
if (TELEGRAM_BOT_API_BUILD_TESTS)
  enable_testing()

  # The test drives the real Client.cpp, but sends the requests for TDLib to the test instead of a td::ClientActor.
  set(TELEGRAM_BOT_API_TEST_CLIENT "${CMAKE_CURRENT_BINARY_DIR}/test/Client.test.cpp")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS telegram-bot-api/Client.cpp)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/telegram-bot-api/Client.cpp" TELEGRAM_BOT_API_CLIENT_CPP)
  set(TELEGRAM_BOT_API_SEND_REQUEST
"  CHECK(!td_client_.empty());
  auto id = handlers_.create(std::move(handler));
  send_closure(td_client_, &td::ClientActor::request, id, std::move(f));")
  string(FIND "${TELEGRAM_BOT_API_CLIENT_CPP}" "${TELEGRAM_BOT_API_SEND_REQUEST}" TELEGRAM_BOT_API_SEND_REQUEST_POS)
  if (TELEGRAM_BOT_API_SEND_REQUEST_POS EQUAL -1)
    message(FATAL_ERROR "Client::do_send_request has changed, update the test")
  endif()
  string(REPLACE "${TELEGRAM_BOT_API_SEND_REQUEST}"
"  auto id = handlers_.create(std::move(handler));
  test_record_request(id, std::move(f));" TELEGRAM_BOT_API_CLIENT_CPP "${TELEGRAM_BOT_API_CLIENT_CPP}")
  file(WRITE "${TELEGRAM_BOT_API_TEST_CLIENT}" "${TELEGRAM_BOT_API_CLIENT_CPP}")

  set(TELEGRAM_BOT_API_TEST_SOURCE ${TELEGRAM_BOT_API_SOURCE})
  list(REMOVE_ITEM TELEGRAM_BOT_API_TEST_SOURCE telegram-bot-api/telegram-bot-api.cpp telegram-bot-api/Client.cpp)
  add_executable(test-join-chat-result test/join-chat-result.cpp ${TELEGRAM_BOT_API_TEST_SOURCE})
  target_include_directories(test-join-chat-result PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                                                           $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/test>)
  target_link_libraries(test-join-chat-result PRIVATE memprof tdactor tdcore tddb tdnet tdutils)

  foreach (TEST_CASE chat_id-success invite_link-success chat_id-request_sent invite_link-request_sent
                     chat_id-guard_bot_approval_required invite_link-guard_bot_approval_required chat_id-declined
                     invite_link-declined)
    add_test(NAME join-chat-result-${TEST_CASE} COMMAND test-join-chat-result ${TEST_CASE})
  endforeach()
endif()
