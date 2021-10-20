cmake_minimum_required(VERSION 3.1)

project(chat)


add_executable(chats server.c)
add_executable(chatc client.c)
add_executable(evil  evil.c)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(chats PRIVATE Threads::Threads)
target_link_libraries(chatc PRIVATE Threads::Threads)
target_link_libraries(evil PRIVATE Threads::Threads)
