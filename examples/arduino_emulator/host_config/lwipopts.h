#pragma once

#define NO_SYS       1
#define LWIP_SOCKET  0
#define LWIP_NETCONN 0
#define LWIP_TCP     1
#define LWIP_IPV6    1

#if defined(__linux__) && !defined(LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS)
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS 1
#endif
