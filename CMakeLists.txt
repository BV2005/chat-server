cmake_minimum_required(VERSION 3.20)
project(untitled)
set(CMAKE_CXX_STANDARD 20)
add_executable(tcp_server TCP_SERVER.cpp
        packet.cpp
        "Client_create_and_manage.cpp"
        Client_create_and_manage.h
        Router.h
        Router.cpp
        packet.h
        Room_manager.h
        Room_manager.cpp)
target_link_libraries(tcp_server ws2_32 mswsock)

add_executable(tcp_client Chat_client.cpp
        Chat_client.h
        packet.cpp
        packet.h)
target_link_libraries(tcp_client ws2_32)
