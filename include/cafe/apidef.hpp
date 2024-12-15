#pragma once

#ifdef CAFE_SHARED_LIB
#ifdef CAFE_EXPORTS
#define CAFE_API __declspec(dllexport)
#else
#define CAFE_API __declspec(dllimport)
#endif
#else
#define CAFE_API
#endif