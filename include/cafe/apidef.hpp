#pragma once

#if defined(CAFE_BUILDING_LIB)
#define CAFE_API __declspec(dllexport)
#elif defined(CAFE_STATIC_LIB)
#define CAFE_API
#else
#define CAFE_API __declspec(dllimport)
#endif
