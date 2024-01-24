#ifndef CAFE_CLASS_RW_HPP
#define CAFE_CLASS_RW_HPP

#include "data_rw.hpp"
#include "class_file.hpp"

namespace cafe {
class_file read(data_reader& reader);

class_file read(data_reader&& reader);
}

#endif //CAFE_CLASS_RW_HPP
